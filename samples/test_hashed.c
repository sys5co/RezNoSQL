/*
 * test_hashed.c - Test RezNoSQL hashed (unordered) key mode
 *
 * Creates a KSDS with 128-byte hashed keys, writes docs,
 * reads back by original key (hashed internally), and browses.
 *
 * Prerequisites:
 *   submit create_hashed.jcl
 */

#include <stdio.h>
#include <string.h>
#include "reznosql.h"

#define DSN      "IBMUSER.HASH.TEST"
#define KEY_SIZE 128  /* ignored when RZNSQ_FLAG_HASHED is set */

static const char *PK = "\"_id\"";

int main(void)
{
    int rc, i;
    rznsq_connection_t conn = NULL;
    rznsq_open_options oopts;
    rznsq_write_options wopts;
    rznsq_read_options ropts;
    char buf[4096];
    size_t buf_len;

    printf("=== RezNoSQL Hashed Key Test ===\n\n");

    /* Open in hashed mode */
    memset(&oopts, 0, sizeof(oopts));
    oopts.primary_key = PK;
    oopts.flags = RZNSQ_FLAG_HASHED;

    rc = rznsq_open(&conn, DSN, 0, &oopts);
    if (rc != 0) {
        printf("Open failed: rc=0x%08X\n", rc);
        printf("Did you run: submit create_hashed.jcl ?\n");
        return 8;
    }
    printf("Opened %s (hashed mode, key_size=128)\n\n", DSN);

    /* Write documents */
    printf("--- Write ---\n");
    {
        const char *docs[] = {
            "{\"_id\":\"emp001\",\"name\":\"Alice\",\"dept\":\"Engineering\"}",
            "{\"_id\":\"emp002\",\"name\":\"Bob\",\"dept\":\"Marketing\"}",
            "{\"_id\":\"emp003\",\"name\":\"Carol\",\"dept\":\"Engineering\"}",
            "{\"_id\":\"emp004\",\"name\":\"Dave\",\"dept\":\"Sales\"}",
            "{\"_id\":\"emp005\",\"name\":\"Eve\",\"dept\":\"Engineering\"}"
        };

        memset(&wopts, 0, sizeof(wopts));
        wopts.key_name = PK;

        for (i = 0; i < 5; i++) {
            rc = rznsq_write(conn, docs[i], strlen(docs[i]), &wopts);
            if (rc == 0)
                printf("  Wrote: %s\n", docs[i]);
            else
                printf("  Write FAILED: rc=0x%08X %s\n", rc,
                       rznsq_last_result(conn));
        }
    }

    /* Read back by original key (library hashes internally) */
    printf("\n--- Read by key (hashed lookup) ---\n");
    {
        const char *keys[] = {"\"emp001\"", "\"emp003\"", "\"emp005\""};
        memset(&ropts, 0, sizeof(ropts));

        for (i = 0; i < 3; i++) {
            buf_len = sizeof(buf);
            rc = rznsq_read(conn, PK, keys[i], buf, &buf_len, 0, &ropts);
            if (rc == 0) {
                buf[buf_len] = '\0';
                printf("  %s -> %s\n", keys[i], buf);
            } else {
                printf("  %s -> FAILED rc=0x%08X\n", keys[i], rc);
            }
        }
    }

    /* Read non-existent key */
    printf("\n--- Read non-existent key ---\n");
    {
        memset(&ropts, 0, sizeof(ropts));
        buf_len = sizeof(buf);
        rc = rznsq_read(conn, PK, "\"nosuchkey\"", buf, &buf_len, 0, &ropts);
        printf("  \"nosuchkey\" -> rc=0x%08X (expected NOT_FOUND)\n", rc);
    }

    /* Update */
    printf("\n--- Update ---\n");
    {
        const char *updated =
            "{\"_id\":\"emp002\",\"name\":\"Bob\",\"dept\":\"Engineering\","
            "\"note\":\"Transferred\"}";
        rznsq_update_options uopts;
        memset(&uopts, 0, sizeof(uopts));

        rc = rznsq_update(conn, PK, "\"emp002\"",
                          updated, strlen(updated), &uopts);
        if (rc == 0) {
            printf("  Updated emp002\n");
            buf_len = sizeof(buf);
            memset(&ropts, 0, sizeof(ropts));
            rc = rznsq_read(conn, PK, "\"emp002\"", buf, &buf_len, 0, &ropts);
            if (rc == 0) {
                buf[buf_len] = '\0';
                printf("  Verify: %s\n", buf);
            }
        } else {
            printf("  Update FAILED: rc=0x%08X\n", rc);
        }
    }

    /* Delete */
    printf("\n--- Delete ---\n");
    {
        rznsq_delete_options dopts;
        memset(&dopts, 0, sizeof(dopts));
        rc = rznsq_delete(conn, PK, "\"emp004\"", &dopts);
        if (rc == 0)
            printf("  Deleted emp004\n");
        else
            printf("  Delete FAILED: rc=0x%08X\n", rc);
    }

    /* Browse all (returns hash order, not key order) */
    printf("\n--- Browse (hash order, not sorted) ---\n");
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
        printf("  Total: %d (expected 4 after delete)\n", count);
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
            printf("  Deletes: %lu\n", stats.deletes);
            printf("  Browses: %lu\n", stats.browses);
        }
    }

    rznsq_close(conn);
    printf("\nDone. Run: submit delete_hashed.jcl\n");
    return 0;
}
