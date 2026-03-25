/*
 * reznosql.c - RezNoSQL: Single-file implementation
 *
 * All CRUD, browse, management, and diagnostics in one file.
 * Uses plain VSAM KSDS via fopen/fread/fwrite/flocate/fdelrec.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "reznosql.h"

/* --------------------------------------------------------------------
 * Internal connection structure
 * -------------------------------------------------------------------- */

typedef struct rznsq_index {
    char           aix_dsname[48];
    char           alt_key_name[64];
    FILE          *fp;
    unsigned int   key_size;
    int            non_unique;      /* 1 = composite key: alt+primary    */
    int            active;
} rznsq_index_t;

typedef struct rznsq_conn {
    FILE          *fp;
    char           dsname[48];
    char           primary_key_name[64];
    unsigned int   key_size;
    unsigned int   max_rec_size;
    int            browse_active;
    int            auto_key;         /* 1 = auto-generate rznsq_id        */
    unsigned long  auto_key_seq;     /* Sequence counter for auto keys     */
    char           last_error[RZNSQ_MAX_ERRMSG];
    rznsq_stats    stats;
    rznsq_index_t  indexes[RZNSQ_MAX_INDEXES];
    int            num_indexes;
} rznsq_conn_t;

/* --------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------- */

static void set_error(rznsq_conn_t *c, const char *msg)
{
    if (c) {
        strncpy(c->last_error, msg, RZNSQ_MAX_ERRMSG - 1);
        c->last_error[RZNSQ_MAX_ERRMSG - 1] = '\0';
    }
}

static void pad_key(char *dst, const char *src, size_t src_len,
                    unsigned int ks)
{
    memset(dst, 0, ks);
    if (src_len > ks) src_len = ks;
    memcpy(dst, src, src_len);
}

static void encode_u32(unsigned char *p, unsigned int v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >>  8) & 0xFF;
    p[3] =  v        & 0xFF;
}

static unsigned int decode_u32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) |
           ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] <<  8) |
            (unsigned int)p[3];
}

/* Strip surrounding quotes from a string: "foo" -> foo */
static size_t strip_quotes(const char *in, char *out, size_t out_size)
{
    size_t len = strlen(in);
    if (len >= 2 && in[0] == '"' && in[len - 1] == '"') {
        len -= 2;
        if (len >= out_size) len = out_size - 1;
        memcpy(out, in + 1, len);
        out[len] = '\0';
        return len;
    }
    if (len >= out_size) len = out_size - 1;
    memcpy(out, in, len);
    out[len] = '\0';
    return len;
}

/*
 * Generate a unique key: 16 hex chars from time + 16 hex from counter.
 * Total: 32 hex characters (RZNSQ_AUTO_KEY_LEN).
 */
static void generate_auto_key(rznsq_conn_t *c, char *out)
{
    unsigned long ts = (unsigned long)time(NULL);
    unsigned long seq = c->auto_key_seq++;
    sprintf(out, "%016lX%016lX", ts, seq);
}

/*
 * Prepend "rznsq_id":"<key>" into a JSON document.
 * Input doc must start with '{'.
 * Returns new doc length, or -1 on error.
 * new_doc must be at least doc_len + RZNSQ_AUTO_KEY_LEN + 20 bytes.
 */
static int inject_auto_key(const char *doc, size_t doc_len,
                           const char *key_val,
                           char *new_doc, size_t new_doc_size)
{
    int prefix_len;
    size_t total;

    if (doc_len < 2 || doc[0] != '{')
        return -1;

    /* Build: {"rznsq_id":"<key>", ... rest ... */
    prefix_len = sprintf(new_doc, "{\"rznsq_id\":\"%s\",", key_val);
    total = prefix_len + (doc_len - 1); /* -1 to skip opening '{' */
    if (total >= new_doc_size) return -1;
    memcpy(new_doc + prefix_len, doc + 1, doc_len - 1);
    return (int)total;
}

/*
 * Extract a JSON string value for a given key name.
 * key_name includes quotes, e.g. "\"_id\""
 * Returns length of extracted value, or -1 on failure.
 * Value is written WITHOUT surrounding quotes.
 */
