/*
 * reznosql.h - RezNoSQL: Lightweight JSON document store over VSAM KSDS
 *
 * API shape mirrors EzNoSQL (znsq_*) but uses plain VSAM KSDS.
 * No RLS, no Coupling Facility, no Parallel Sysplex required.
 *
 * Record layout:
 *   [key: key_size bytes, null-padded]
 *   [doc_len: 4 bytes, big-endian unsigned int]
 *   [json: variable-length UTF-8 document]
 */

#ifndef REZNOSQL_H
#define REZNOSQL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------- */

#define RZNSQ_KEY_MAX        251
#define RZNSQ_KEY_DEFAULT    251   /* Matches EzNoSQL ordered key limit  */
#define RZNSQ_KEY_MIN        4
#define RZNSQ_HASHED_KEY_SIZE 128  /* Matches EzNoSQL unordered key size */
#define RZNSQ_DOCLEN_SIZE    4
#define RZNSQ_MAX_DOC        32000
#define RZNSQ_MAX_INDEXES    16
#define RZNSQ_MAX_ERRMSG     256
#define RZNSQ_AUTO_KEY_NAME  "\"rznsq_id\""
#define RZNSQ_AUTO_KEY_LEN   32    /* Hex chars in auto-generated key    */

/* Record header size given key_size */
#define RZNSQ_REC_HEADER(ks) ((ks) + RZNSQ_DOCLEN_SIZE)
#define RZNSQ_MAX_REC(ks)    (RZNSQ_REC_HEADER(ks) + RZNSQ_MAX_DOC)

/* --------------------------------------------------------------------
 * Return codes
 * -------------------------------------------------------------------- */

#define RZNSQ_RC_OK          0x00000000
#define RZNSQ_RC_WARN        0x00040000
#define RZNSQ_RC_ERROR       0x00080000
#define RZNSQ_RC_SEVERE      0x000C0000

/* Reason codes (low 16 bits) */
#define RZNSQ_RSN_OK              0x0000
#define RZNSQ_RSN_NOT_FOUND       0x0010
#define RZNSQ_RSN_DUP_KEY         0x0014
#define RZNSQ_RSN_END_OF_DATA     0x0043
#define RZNSQ_RSN_BUF_TOO_SMALL   0x0051
#define RZNSQ_RSN_OPEN_FAILED     0x0060
#define RZNSQ_RSN_IDCAMS_FAILED   0x0070
#define RZNSQ_RSN_IO_ERROR        0x0080
#define RZNSQ_RSN_INVALID_PARAM   0x0090
#define RZNSQ_RSN_NOT_OPEN        0x00A0
#define RZNSQ_RSN_NO_BROWSE       0x00B0
#define RZNSQ_RSN_JSON_PARSE      0x00C0
#define RZNSQ_RSN_ALLOC_FAILED    0x00D0

#define rznsq_err(rc) ((unsigned int)(rc) & 0x0000FFFF)
#define RZNSQ_MAKE_RC(sev, rsn) ((sev) | (rsn))

/* Index flags */
#define RZNSQ_INDEX_UNIQUE      0
#define RZNSQ_INDEX_NON_UNIQUE  1

/* Create/open flags */
#define RZNSQ_FLAG_HASHED       0x0001  /* Unordered (hashed) key mode   */

/* --------------------------------------------------------------------
 * Opaque connection handle
 * -------------------------------------------------------------------- */

typedef void *rznsq_connection_t;

/* --------------------------------------------------------------------
 * Option structs
 * -------------------------------------------------------------------- */

typedef struct {
    unsigned int   max_space;       /* MB                                */
    unsigned int   key_size;        /* Key field size (0 = default 251)  */
    unsigned int   avg_doc_size;    /* Avg doc size for RECORDSIZE avg   */
    const char    *primary_key;     /* JSON key name, NULL = auto rznsq_id */
    unsigned int   flags;           /* RZNSQ_FLAG_HASHED for unordered   */
} rznsq_create_options;

typedef struct {
    const char    *base_name;       /* Primary KSDS dataset name         */
    const char    *aix_name;        /* AIX dataset name                  */
    const char    *alt_key;         /* JSON alternate key name           */
    unsigned int   flags;           /* RZNSQ_INDEX_UNIQUE/NON_UNIQUE     */
    unsigned int   max_space;       /* AIX size in MB                    */
    unsigned int   key_size;        /* AIX key size (0 = default 251)    */
} rznsq_create_index_options;

