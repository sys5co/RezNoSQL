/*
 * reznosql_sample.c - RezNoSQL API demo over VSAM KSDS
 *
 * Compile:
 *   xlc -qcpluscmt -qlanglvl=extc99 -I. -o reznosql_sample reznosql_sample.c reznosql.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reznosql.h"

#define BASE_DSN  "IBMUSER.RZNSQL2"
#define AIX_DSN   "IBMUSER.RZNSQL2.AIX1"

static const char *PRIMARY_KEY = "\"_id\"";
static const char *ALT_KEY     = "\"author\"";
#define KEY_SIZE 32

static void check_rc(const char *func, int rc, rznsq_connection_t conn)
{
    if (rc != 0) {
        printf("  %s failed: rc=0x%08X (reason=0x%04X)\n",
               func, rc, rznsq_err(rc));
        if (conn)
            printf("  Detail: %s\n", rznsq_last_result(conn));
    }
}

int main()
{
    int rc;
    rznsq_connection_t conn = NULL;

    printf("=== RezNoSQL Sample (VSAM KSDS) ===\n\n");

    /* 1. Clean up */
    printf("Cleaning up...\n");
    rznsq_destroy(AIX_DSN);
    rznsq_destroy(BASE_DSN);

    /* 2. Create primary KSDS */
    printf("\n--- Create Database ---\n");
    {
        rznsq_create_options copts;
        memset(&copts, 0, sizeof(copts));
        copts.max_space = 1;
        copts.primary_key = PRIMARY_KEY;
        copts.avg_doc_size = 256;
        copts.key_size = KEY_SIZE;

        rc = rznsq_create(BASE_DSN, &copts);
        check_rc("rznsq_create", rc, NULL);
        if (rc != 0) return 8;
        printf("Created: %s\n", BASE_DSN);
    }

    /* 3. Create secondary index */
    printf("\n--- Create Secondary Index ---\n");
    {
        rznsq_create_index_options iopts;
        memset(&iopts, 0, sizeof(iopts));
        iopts.base_name = BASE_DSN;
        iopts.aix_name = AIX_DSN;
        iopts.alt_key = ALT_KEY;
        iopts.flags = RZNSQ_INDEX_NON_UNIQUE;
        iopts.max_space = 1;
        iopts.key_size = KEY_SIZE;

        rc = rznsq_create_index(&iopts);
        check_rc("rznsq_create_index", rc, NULL);
        if (rc != 0) goto destroy;
        printf("Created index: %s\n", AIX_DSN);
    }

    /* 4. Open */
    printf("\n--- Open ---\n");
    {
        rznsq_open_options oopts;
        memset(&oopts, 0, sizeof(oopts));
        oopts.primary_key = PRIMARY_KEY;
        oopts.key_size = KEY_SIZE;

        rc = rznsq_open(&conn, BASE_DSN, 0, &oopts);
        check_rc("rznsq_open", rc, NULL);
        if (rc != 0) goto destroy;
        printf("Opened\n");
    }

    /* 5. Activate secondary index */
    printf("\n--- Activate Index ---\n");
    {
        rznsq_add_index_options aiopts;
        memset(&aiopts, 0, sizeof(aiopts));
        aiopts.base_name = BASE_DSN;
        aiopts.aix_name = AIX_DSN;
        aiopts.alt_key = ALT_KEY;
        aiopts.flags = RZNSQ_INDEX_NON_UNIQUE;

        rc = rznsq_add_index(conn, &aiopts);
        check_rc("rznsq_add_index", rc, conn);
        if (rc != 0) goto close_db;
        printf("Activated index\n");
    }

    /* 6. Write 3 documents */
    printf("\n--- Write ---\n");
    {
        const char *docs[] = {
            "{\"_id\":\"001\",\"title\":\"The Hobbit\","
            "\"author\":\"J.R.R. Tolkien\",\"year\":1937}",

            "{\"_id\":\"002\",\"title\":\"Foundation\","
            "\"author\":\"Isaac Asimov\",\"year\":1951}",

            "{\"_id\":\"003\",\"title\":\"The Silmarillion\","
            "\"author\":\"J.R.R. Tolkien\",\"year\":1977}"
        };
        rznsq_write_options wopts;
        memset(&wopts, 0, sizeof(wopts));
        wopts.key_name = PRIMARY_KEY;

        for (int i = 0; i < 3; i++) {
            rc = rznsq_write(conn, docs[i], strlen(docs[i]), &wopts);
            check_rc("rznsq_write", rc, conn);
            if (rc == 0) printf("Wrote: %.40s...\n", docs[i]);
        }
    }

    /* 7. Read by primary key */
    printf("\n--- Read by Primary Key ---\n");
    {
        char buf[4096];
        size_t buf_len = sizeof(buf);
        rznsq_read_options ropts;
        memset(&ropts, 0, sizeof(ropts));

        rc = rznsq_read(conn, PRIMARY_KEY, "\"002\"",
                        buf, &buf_len, 0, &ropts);
        check_rc("rznsq_read", rc, conn);
        if (rc == 0) {
            buf[buf_len] = '\0';
            printf("Doc 002: %s\n", buf);
        }
    }

    /* 8. Browse all */
    printf("\n--- Browse ---\n");
    {
        char buf[4096];
        size_t buf_len;
        rznsq_position_options popts;
        memset(&popts, 0, sizeof(popts));
        popts.key_name = PRIMARY_KEY;
        popts.generic = 1;

        rc = rznsq_position(conn, PRIMARY_KEY, "\"001\"", &popts);
        check_rc("rznsq_position", rc, conn);

        if (rc == 0) {
            int count = 0;
            while (1) {
                buf_len = sizeof(buf);
                rc = rznsq_next_result(conn, buf, &buf_len);
                if (rc != 0) {
                    if (rznsq_err(rc) == RZNSQ_RSN_END_OF_DATA)
                        printf("End of data\n");
                    else
                        check_rc("rznsq_next_result", rc, conn);
                    break;
                }
                buf[buf_len] = '\0';
                printf("  [%d] %s\n", ++count, buf);
            }
            rznsq_close_result(conn);
        }
    }

    /* 9. Update */
    printf("\n--- Update ---\n");
    {
        const char *updated =
            "{\"_id\":\"002\",\"title\":\"Foundation\","
            "\"author\":\"Isaac Asimov\",\"year\":1951,"
            "\"note\":\"Updated via RezNoSQL\"}";
        rznsq_update_options uopts;
        memset(&uopts, 0, sizeof(uopts));

        rc = rznsq_update(conn, PRIMARY_KEY, "\"002\"",
                          updated, strlen(updated), &uopts);
        check_rc("rznsq_update", rc, conn);
        if (rc == 0) printf("Updated 002\n");

        /* Verify */
        {
            char buf[4096];
            size_t buf_len = sizeof(buf);
            rznsq_read_options ropts;
            memset(&ropts, 0, sizeof(ropts));
            rc = rznsq_read(conn, PRIMARY_KEY, "\"002\"",
                            buf, &buf_len, 0, &ropts);
            if (rc == 0) {
                buf[buf_len] = '\0';
                printf("Verified: %s\n", buf);
            }
        }
    }

    /* 10. Delete */
    printf("\n--- Delete ---\n");
    {
        rznsq_delete_options dopts;
        memset(&dopts, 0, sizeof(dopts));

        rc = rznsq_delete(conn, PRIMARY_KEY, "\"003\"", &dopts);
        check_rc("rznsq_delete", rc, conn);
        if (rc == 0) printf("Deleted 003\n");
    }

    /* 11. Browse-result operations: write_result, update_result, delete_result */
    printf("\n--- Browse-Result Operations ---\n");
    {
        /* Re-add doc 003 via write_result during browse */
        rznsq_position_options popts;
        memset(&popts, 0, sizeof(popts));
        popts.generic = 1;

        rc = rznsq_position(conn, PRIMARY_KEY, "\"001\"", &popts);
        check_rc("rznsq_position", rc, conn);

        if (rc == 0) {
            /* Insert a new doc during browse */
            const char *new_doc =
                "{\"_id\":\"004\",\"title\":\"Rendezvous with Rama\","
                "\"author\":\"Arthur C. Clarke\",\"year\":1973}";
            rznsq_write_options wopts;
            memset(&wopts, 0, sizeof(wopts));
            wopts.key_name = PRIMARY_KEY;

            rc = rznsq_write_result(conn, new_doc, strlen(new_doc), &wopts);
            check_rc("rznsq_write_result", rc, conn);
            if (rc == 0) printf("write_result: inserted 004\n");

            rznsq_close_result(conn);
        }

        /* Browse to 002, then update_result */
        rc = rznsq_position(conn, PRIMARY_KEY, "\"002\"", NULL);
        if (rc == 0) {
            char buf[4096];
            size_t buf_len = sizeof(buf);

            rc = rznsq_next_result(conn, buf, &buf_len);
            if (rc == 0) {
                const char *upd =
                    "{\"_id\":\"002\",\"title\":\"Foundation\","
                    "\"author\":\"Isaac Asimov\",\"year\":1951,"
                    "\"note\":\"via update_result\"}";
                rc = rznsq_update_result(conn, upd, strlen(upd));
                check_rc("rznsq_update_result", rc, conn);
                if (rc == 0) printf("update_result: updated 002\n");
            }
            rznsq_close_result(conn);
        }

        /* Browse to 004, then delete_result */
        rc = rznsq_position(conn, PRIMARY_KEY, "\"004\"", NULL);
        if (rc == 0) {
            char buf[4096];
            size_t buf_len = sizeof(buf);

            rc = rznsq_next_result(conn, buf, &buf_len);
            if (rc == 0) {
                rc = rznsq_delete_result(conn);
                check_rc("rznsq_delete_result", rc, conn);
                if (rc == 0) printf("delete_result: deleted 004\n");
            }
            rznsq_close_result(conn);
        }

        /* Browse all to verify */
        printf("After browse-result ops:\n");
        memset(&popts, 0, sizeof(popts));
        popts.generic = 1;
        rc = rznsq_position(conn, PRIMARY_KEY, "\"001\"", &popts);
        if (rc == 0) {
            char buf[4096];
            size_t buf_len;
            int count = 0;
            while (1) {
                buf_len = sizeof(buf);
                rc = rznsq_next_result(conn, buf, &buf_len);
                if (rc != 0) {
                    if (rznsq_err(rc) == RZNSQ_RSN_END_OF_DATA)
                        printf("End of data\n");
                    break;
                }
                buf[buf_len] = '\0';
                printf("  [%d] %s\n", ++count, buf);
            }
            rznsq_close_result(conn);
        }
    }

    /* 12. Read by alternate key */
    printf("\n--- Read by Alternate Key ---\n");
    {
        char buf[4096];
        size_t buf_len = sizeof(buf);
        rznsq_read_options ropts;
        memset(&ropts, 0, sizeof(ropts));

        rc = rznsq_read(conn, ALT_KEY, "\"J.R.R. Tolkien\"",
                        buf, &buf_len, 0, &ropts);
        check_rc("rznsq_read (alt)", rc, conn);
        if (rc == 0) {
            buf[buf_len] = '\0';
            printf("By author: %s\n", buf);
        }
    }

    /* 12. Stats */
    printf("\n--- Stats ---\n");
    {
        rznsq_stats stats;
        rc = rznsq_report_stats(conn, &stats);
        if (rc == 0) {
            printf("Reads:   %lu\n", stats.reads);
            printf("Writes:  %lu\n", stats.writes);
            printf("Updates: %lu\n", stats.updates);
            printf("Deletes: %lu\n", stats.deletes);
            printf("Browses: %lu\n", stats.browses);
        }
    }

    /* 13. Drop index */
    printf("\n--- Drop Index ---\n");
    rc = rznsq_drop_index(conn, AIX_DSN);
    check_rc("rznsq_drop_index", rc, conn);
    if (rc == 0) printf("Dropped index\n");

    /* 14. Close + destroy */