static int json_extract_key(const char *json, size_t json_len,
                            const char *key_name,
                            char *val_out, size_t val_size)
{
    const char *p, *end, *vs, *ve;
    char search_key[128];
    size_t klen;

    /* Build search pattern: key_name followed by : */
    klen = strlen(key_name);
    if (klen + 2 > sizeof(search_key)) return -1;
    memcpy(search_key, key_name, klen);
    search_key[klen] = '\0';

    end = json + json_len;
    p = json;

    while (p < end) {
        /* Find key_name in JSON */
        const char *found = NULL;
        size_t remain = end - p;
        size_t i;
        for (i = 0; i + klen <= remain; i++) {
            if (memcmp(p + i, search_key, klen) == 0) {
                found = p + i;
                break;
            }
        }
        if (!found) return -1;

        /* Skip past key and whitespace to find colon */
        p = found + klen;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p >= end || *p != ':') continue;
        p++; /* skip colon */
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p >= end) return -1;

        /* Extract value */
        if (*p == '"') {
            /* String value */
            vs = p + 1;
            ve = vs;
            while (ve < end && *ve != '"') {
                if (*ve == '\\' && ve + 1 < end) ve++; /* skip escaped */
                ve++;
            }
            if (ve >= end) return -1;
            {
                size_t vlen = ve - vs;
                if (vlen >= val_size) vlen = val_size - 1;
                memcpy(val_out, vs, vlen);
                val_out[vlen] = '\0';
                return (int)vlen;
            }
        } else {
            /* Number or other - take until , or } */
            vs = p;
            ve = vs;
            while (ve < end && *ve != ',' && *ve != '}' && *ve != ' ')
                ve++;
            {
                size_t vlen = ve - vs;
                if (vlen >= val_size) vlen = val_size - 1;
                memcpy(val_out, vs, vlen);
                val_out[vlen] = '\0';
                return (int)vlen;
            }
        }
    }
    return -1;
}

/* Run IDCAMS with sysin from a temp file */
static int run_idcams(const char *sysin)
{
    FILE *f = fopen("/tmp/rznsq_sysin.txt", "w");
    if (!f) return -1;
    fprintf(f, "%s", sysin);
    fclose(f);
    return system("mvscmdauth --pgm=IDCAMS '--sysprint=*'"
                  " --sysin=stdin < /tmp/rznsq_sysin.txt");
}

/* Open a VSAM KSDS, handling empty-dataset bootstrap */
static FILE *open_ksds(const char *dsname)
{
    char dsn[128];
    FILE *fp;

    sprintf(dsn, "//'%s'", dsname);
    fp = fopen(dsn, "rb+,type=record");
    if (!fp) {
        fp = fopen(dsn, "wb,type=record");
        if (fp) {
            fclose(fp);
            fp = fopen(dsn, "rb+,type=record");
        }
    }
    return fp;
}

/* Pack a record: key + doc_len(4) + json */
static int pack_record(char *buf, const char *key, size_t key_len,
                       unsigned int ks,
                       const char *json, unsigned int json_len)
{
    unsigned int hdr = RZNSQ_REC_HEADER(ks);
    pad_key(buf, key, key_len, ks);
    encode_u32((unsigned char *)(buf + ks), json_len);
    memcpy(buf + hdr, json, json_len);
    return hdr + json_len;
}

/*
 * Build AIX key for an index.
 * Non-unique: composite [alt_val padded to ks][primary_val padded to ks]
 * Unique:     [alt_val padded to ks]
 * Returns total AIX key length.
 */
static unsigned int build_aix_key(char *dst, rznsq_index_t *idx,
                                  const char *alt_val, size_t alt_len,
                                  const char *primary_val, size_t primary_len)
{
    unsigned int ks = idx->key_size;
    pad_key(dst, alt_val, alt_len, ks);
    if (idx->non_unique) {
        pad_key(dst + ks, primary_val, primary_len, ks);
        return ks * 2;
    }
    return ks;
}

/* Write to secondary indexes: store alt_key -> primary_key mapping */
static void index_write(rznsq_conn_t *c, const char *json, size_t json_len,
                        const char *primary_val, size_t primary_len)
{
    int i;
    for (i = 0; i < c->num_indexes; i++) {
        rznsq_index_t *idx = &c->indexes[i];
        char alt_val[RZNSQ_KEY_MAX + 1];
        int alt_len;
        char aix_key[RZNSQ_KEY_MAX * 2];
        unsigned int aix_ks;
        char rec[RZNSQ_KEY_MAX * 4 + 8];
        int rec_len;

        if (!idx->active || !idx->fp) continue;

        alt_len = json_extract_key(json, json_len,
                                   idx->alt_key_name,
                                   alt_val, sizeof(alt_val));
        if (alt_len < 0) continue;

        aix_ks = build_aix_key(aix_key, idx,
                               alt_val, alt_len,
                               primary_val, primary_len);

        /* AIX record: aix_key + doc_len + primary_key_value */
        rec_len = pack_record(rec, aix_key, aix_ks, aix_ks,
                              primary_val, primary_len);
        if (fwrite(rec, rec_len, 1, idx->fp) != 1)
            clearerr(idx->fp);
    }
}