typedef struct {
    const char    *base_name;
    const char    *aix_name;
    const char    *alt_key;
    unsigned int   flags;           /* RZNSQ_INDEX_UNIQUE/NON_UNIQUE     */
} rznsq_add_index_options;

typedef struct {
    const char    *primary_key;     /* JSON primary key name, NULL=auto  */
    unsigned int   key_size;        /* Must match create key_size        */
    unsigned int   flags;           /* RZNSQ_FLAG_HASHED for unordered   */
} rznsq_open_options;

typedef struct {
    const char    *key_name;
} rznsq_write_options;

typedef struct {
    unsigned int   reserved;
} rznsq_read_options;

typedef struct {
    unsigned int   reserved;
} rznsq_update_options;

typedef struct {
    unsigned int   reserved;
} rznsq_delete_options;

typedef struct {
    const char    *key_name;
    unsigned int   generic;         /* 1 = >= search, 0 = exact          */
} rznsq_position_options;

typedef struct {
    unsigned long  reads;
    unsigned long  writes;
    unsigned long  updates;
    unsigned long  deletes;
    unsigned long  browses;
} rznsq_stats;

/* --------------------------------------------------------------------
 * Data Management
 * -------------------------------------------------------------------- */

int rznsq_create(const char *dsname, const rznsq_create_options *options);
int rznsq_destroy(const char *dsname);
int rznsq_create_index(const rznsq_create_index_options *options);
int rznsq_add_index(rznsq_connection_t conn,
                    const rznsq_add_index_options *options);
int rznsq_drop_index(rznsq_connection_t conn, const char *aix_name);
int rznsq_report_stats(rznsq_connection_t conn, rznsq_stats *stats);

/* --------------------------------------------------------------------
 * Connection Management
 * -------------------------------------------------------------------- */

int rznsq_open(rznsq_connection_t *conn, const char *dsname,
               unsigned int flags, const rznsq_open_options *options);
int rznsq_close(rznsq_connection_t conn);

/* --------------------------------------------------------------------
 * Document CRUD
 * -------------------------------------------------------------------- */

int rznsq_write(rznsq_connection_t conn, const char *doc,
                size_t doc_len, const rznsq_write_options *options);

int rznsq_read(rznsq_connection_t conn, const char *key_name,
               const char *key_value, char *buf, size_t *buf_len,
               unsigned int flags, const rznsq_read_options *options);

int rznsq_delete(rznsq_connection_t conn, const char *key_name,
                 const char *key_value, const rznsq_delete_options *options);

int rznsq_update(rznsq_connection_t conn, const char *key_name,
                 const char *key_value, const char *doc, size_t doc_len,
                 const rznsq_update_options *options);

/* --------------------------------------------------------------------
 * Browse
 * -------------------------------------------------------------------- */

int rznsq_position(rznsq_connection_t conn, const char *key_name,
                   const char *key_value,
                   const rznsq_position_options *options);

int rznsq_next_result(rznsq_connection_t conn, char *buf, size_t *buf_len);
int rznsq_close_result(rznsq_connection_t conn);

int rznsq_write_result(rznsq_connection_t conn, const char *doc,
                       size_t doc_len, const rznsq_write_options *options);
int rznsq_delete_result(rznsq_connection_t conn);
int rznsq_update_result(rznsq_connection_t conn, const char *doc,
                        size_t doc_len);

/* --------------------------------------------------------------------
 * Transaction stubs (no-ops)
 * -------------------------------------------------------------------- */

int rznsq_commit(rznsq_connection_t conn);
int rznsq_abort(rznsq_connection_t conn);

/* --------------------------------------------------------------------
 * Diagnostics
 * -------------------------------------------------------------------- */

const char *rznsq_last_result(rznsq_connection_t conn);

/* --------------------------------------------------------------------
 * EzNoSQL compatibility aliases (znsq_* → rznsq_*)
 *
 * These macros let callers use the standard EzNoSQL names.
 * Include reznosql.h instead of znsq.h and the same source
 * compiles against RezNoSQL without any code changes.
 * -------------------------------------------------------------------- */

/* Types */
#define znsq_connection_t       rznsq_connection_t
#define znsq_create_options     rznsq_create_options
#define znsq_create_index_options rznsq_create_index_options
#define znsq_add_index_options  rznsq_add_index_options
#define znsq_open_options       rznsq_open_options
#define znsq_write_options      rznsq_write_options
#define znsq_read_options       rznsq_read_options
#define znsq_update_options     rznsq_update_options
#define znsq_delete_options     rznsq_delete_options
#define znsq_position_options   rznsq_position_options
#define znsq_stats              rznsq_stats