close_db:
    printf("\n--- Close ---\n");
    rc = rznsq_close(conn);
    conn = NULL;
    check_rc("rznsq_close", rc, NULL);
    if (rc == 0) printf("Closed\n");

destroy:
    printf("\n--- Destroy ---\n");
    rznsq_destroy(AIX_DSN);
    rznsq_destroy(BASE_DSN);
    printf("Destroyed\n");

    /* ============================================================
     * Part 2: Auto-generated rznsq_id demo
     * ============================================================ */
    printf("\n\n=== Auto-Key (rznsq_id) Demo ===\n\n");

#define AUTO_DSN "IBMUSER.RZNSQL3"

    /* Create with default 251-byte keys */
    {
        rznsq_create_options copts;
        memset(&copts, 0, sizeof(copts));
        copts.max_space = 1;
        /* primary_key = NULL -> auto rznsq_id */
        /* key_size = 0 -> default 251 */

        rznsq_destroy(AUTO_DSN);
        rc = rznsq_create(AUTO_DSN, &copts);
        check_rc("rznsq_create (auto)", rc, NULL);
        if (rc != 0) goto done;
        printf("Created: %s (key_size=251, auto rznsq_id)\n", AUTO_DSN);
    }

    /* Open with auto-key mode (primary_key = NULL) */
    {
        rznsq_open_options oopts;
        memset(&oopts, 0, sizeof(oopts));
        /* primary_key = NULL -> auto rznsq_id */
        /* key_size = 0 -> default 251 */

        rc = rznsq_open(&conn, AUTO_DSN, 0, &oopts);
        check_rc("rznsq_open (auto)", rc, NULL);
        if (rc != 0) goto destroy_auto;
        printf("Opened (auto-key mode)\n");
    }

    /* Write docs without _id - engine generates rznsq_id */
    printf("\n--- Write (auto-key) ---\n");
    {
        const char *docs[] = {
            "{\"title\":\"Neuromancer\",\"author\":\"William Gibson\"}",
            "{\"title\":\"Snow Crash\",\"author\":\"Neal Stephenson\"}",
            "{\"title\":\"Dune\",\"author\":\"Frank Herbert\"}"
        };

        for (int i = 0; i < 3; i++) {
            rc = rznsq_write(conn, docs[i], strlen(docs[i]), NULL);
            check_rc("rznsq_write (auto)", rc, conn);
            if (rc == 0)
                printf("Wrote: %s\n", docs[i]);
        }
    }

    /* Browse to see auto-generated keys */
    printf("\n--- Browse (auto-key) ---\n");
    {
        char buf[4096];
        size_t buf_len;
        rznsq_position_options popts;
        memset(&popts, 0, sizeof(popts));
        popts.generic = 1;

        rc = rznsq_position(conn, NULL, "\"\"", &popts);
        check_rc("rznsq_position (auto)", rc, conn);

        if (rc == 0) {
            int count = 0;
            while (1) {
                buf_len = sizeof(buf);
                rc = rznsq_next_result(conn, buf, &buf_len);
                if (rc != 0) {
                    if (rznsq_err(rc) == RZNSQ_RSN_END_OF_DATA)
                        printf("End of data\n");
                    else
                        check_rc("rznsq_next_result", rc, conn);
                    break;
                }
                buf[buf_len] = '\0';
                printf("  [%d] %s\n", ++count, buf);
            }
            rznsq_close_result(conn);
        }
    }

    /* Stats */
    printf("\n--- Stats (auto-key) ---\n");
    {
        rznsq_stats stats;
        rc = rznsq_report_stats(conn, &stats);
        if (rc == 0) {
            printf("Reads:   %lu\n", stats.reads);
            printf("Writes:  %lu\n", stats.writes);
            printf("Browses: %lu\n", stats.browses);
        }
    }

    /* Close */
    rznsq_close(conn);
    conn = NULL;
    printf("\nClosed\n");

destroy_auto:
    rznsq_destroy(AUTO_DSN);
    printf("Destroyed %s\n", AUTO_DSN);

done:
    printf("\n=== Done ===\n");
    return 0;
}