/* Delete from secondary indexes */
static void index_delete(rznsq_conn_t *c, const char *json, size_t json_len,
                         const char *primary_val, size_t primary_len)
{
    int i;
    for (i = 0; i < c->num_indexes; i++) {
        rznsq_index_t *idx = &c->indexes[i];
        char alt_val[RZNSQ_KEY_MAX + 1];
        int alt_len;
        char aix_key[RZNSQ_KEY_MAX * 2];
        unsigned int aix_ks;
        char buf[RZNSQ_KEY_MAX * 4 + RZNSQ_MAX_DOC];

        if (!idx->active || !idx->fp) continue;

        alt_len = json_extract_key(json, json_len,
                                   idx->alt_key_name,
                                   alt_val, sizeof(alt_val));
        if (alt_len < 0) continue;

        aix_ks = build_aix_key(aix_key, idx,
                               alt_val, alt_len,
                               primary_val, primary_len);

        if (flocate(idx->fp, aix_key, aix_ks, __KEY_EQ) != 0)
            continue;
        if (fread(buf, 1, sizeof(buf), idx->fp) >= (aix_ks + RZNSQ_DOCLEN_SIZE))
            fdelrec(idx->fp);
        clearerr(idx->fp);
    }
}

/* --------------------------------------------------------------------
 * Data Management
 * -------------------------------------------------------------------- */

int rznsq_create(const char *dsname, const rznsq_create_options *options)
{
    char sysin[1024];
    unsigned int ks, avg_rec, max_rec, mb;

    if (!dsname || !options)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);

    ks = options->key_size;
    if (ks == 0) ks = RZNSQ_KEY_DEFAULT;
    if (ks < RZNSQ_KEY_MIN) ks = RZNSQ_KEY_MIN;
    if (ks > RZNSQ_KEY_MAX) ks = RZNSQ_KEY_MAX;

    avg_rec = RZNSQ_REC_HEADER(ks) + (options->avg_doc_size ? options->avg_doc_size : 256);
    max_rec = RZNSQ_MAX_REC(ks);
    mb = options->max_space ? options->max_space : 1;

    sprintf(sysin,
        " DEFINE CLUSTER ( -\n"
        "   NAME('%s') -\n"
        "   INDEXED -\n"
        "   KEYS(%u 0) -\n"
        "   RECORDSIZE(%u %u) -\n"
        "   MEGABYTES(%u %u) -\n"
        "   SHAREOPTIONS(2 3) -\n"
        "   SPEED -\n"
        " ) -\n"
        " DATA ( -\n"
        "   NAME('%s.DATA') -\n"
        " ) -\n"
        " INDEX ( -\n"
        "   NAME('%s.INDEX') -\n"
        " )",
        dsname, ks, avg_rec, max_rec, mb, mb,
        dsname, dsname);

    if (run_idcams(sysin) != 0)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_IDCAMS_FAILED);
    return RZNSQ_RC_OK;
}

int rznsq_destroy(const char *dsname)
{
    char sysin[256];
    if (!dsname)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);

    sprintf(sysin, " DELETE '%s' CLUSTER PURGE", dsname);
    run_idcams(sysin);
    return RZNSQ_RC_OK;
}