/* Constants */
#define ZNSQ_KEY_MAX            RZNSQ_KEY_MAX
#define ZNSQ_KEY_DEFAULT        RZNSQ_KEY_DEFAULT
#define ZNSQ_KEY_MIN            RZNSQ_KEY_MIN
#define ZNSQ_DOCLEN_SIZE        RZNSQ_DOCLEN_SIZE
#define ZNSQ_MAX_DOC            RZNSQ_MAX_DOC
#define ZNSQ_MAX_INDEXES        RZNSQ_MAX_INDEXES
#define ZNSQ_MAX_ERRMSG         RZNSQ_MAX_ERRMSG
#define ZNSQ_AUTO_KEY_NAME      RZNSQ_AUTO_KEY_NAME
#define ZNSQ_AUTO_KEY_LEN       RZNSQ_AUTO_KEY_LEN
#define ZNSQ_HASHED_KEY_SIZE    RZNSQ_HASHED_KEY_SIZE
#define ZNSQ_FLAG_HASHED        RZNSQ_FLAG_HASHED
#define ZNSQ_REC_HEADER         RZNSQ_REC_HEADER
#define ZNSQ_MAX_REC            RZNSQ_MAX_REC

/* Return codes */
#define ZNSQ_RC_OK              RZNSQ_RC_OK
#define ZNSQ_RC_WARN            RZNSQ_RC_WARN
#define ZNSQ_RC_ERROR           RZNSQ_RC_ERROR
#define ZNSQ_RC_SEVERE          RZNSQ_RC_SEVERE

/* Reason codes */
#define ZNSQ_RSN_OK             RZNSQ_RSN_OK
#define ZNSQ_RSN_NOT_FOUND      RZNSQ_RSN_NOT_FOUND
#define ZNSQ_RSN_DUP_KEY        RZNSQ_RSN_DUP_KEY
#define ZNSQ_RSN_END_OF_DATA    RZNSQ_RSN_END_OF_DATA
#define ZNSQ_RSN_BUF_TOO_SMALL  RZNSQ_RSN_BUF_TOO_SMALL
#define ZNSQ_RSN_OPEN_FAILED    RZNSQ_RSN_OPEN_FAILED
#define ZNSQ_RSN_IDCAMS_FAILED  RZNSQ_RSN_IDCAMS_FAILED
#define ZNSQ_RSN_IO_ERROR       RZNSQ_RSN_IO_ERROR
#define ZNSQ_RSN_INVALID_PARAM  RZNSQ_RSN_INVALID_PARAM
#define ZNSQ_RSN_NOT_OPEN       RZNSQ_RSN_NOT_OPEN
#define ZNSQ_RSN_NO_BROWSE      RZNSQ_RSN_NO_BROWSE
#define ZNSQ_RSN_JSON_PARSE     RZNSQ_RSN_JSON_PARSE
#define ZNSQ_RSN_ALLOC_FAILED   RZNSQ_RSN_ALLOC_FAILED

/* Macros */
#define znsq_err                rznsq_err
#define ZNSQ_MAKE_RC            RZNSQ_MAKE_RC

/* Index flags */
#define ZNSQ_INDEX_UNIQUE       RZNSQ_INDEX_UNIQUE
#define ZNSQ_INDEX_NON_UNIQUE   RZNSQ_INDEX_NON_UNIQUE

/* Functions */
#define znsq_create             rznsq_create
#define znsq_destroy            rznsq_destroy
#define znsq_create_index       rznsq_create_index
#define znsq_add_index          rznsq_add_index
#define znsq_drop_index         rznsq_drop_index
#define znsq_report_stats       rznsq_report_stats
#define znsq_open               rznsq_open
#define znsq_close              rznsq_close
#define znsq_write              rznsq_write
#define znsq_read               rznsq_read
#define znsq_delete             rznsq_delete
#define znsq_update             rznsq_update
#define znsq_position           rznsq_position
#define znsq_next_result        rznsq_next_result
#define znsq_close_result       rznsq_close_result
#define znsq_write_result       rznsq_write_result
#define znsq_delete_result      rznsq_delete_result
#define znsq_update_result      rznsq_update_result
#define znsq_commit             rznsq_commit
#define znsq_abort              rznsq_abort
#define znsq_last_result        rznsq_last_result

#ifdef __cplusplus
}
#endif

#endif /* REZNOSQL_H */
