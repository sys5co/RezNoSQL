/*
 * search_indexed.c - RezNoSQL indexed search sample
 *
 * Demonstrates two search strategies:
 *   1. Direct lookup by alt key (dept index) — O(1) for single match
 *   2. Browse + filter — O(n) scan with app-level filtering
 *
 * Prerequisites:
 *   1. submit create_indexed.jcl
 *   2. make search_indexed
 *   3. ./search_indexed
 *   4. submit delete_indexed.jcl
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "reznosql.h"

#define DSN      "IBMUSER.SRCHI.DEMO"
#define AIX_DSN  "IBMUSER.SRCHI.DEPT"
#define KEY_SIZE 32

static const char *PK      = "\"_id\"";
static const char *ALT_KEY = "\"dept\"";

/* ------------------------------------------------------------------ */
/* Minimal JSON value extractor                                       */
/* ------------------------------------------------------------------ */
static int json_get(const char *json, const char *key,
                    char *out, size_t out_sz)
{
    const char *p = strstr(json, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == ':') p++;

    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < out_sz - 1)
            out[i++] = *p++;
        out[i] = '\0';
    } else {
        size_t i = 0;
        while (*p && *p != ',' && *p != '}' && i < out_sz - 1)
            out[i++] = *p++;
        out[i] = '\0';
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Browse all, apply filter callback                                   */
/* ------------------------------------------------------------------ */
typedef int (*filter_fn)(const char *doc, void *ctx);

static int browse_filter(rznsq_connection_t conn, filter_fn fn, void *ctx)
{
    rznsq_position_options popts;
    char buf[4096];
    size_t buf_len;
    int count = 0;

    memset(&popts, 0, sizeof(popts));
    popts.generic = 1;

    if (rznsq_position(conn, PK, "\"\"", &popts) != 0)
        return 0;

    while (1) {
        buf_len = sizeof(buf);
        if (rznsq_next_result(conn, buf, &buf_len) != 0)
            break;
        buf[buf_len] = '\0';
        if (fn(buf, ctx))
            count++;
    }
    rznsq_close_result(conn);
    return count;
}

/* ------------------------------------------------------------------ */
/* Filters                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *dept;
} dept_ctx;

static int filter_dept(const char *doc, void *ctx)
{
    dept_ctx *d = (dept_ctx *)ctx;
    char val[128];
    if (json_get(doc, "\"dept\"", val, sizeof(val))
        && strcmp(val, d->dept) == 0) {
        printf("  %s\n", doc);
        return 1;
    }
    return 0;
}

typedef struct {
    long min_salary;
    long max_salary;
} salary_ctx;

static int filter_salary(const char *doc, void *ctx)
{
    salary_ctx *s = (salary_ctx *)ctx;
    char val[32];
    if (json_get(doc, "\"salary\"", val, sizeof(val))) {
        long salary = atol(val);
        if (salary >= s->min_salary && salary <= s->max_salary) {
            printf("  %s\n", doc);
            return 1;
        }
    }
    return 0;
}

typedef struct {
    const char *dept;
    long min_salary;
    long max_salary;
} combined_ctx;

static int filter_combined(const char *doc, void *ctx)
{
    combined_ctx *c = (combined_ctx *)ctx;
    char dept_val[128], sal_val[32];

    if (!json_get(doc, "\"dept\"", dept_val, sizeof(dept_val)))
        return 0;
    if (strcmp(dept_val, c->dept) != 0)
        return 0;
    if (!json_get(doc, "\"salary\"", sal_val, sizeof(sal_val)))
        return 0;

    long salary = atol(sal_val);
    if (salary >= c->min_salary && salary <= c->max_salary) {
        printf("  %s\n", doc);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    int rc, i, count;
    rznsq_connection_t conn = NULL;
    rznsq_open_options oopts;
    rznsq_write_options wopts;
    rznsq_read_options ropts;
    char buf[4096];
    size_t buf_len;

    printf("=== RezNoSQL Indexed Search Sample ===\n\n");

    /* --- Open --- */
    memset(&oopts, 0, sizeof(oopts));
    oopts.primary_key = PK;
    oopts.key_size = KEY_SIZE;

    rc = rznsq_open(&conn, DSN, 0, &oopts);
    if (rc != 0) {
        printf("Open failed: rc=0x%08X\n", rc);
        printf("Did you run: submit create_indexed.jcl ?\n");
        return 8;
    }

    /* --- Activate dept index --- */
    {
        rznsq_add_index_options aiopts;
        memset(&aiopts, 0, sizeof(aiopts));
        aiopts.base_name = DSN;
        aiopts.aix_name  = AIX_DSN;
        aiopts.alt_key   = ALT_KEY;
        aiopts.flags     = RZNSQ_INDEX_NON_UNIQUE;

        rc = rznsq_add_index(conn, &aiopts);
        if (rc != 0) {
            printf("add_index failed: rc=0x%08X\n", rc);
            rznsq_close(conn);
            return 8;
        }
        printf("Activated dept index\n");
    }

    /* --- Load sample data --- */
    printf("\n--- Loading 10 employees ---\n");
    {
        const char *docs[] = {
            "{\"_id\":\"E001\",\"name\":\"Alice\",\"dept\":\"Engineering\",\"salary\":95000}",
            "{\"_id\":\"E002\",\"name\":\"Bob\",\"dept\":\"Marketing\",\"salary\":82000}",
            "{\"_id\":\"E003\",\"name\":\"Carol\",\"dept\":\"Engineering\",\"salary\":105000}",
            "{\"_id\":\"E004\",\"name\":\"Dave\",\"dept\":\"Sales\",\"salary\":78000}",
            "{\"_id\":\"E005\",\"name\":\"Eve\",\"dept\":\"Engineering\",\"salary\":112000}",
            "{\"_id\":\"E006\",\"name\":\"Frank\",\"dept\":\"Marketing\",\"salary\":91000}",
            "{\"_id\":\"E007\",\"name\":\"Grace\",\"dept\":\"Sales\",\"salary\":86000}",
            "{\"_id\":\"E008\",\"name\":\"Hank\",\"dept\":\"Engineering\",\"salary\":99000}",
            "{\"_id\":\"E009\",\"name\":\"Iris\",\"dept\":\"Sales\",\"salary\":103000}",
            "{\"_id\":\"E010\",\"name\":\"Jack\",\"dept\":\"Marketing\",\"salary\":76000}"
        };

        memset(&wopts, 0, sizeof(wopts));
        wopts.key_name = PK;

        for (i = 0; i < 10; i++) {
            rc = rznsq_write(conn, docs[i], strlen(docs[i]), &wopts);
            if (rc != 0)
                printf("  Write failed: rc=0x%08X\n", rc);
        }
        printf("  Loaded 10 employees\n");
    }

    /* ============================================================== */
    /* Strategy 1: Direct lookup via alt key index (O(1) per call)     */
    /* ============================================================== */
    printf("\n--- Direct lookup: dept = Engineering (via index) ---\n");
    printf("  rznsq_read with alt key returns first matching doc:\n");
    {
        memset(&ropts, 0, sizeof(ropts));
        buf_len = sizeof(buf);
        rc = rznsq_read(conn, ALT_KEY, "\"Engineering\"",
                        buf, &buf_len, 0, &ropts);
        if (rc == 0) {
            buf[buf_len] = '\0';
            printf("  %s\n", buf);
        }
    }

    printf("\n--- Direct lookup: dept = Sales (via index) ---\n");
    {
        memset(&ropts, 0, sizeof(ropts));
        buf_len = sizeof(buf);
        rc = rznsq_read(conn, ALT_KEY, "\"Sales\"",
                        buf, &buf_len, 0, &ropts);
        if (rc == 0) {
            buf[buf_len] = '\0';
            printf("  %s\n", buf);
        }
    }

    /* ============================================================== */
    /* Strategy 2: Browse + filter (scans all docs, filters in app)    */
    /* Best for: multi-match queries, range filters, combined criteria */
    /* ============================================================== */
    printf("\n--- Browse + filter: dept = Engineering ---\n");
    {
        dept_ctx ctx = { "Engineering" };
        count = browse_filter(conn, filter_dept, &ctx);
        printf("  Found: %d\n", count);
    }

    printf("\n--- Browse + filter: salary 80000-100000 ---\n");
    {
        salary_ctx ctx = { 80000, 100000 };
        count = browse_filter(conn, filter_salary, &ctx);
        printf("  Found: %d\n", count);
    }

    printf("\n--- Browse + filter: salary >= 100000 ---\n");
    {
        salary_ctx ctx = { 100000, 999999 };
        count = browse_filter(conn, filter_salary, &ctx);
        printf("  Found: %d\n", count);
    }

    printf("\n--- Browse + filter: Engineering AND salary >= 100000 ---\n");
    {
        combined_ctx ctx = { "Engineering", 100000, 999999 };
        count = browse_filter(conn, filter_combined, &ctx);
        printf("  Found: %d\n", count);
    }

    printf("\n--- Browse + filter: Marketing AND salary 75000-85000 ---\n");
    {
        combined_ctx ctx = { "Marketing", 75000, 85000 };
        count = browse_filter(conn, filter_combined, &ctx);
        printf("  Found: %d\n", count);
    }

    /* --- Close --- */
    rznsq_close(conn);
    printf("\nDone. Run: submit delete_indexed.jcl\n");

    return 0;
}