int rznsq_create_index(const rznsq_create_index_options *options)
{
    char sysin[1024];
    unsigned int ks, aix_ks, avg_rec, max_rec, mb;

    if (!options || !options->aix_name || !options->base_name)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);

    ks = options->key_size;
    if (ks == 0) ks = RZNSQ_KEY_DEFAULT;
    if (ks < RZNSQ_KEY_MIN) ks = RZNSQ_KEY_MIN;
    if (ks > RZNSQ_KEY_MAX) ks = RZNSQ_KEY_MAX;

    /* Non-unique: composite key = alt_key + primary_key (2x key_size) */
    aix_ks = (options->flags & RZNSQ_INDEX_NON_UNIQUE) ? ks * 2 : ks;

    /* AIX record: composite_key + 4-byte len + primary_key_value */
    avg_rec = aix_ks + RZNSQ_DOCLEN_SIZE + ks;
    max_rec = aix_ks + RZNSQ_DOCLEN_SIZE + RZNSQ_KEY_MAX;
    mb = options->max_space ? options->max_space : 1;

    sprintf(sysin,
        " DEFINE CLUSTER ( -\n"
        "   NAME('%s') -\n"
        "   INDEXED -\n"
        "   KEYS(%u 0) -\n"
        "   RECORDSIZE(%u %u) -\n"
        "   MEGABYTES(%u %u) -\n"
        "   SHAREOPTIONS(2 3) -\n"
        "   SPEED -\n"
        " ) -\n"
        " DATA ( -\n"
        "   NAME('%s.DATA') -\n"
        " ) -\n"
        " INDEX ( -\n"
        "   NAME('%s.INDEX') -\n"
        " )",
        options->aix_name, aix_ks, avg_rec, max_rec, mb, mb,
        options->aix_name, options->aix_name);

    if (run_idcams(sysin) != 0)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_IDCAMS_FAILED);
    return RZNSQ_RC_OK;
}

int rznsq_add_index(rznsq_connection_t conn,
                    const rznsq_add_index_options *options)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    rznsq_index_t *idx;

    if (!c || !options || !options->aix_name || !options->alt_key)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    if (c->num_indexes >= RZNSQ_MAX_INDEXES)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);

    idx = &c->indexes[c->num_indexes];
    memset(idx, 0, sizeof(*idx));
    strncpy(idx->aix_dsname, options->aix_name, sizeof(idx->aix_dsname) - 1);
    strncpy(idx->alt_key_name, options->alt_key, sizeof(idx->alt_key_name) - 1);
    idx->key_size = c->key_size;
    idx->non_unique = (options->flags & RZNSQ_INDEX_NON_UNIQUE) ? 1 : 0;

    idx->fp = open_ksds(options->aix_name);
    if (!idx->fp) {
        set_error(c, "Failed to open AIX dataset");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_OPEN_FAILED);
    }

    idx->active = 1;
    c->num_indexes++;
    return RZNSQ_RC_OK;
}

int rznsq_drop_index(rznsq_connection_t conn, const char *aix_name)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    int i;

    if (!c || !aix_name)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);

    for (i = 0; i < c->num_indexes; i++) {
        if (strcmp(c->indexes[i].aix_dsname, aix_name) == 0) {
            if (c->indexes[i].fp) {
                fclose(c->indexes[i].fp);
                c->indexes[i].fp = NULL;
            }
            c->indexes[i].active = 0;
            return RZNSQ_RC_OK;
        }
    }
    return RZNSQ_MAKE_RC(RZNSQ_RC_WARN, RZNSQ_RSN_NOT_FOUND);
}

int rznsq_report_stats(rznsq_connection_t conn, rznsq_stats *stats)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    if (!c || !stats)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    *stats = c->stats;
    return RZNSQ_RC_OK;
}

/* --------------------------------------------------------------------
 * Connection Management
 * -------------------------------------------------------------------- */

int rznsq_open(rznsq_connection_t *conn, const char *dsname,
               unsigned int flags, const rznsq_open_options *options)
{
    rznsq_conn_t *c;
    unsigned int ks;

    if (!conn || !dsname)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);

    c = (rznsq_conn_t *)calloc(1, sizeof(rznsq_conn_t));
    if (!c)
        return RZNSQ_MAKE_RC(RZNSQ_RC_SEVERE, RZNSQ_RSN_ALLOC_FAILED);

    ks = RZNSQ_KEY_DEFAULT;
    if (options) {
        if (options->key_size >= RZNSQ_KEY_MIN &&
            options->key_size <= RZNSQ_KEY_MAX)
            ks = options->key_size;
        if (options->primary_key) {
            strncpy(c->primary_key_name, options->primary_key,
                    sizeof(c->primary_key_name) - 1);
        } else {
            /* Auto-key mode: use rznsq_id */
            strncpy(c->primary_key_name, RZNSQ_AUTO_KEY_NAME,
                    sizeof(c->primary_key_name) - 1);
            c->auto_key = 1;
            c->auto_key_seq = 0;
        }
    }

    c->key_size = ks;
    c->max_rec_size = RZNSQ_MAX_REC(ks);
    strncpy(c->dsname, dsname, sizeof(c->dsname) - 1);

    c->fp = open_ksds(dsname);
    if (!c->fp) {
        set_error(c, "Failed to open KSDS");
        free(c);
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_OPEN_FAILED);
    }

    *conn = c;
    return RZNSQ_RC_OK;
}

