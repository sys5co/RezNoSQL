/*
 * demo.c - Simple RezNoSQL demo
 *
 * Prerequisites:
 *   1. Run create.jcl to define IBMUSER.REZNOSQL.DEMO
 *   2. Compile: xlc -qcpluscmt -qlanglvl=extc99 -I. -o demo demo.c reznosql.c
 *   3. Run: ./demo
 *   4. When done: run delete.jcl to clean up
 */

#include <stdio.h>
#include <string.h>
#include "reznosql.h"

#define DSN "IBMUSER.REZNOSQL.DEMO"
#define KEY_SIZE 32

static const char *PK = "\"_id\"";

int main()
{
    int rc;
    rznsq_connection_t conn = NULL;
    rznsq_open_options oopts;
    rznsq_write_options wopts;
    rznsq_read_options ropts;
    rznsq_update_options uopts;
    char buf[4096];
    size_t buf_len;

    printf("=== RezNoSQL Demo ===\n\n");

    /* Open */
    memset(&oopts, 0, sizeof(oopts));
    oopts.primary_key = PK;
    oopts.key_size = KEY_SIZE;

    rc = rznsq_open(&conn, DSN, 0, &oopts);
    if (rc != 0) {
        printf("Open failed: rc=0x%08X\n", rc);
        printf("Did you run create.jcl first?\n");
        return 8;
    }
    printf("Opened %s\n\n", DSN);

    /* Write */
    printf("--- Write ---\n");
    {
        const char *docs[] = {
            "{\"_id\":\"emp001\",\"name\":\"Alice\",\"dept\":\"Engineering\",\"salary\":95000}",
            "{\"_id\":\"emp002\",\"name\":\"Bob\",\"dept\":\"Marketing\",\"salary\":82000}",
            "{\"_id\":\"emp003\",\"name\":\"Carol\",\"dept\":\"Engineering\",\"salary\":105000}",
            "{\"_id\":\"emp004\",\"name\":\"Dave\",\"dept\":\"Sales\",\"salary\":78000}",
            "{\"_id\":\"emp005\",\"name\":\"Eve\",\"dept\":\"Engineering\",\"salary\":112000}"
        };
        int i;

        memset(&wopts, 0, sizeof(wopts));
        wopts.key_name = PK;

        for (i = 0; i < 5; i++) {
            rc = rznsq_write(conn, docs[i], strlen(docs[i]), &wopts);
            if (rc == 0)
                printf("  Wrote: %s\n", docs[i]);
            else
                printf("  Write failed: rc=0x%08X %s\n", rc,
                       rznsq_last_result(conn));
        }
    }

    /* Read */
    printf("\n--- Read ---\n");
    {
        const char *keys[] = {"\"emp001\"", "\"emp003\"", "\"emp005\""};
        int i;

        memset(&ropts, 0, sizeof(ropts));
        for (i = 0; i < 3; i++) {
            buf_len = sizeof(buf);
            rc = rznsq_read(conn, PK, keys[i], buf, &buf_len, 0, &ropts);
            if (rc == 0) {
                buf[buf_len] = '\0';
                printf("  %s\n", buf);
            } else {
                printf("  Read %s failed: rc=0x%08X\n", keys[i], rc);
            }
        }
    }

    /* Update */
    printf("\n--- Update ---\n");
    {
        const char *updated =
            "{\"_id\":\"emp002\",\"name\":\"Bob\","
            "\"dept\":\"Engineering\",\"salary\":88000,"
            "\"note\":\"Transferred from Marketing\"}";

        memset(&uopts, 0, sizeof(uopts));
        rc = rznsq_update(conn, PK, "\"emp002\"",
                          updated, strlen(updated), &uopts);
        if (rc == 0)
            printf("  Updated emp002\n");
        else
            printf("  Update failed: rc=0x%08X\n", rc);

        /* Verify */
        buf_len = sizeof(buf);
        memset(&ropts, 0, sizeof(ropts));
        rc = rznsq_read(conn, PK, "\"emp002\"", buf, &buf_len, 0, &ropts);
        if (rc == 0) {
            buf[buf_len] = '\0';
            printf("  Verify: %s\n", buf);
        }
    }

    /* Browse all */
    printf("\n--- Browse All ---\n");
    {
        rznsq_position_options popts;
        int count = 0;

        memset(&popts, 0, sizeof(popts));
        popts.generic = 1;

        rc = rznsq_position(conn, PK, "\"\"", &popts);
        if (rc == 0) {
            while (1) {
                buf_len = sizeof(buf);
                rc = rznsq_next_result(conn, buf, &buf_len);
                if (rc != 0) break;
                buf[buf_len] = '\0';
                printf("  [%d] %s\n", ++count, buf);
            }
            rznsq_close_result(conn);
        }
        printf("  Total: %d documents\n", count);
    }

    /* Stats */
    printf("\n--- Stats ---\n");
    {
        rznsq_stats stats;
        rc = rznsq_report_stats(conn, &stats);
        if (rc == 0) {
            printf("  Reads:   %lu\n", stats.reads);
            printf("  Writes:  %lu\n", stats.writes);
            printf("  Updates: %lu\n", stats.updates);
            printf("  Browses: %lu\n", stats.browses);
        }
    }

    /* Close */
    rznsq_close(conn);
    printf("\nClosed. Run delete.jcl to clean up.\n");

    return 0;
}