int rznsq_close(rznsq_connection_t conn)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    int i;

    if (!c) return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);

    for (i = 0; i < c->num_indexes; i++) {
        if (c->indexes[i].fp)
            fclose(c->indexes[i].fp);
    }
    if (c->fp) fclose(c->fp);
    free(c);
    return RZNSQ_RC_OK;
}

/* --------------------------------------------------------------------
 * Document CRUD
 * -------------------------------------------------------------------- */

int rznsq_write(rznsq_connection_t conn, const char *doc,
                size_t doc_len, const rznsq_write_options *options)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    char key_val[RZNSQ_KEY_MAX + 1];
    const char *key_name;
    int klen;
    char *rec;
    int rec_len;

    if (!c || !doc)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    if (!c->fp)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_OPEN);

    /* Determine key name: from options, or from connection primary_key */
    key_name = NULL;
    if (options && options->key_name)
        key_name = options->key_name;
    else if (c->primary_key_name[0])
        key_name = c->primary_key_name;

    if (!key_name) {
        set_error(c, "No key name specified");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    }

    /* Extract key value from JSON document */
    klen = json_extract_key(doc, doc_len, key_name, key_val, sizeof(key_val));

    if (klen < 0 && c->auto_key) {
        /* Auto-generate key and inject into document */
        char *aug_doc;
        int aug_len;
        size_t aug_size = doc_len + RZNSQ_AUTO_KEY_LEN + 64;

        generate_auto_key(c, key_val);
        klen = (int)strlen(key_val);

        aug_doc = (char *)malloc(aug_size);
        if (!aug_doc)
            return RZNSQ_MAKE_RC(RZNSQ_RC_SEVERE, RZNSQ_RSN_ALLOC_FAILED);

        aug_len = inject_auto_key(doc, doc_len, key_val, aug_doc, aug_size);
        if (aug_len < 0) {
            free(aug_doc);
            set_error(c, "Failed to inject auto key (doc must start with '{')");
            return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_JSON_PARSE);
        }

        /* Write with augmented doc */
        rec = (char *)malloc(c->max_rec_size);
        if (!rec) { free(aug_doc); return RZNSQ_MAKE_RC(RZNSQ_RC_SEVERE, RZNSQ_RSN_ALLOC_FAILED); }

        rec_len = pack_record(rec, key_val, klen, c->key_size,
                              aug_doc, (unsigned int)aug_len);

        if (fwrite(rec, rec_len, 1, c->fp) != 1) {
            clearerr(c->fp);
            free(rec); free(aug_doc);
            set_error(c, "fwrite failed (duplicate key or I/O error)");
            return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_DUP_KEY);
        }

        index_write(c, aug_doc, aug_len, key_val, klen);
        free(rec); free(aug_doc);
        c->stats.writes++;
        return RZNSQ_RC_OK;
    }

    if (klen < 0) {
        set_error(c, "Failed to extract key from JSON");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_JSON_PARSE);
    }

    rec = (char *)malloc(c->max_rec_size);
    if (!rec)
        return RZNSQ_MAKE_RC(RZNSQ_RC_SEVERE, RZNSQ_RSN_ALLOC_FAILED);

    rec_len = pack_record(rec, key_val, klen, c->key_size,
                          doc, (unsigned int)doc_len);

    if (fwrite(rec, rec_len, 1, c->fp) != 1) {
        clearerr(c->fp);
        free(rec);
        set_error(c, "fwrite failed (duplicate key or I/O error)");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_DUP_KEY);
    }

    free(rec);

    /* Update secondary indexes */
    index_write(c, doc, doc_len, key_val, klen);

    c->stats.writes++;
    return RZNSQ_RC_OK;
}

int rznsq_read(rznsq_connection_t conn, const char *key_name,
               const char *key_value, char *buf, size_t *buf_len,
               unsigned int flags, const rznsq_read_options *options)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    unsigned int ks, hdr;
    char lookup_val[RZNSQ_KEY_MAX + 1];
    size_t lookup_len;
    char padded[RZNSQ_KEY_MAX];
    char *rec;
    size_t bytes_read;
    unsigned int doc_len;
    FILE *search_fp;
    int is_alt_key = 0;

    if (!c || !key_value || !buf || !buf_len)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    if (!c->fp)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_OPEN);

    ks = c->key_size;
    hdr = RZNSQ_REC_HEADER(ks);

    /* Strip quotes from key_value */
    lookup_len = strip_quotes(key_value, lookup_val, sizeof(lookup_val));

    /* Check if this is an alternate key lookup */
    search_fp = c->fp;
    if (key_name && c->primary_key_name[0] &&
        strcmp(key_name, c->primary_key_name) != 0) {
        int i;
        for (i = 0; i < c->num_indexes; i++) {
            if (c->indexes[i].active &&
                strcmp(c->indexes[i].alt_key_name, key_name) == 0) {
                search_fp = c->indexes[i].fp;
                is_alt_key = 1;
                break;
            }
        }
    }

    if (is_alt_key) {
        /* Alt-key lookup: read from AIX, then from main KSDS */
        rznsq_index_t *idx = NULL;
        unsigned int aix_ks;
        char primary_val[RZNSQ_KEY_MAX + 1];
        unsigned int plen;
        int ii;

        for (ii = 0; ii < c->num_indexes; ii++) {
            if (c->indexes[ii].active &&
                strcmp(c->indexes[ii].alt_key_name, key_name) == 0) {
                idx = &c->indexes[ii];
                break;
            }
        }

        /* For non-unique: use __KEY_GE with alt_key portion only */
        pad_key(padded, lookup_val, lookup_len, ks);
        if (idx->non_unique) {
            /* Composite key: [alt_key ks bytes][primary_key ks bytes] */
            char composite[RZNSQ_KEY_MAX * 2];
            aix_ks = ks * 2;
            memcpy(composite, padded, ks);
            memset(composite + ks, 0, ks); /* primary = all zeros for GE */
            if (flocate(idx->fp, composite, aix_ks, __KEY_GE) != 0) {
                set_error(c, "Alt key not found");
                return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_FOUND);
            }
        } else {
            aix_ks = ks;
            if (flocate(idx->fp, padded, aix_ks, __KEY_EQ) != 0) {
                set_error(c, "Alt key not found");
                return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_FOUND);
            }
        }

        rec = (char *)malloc(aix_ks + RZNSQ_DOCLEN_SIZE + RZNSQ_KEY_MAX);
        if (!rec)
            return RZNSQ_MAKE_RC(RZNSQ_RC_SEVERE, RZNSQ_RSN_ALLOC_FAILED);

        bytes_read = fread(rec, 1, aix_ks + RZNSQ_DOCLEN_SIZE + RZNSQ_KEY_MAX,
                           idx->fp);
        if (bytes_read < aix_ks + RZNSQ_DOCLEN_SIZE) {
            clearerr(idx->fp);
            free(rec);
            set_error(c, "fread AIX failed");
            return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_IO_ERROR);
        }

        /* Verify alt_key prefix matches (for GE search) */
        if (idx->non_unique && memcmp(rec, padded, ks) != 0) {
            free(rec);
            set_error(c, "Alt key not found (GE mismatch)");
            return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_FOUND);
        }

        /* Data portion = primary key value */
        plen = decode_u32((unsigned char *)(rec + aix_ks));
        if (plen > RZNSQ_KEY_MAX) plen = RZNSQ_KEY_MAX;
        memcpy(primary_val, rec + aix_ks + RZNSQ_DOCLEN_SIZE, plen);
        primary_val[plen] = '\0';
        free(rec);

        /* Now read from main KSDS by primary key */
        pad_key(padded, primary_val, plen, ks);
        if (flocate(c->fp, padded, ks, __KEY_EQ) != 0) {
            set_error(c, "Primary key not found via alt-key");
            return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_FOUND);
        }

        rec = (char *)malloc(c->max_rec_size);
        if (!rec)
            return RZNSQ_MAKE_RC(RZNSQ_RC_SEVERE, RZNSQ_RSN_ALLOC_FAILED);

        bytes_read = fread(rec, 1, c->max_rec_size, c->fp);
        if (bytes_read < hdr) {
            clearerr(c->fp);
            free(rec);
            set_error(c, "fread primary failed");
            return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_IO_ERROR);
        }
    } else {
        /* Primary key lookup */
        pad_key(padded, lookup_val, lookup_len, ks);

        if (flocate(c->fp, padded, ks, __KEY_EQ) != 0) {
            set_error(c, "Key not found");
            return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_FOUND);
        }

        rec = (char *)malloc(c->max_rec_size);
        if (!rec)
            return RZNSQ_MAKE_RC(RZNSQ_RC_SEVERE, RZNSQ_RSN_ALLOC_FAILED);

        bytes_read = fread(rec, 1, c->max_rec_size, c->fp);
        if (bytes_read < hdr) {
            clearerr(c->fp);
            free(rec);
            set_error(c, "fread failed");
            return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_IO_ERROR);
        }
    }

    doc_len = decode_u32((unsigned char *)(rec + ks));

    if (doc_len > *buf_len) {
        *buf_len = doc_len;
        free(rec);
        set_error(c, "Buffer too small");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_BUF_TOO_SMALL);
    }

    memcpy(buf, rec + hdr, doc_len);
    *buf_len = doc_len;
    free(rec);

    c->stats.reads++;
    return RZNSQ_RC_OK;
}

int rznsq_delete(rznsq_connection_t conn, const char *key_name,
                 const char *key_value, const rznsq_delete_options *options)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    unsigned int ks, hdr;
    char lookup_val[RZNSQ_KEY_MAX + 1];
    size_t lookup_len;
    char padded[RZNSQ_KEY_MAX];
    char *rec;
    size_t bytes_read;
    unsigned int doc_len;

    if (!c || !key_value)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    if (!c->fp)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_OPEN);

    ks = c->key_size;
    hdr = RZNSQ_REC_HEADER(ks);
    lookup_len = strip_quotes(key_value, lookup_val, sizeof(lookup_val));
    pad_key(padded, lookup_val, lookup_len, ks);

    if (flocate(c->fp, padded, ks, __KEY_EQ) != 0) {
        set_error(c, "Key not found for delete");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_FOUND);
    }

    /* Read record to get JSON (needed for index cleanup) */
    rec = (char *)malloc(c->max_rec_size);
    if (!rec)
        return RZNSQ_MAKE_RC(RZNSQ_RC_SEVERE, RZNSQ_RSN_ALLOC_FAILED);

    bytes_read = fread(rec, 1, c->max_rec_size, c->fp);
    if (bytes_read < hdr) {
        clearerr(c->fp);
        free(rec);
        set_error(c, "fread failed before delete");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_IO_ERROR);
    }

    /* Delete from secondary indexes before deleting record */
    doc_len = decode_u32((unsigned char *)(rec + ks));
    if (c->num_indexes > 0 && doc_len > 0)
        index_delete(c, rec + hdr, doc_len, lookup_val, lookup_len);

    free(rec);

    if (fdelrec(c->fp) != 0) {
        clearerr(c->fp);
        set_error(c, "fdelrec failed");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_IO_ERROR);
    }

    c->stats.deletes++;
    return RZNSQ_RC_OK;
}

int rznsq_update(rznsq_connection_t conn, const char *key_name,
                 const char *key_value, const char *doc, size_t doc_len,
                 const rznsq_update_options *options)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    rznsq_delete_options dopts;
    rznsq_write_options wopts;
    int rc;

    if (!c || !key_value || !doc)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);

    /* Delete old, then write new */
    memset(&dopts, 0, sizeof(dopts));
    rc = rznsq_delete(conn, key_name, key_value, &dopts);
    if (rc != RZNSQ_RC_OK) return rc;

    memset(&wopts, 0, sizeof(wopts));
    wopts.key_name = key_name;
    rc = rznsq_write(conn, doc, doc_len, &wopts);
    if (rc == RZNSQ_RC_OK) {
        /* Adjust stats: write already counted, just fix the update counter */
        c->stats.writes--;
        c->stats.deletes--;
        c->stats.updates++;
    }
    return rc;
}

/* --------------------------------------------------------------------
 * Browse
 * -------------------------------------------------------------------- */

int rznsq_position(rznsq_connection_t conn, const char *key_name,
                   const char *key_value,
                   const rznsq_position_options *options)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    unsigned int ks;
    char lookup_val[RZNSQ_KEY_MAX + 1];
    size_t lookup_len;
    char padded[RZNSQ_KEY_MAX];
    int locate_mode;

    if (!c || !key_value)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    if (!c->fp)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_OPEN);

    ks = c->key_size;
    lookup_len = strip_quotes(key_value, lookup_val, sizeof(lookup_val));
    pad_key(padded, lookup_val, lookup_len, ks);

    locate_mode = __KEY_EQ;
    if (options && options->generic)
        locate_mode = __KEY_GE;

    if (flocate(c->fp, padded, ks, locate_mode) != 0) {
        set_error(c, "flocate failed for position");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NOT_FOUND);
    }

    c->browse_active = 1;
    return RZNSQ_RC_OK;
}

int rznsq_next_result(rznsq_connection_t conn, char *buf, size_t *buf_len)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    unsigned int ks, hdr;
    char *rec;
    size_t bytes_read;
    unsigned int doc_len;

    if (!c || !buf || !buf_len)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    if (!c->browse_active || !c->fp)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NO_BROWSE);

    ks = c->key_size;
    hdr = RZNSQ_REC_HEADER(ks);

    rec = (char *)malloc(c->max_rec_size);
    if (!rec)
        return RZNSQ_MAKE_RC(RZNSQ_RC_SEVERE, RZNSQ_RSN_ALLOC_FAILED);

    bytes_read = fread(rec, 1, c->max_rec_size, c->fp);
    if (bytes_read < hdr) {
        free(rec);
        if (feof(c->fp))
            return RZNSQ_MAKE_RC(RZNSQ_RC_WARN, RZNSQ_RSN_END_OF_DATA);
        clearerr(c->fp);
        set_error(c, "fread failed during browse");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_IO_ERROR);
    }

    doc_len = decode_u32((unsigned char *)(rec + ks));

    if (doc_len > *buf_len) {
        *buf_len = doc_len;
        free(rec);
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_BUF_TOO_SMALL);
    }

    memcpy(buf, rec + hdr, doc_len);
    *buf_len = doc_len;
    free(rec);

    c->stats.browses++;
    return RZNSQ_RC_OK;
}

int rznsq_close_result(rznsq_connection_t conn)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    if (!c) return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    c->browse_active = 0;
    return RZNSQ_RC_OK;
}

/* Write a new document during browse (keyed insert, browse stays active) */
int rznsq_write_result(rznsq_connection_t conn, const char *doc,
                       size_t doc_len, const rznsq_write_options *options)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    if (!c)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    if (!c->browse_active)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NO_BROWSE);

    /* Delegate to normal write — VSAM KSDS inserts by key regardless */
    return rznsq_write(conn, doc, doc_len, options);
}

/* Delete the last-read record during browse (fdelrec on current position) */
int rznsq_delete_result(rznsq_connection_t conn)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;

    if (!c)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    if (!c->browse_active || !c->fp)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NO_BROWSE);

    if (fdelrec(c->fp) != 0) {
        clearerr(c->fp);
        set_error(c, "fdelrec failed on current browse record");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_IO_ERROR);
    }

    c->stats.deletes++;
    return RZNSQ_RC_OK;
}

/* Update the last-read record during browse (delete current + write new) */
int rznsq_update_result(rznsq_connection_t conn, const char *doc,
                        size_t doc_len)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    rznsq_write_options wopts;
    int rc;

    if (!c || !doc)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_INVALID_PARAM);
    if (!c->browse_active || !c->fp)
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_NO_BROWSE);

    /* Delete current record */
    if (fdelrec(c->fp) != 0) {
        clearerr(c->fp);
        set_error(c, "fdelrec failed during update_result");
        return RZNSQ_MAKE_RC(RZNSQ_RC_ERROR, RZNSQ_RSN_IO_ERROR);
    }

    /* Write replacement */
    memset(&wopts, 0, sizeof(wopts));
    rc = rznsq_write(conn, doc, doc_len, &wopts);
    if (rc == RZNSQ_RC_OK) {
        c->stats.writes--;
        c->stats.updates++;
    }
    return rc;
}

/* --------------------------------------------------------------------
 * Transaction stubs
 * -------------------------------------------------------------------- */

int rznsq_commit(rznsq_connection_t conn)  { return RZNSQ_RC_OK; }
int rznsq_abort(rznsq_connection_t conn)   { return RZNSQ_RC_OK; }

/* --------------------------------------------------------------------
 * Diagnostics
 * -------------------------------------------------------------------- */

const char *rznsq_last_result(rznsq_connection_t conn)
{
    rznsq_conn_t *c = (rznsq_conn_t *)conn;
    if (!c) return "No connection";
    return c->last_error;
}
