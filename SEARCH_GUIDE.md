# RezNoSQL Search Guide

RezNoSQL stores JSON documents in VSAM KSDS. Since VSAM has no built-in query engine, searching is done through key-based access and application-level filtering. This guide covers the available strategies and when to use each.

## Search Strategies

| Strategy | How | Performance | Best for |
|---|---|---|---|
| **Primary key lookup** | `rznsq_read()` by key | O(log n) | Fetch by known ID |
| **Alt key lookup** | `rznsq_read()` with secondary index | O(log n) | Fetch by indexed field |
| **Browse + filter** | `rznsq_position()` + `rznsq_next_result()` + app filter | O(n) | Multi-field queries, ranges |
| **Index + filter** | Alt key lookup to narrow, then filter in app | O(k) + filter | Index on selective field, filter the rest |

## Strategy 1: Primary Key Lookup

Direct read by the document's primary key. Fastest possible — VSAM index tree traversal.

```c
rznsq_read_options ropts = {0};
char buf[4096];
size_t len = sizeof(buf);

rc = rznsq_read(conn, "\"_id\"", "\"emp001\"", buf, &len, 0, &ropts);
if (rc == 0) {
    buf[len] = '\0';
    printf("%s\n", buf);
}
```

**When to use**: You know the exact key. This is O(log n) via VSAM index.

## Strategy 2: Secondary Index Lookup

Create a secondary index on a frequently-searched field (e.g. `dept`). The index maps alt key values to primary keys.

### Setup

```c
/* Create the AIX dataset (once, at setup time) */
rznsq_create_index_options ciopts = {0};
ciopts.base_name = "IBMUSER.MY.DATA";
ciopts.aix_name  = "IBMUSER.MY.DEPT";
ciopts.alt_key   = "\"dept\"";
ciopts.flags     = RZNSQ_INDEX_NON_UNIQUE;  /* multiple docs per dept */
ciopts.max_space  = 1;
rznsq_create_index(&ciopts);

/* Activate at open time */
rznsq_add_index_options aiopts = {0};
aiopts.base_name = "IBMUSER.MY.DATA";
aiopts.aix_name  = "IBMUSER.MY.DEPT";
aiopts.alt_key   = "\"dept\"";
aiopts.flags     = RZNSQ_INDEX_NON_UNIQUE;
rznsq_add_index(conn, &aiopts);
```

### Query

```c
/* Direct lookup: returns first matching doc */
rc = rznsq_read(conn, "\"dept\"", "\"Engineering\"", buf, &len, 0, &ropts);
```

**When to use**: You need to look up documents by a non-primary field. O(log n) per lookup. For non-unique indexes, `rznsq_read` returns the first match.

**Limitation**: The current browse (`rznsq_position` / `rznsq_next_result`) operates on the primary KSDS only. To find all documents matching an alt key, combine the index lookup with browse + filter (Strategy 3).

## Strategy 3: Browse + Filter

Scan all documents sequentially and apply filter logic in your application. This is the most flexible approach — any field, any condition, any combination.

```c
rznsq_position_options popts = {0};
popts.generic = 1;  /* start from beginning */

rc = rznsq_position(conn, "\"_id\"", "\"\"", &popts);

while (rznsq_next_result(conn, buf, &len) == 0) {
    buf[len] = '\0';

    /* Apply your filter logic here */
    if (matches_criteria(buf)) {
        printf("%s\n", buf);
    }

    len = sizeof(buf);
}
rznsq_close_result(conn);
```

### Example: Filter by department

```c
int filter_dept(const char *doc, const char *dept) {
    /* Find "dept":"<value>" in the JSON */
    char *p = strstr(doc, "\"dept\"");
    if (!p) return 0;
    return strstr(p, dept) != NULL;
}

/* In browse loop: */
if (filter_dept(buf, "Engineering"))
    printf("%s\n", buf);
```

### Example: Filter by salary range

```c
int filter_salary(const char *doc, long min, long max) {
    char *p = strstr(doc, "\"salary\":");
    if (!p) return 0;
    long salary = atol(p + 9);  /* skip "salary": */
    return salary >= min && salary <= max;
}

/* Find employees earning 80K-100K */
if (filter_salary(buf, 80000, 100000))
    printf("%s\n", buf);
```

### Example: Combined filter

```c
/* Engineering AND salary >= 100K */
if (filter_dept(buf, "Engineering") && filter_salary(buf, 100000, 999999))
    printf("%s\n", buf);
```

**When to use**: Multi-field queries, range queries, complex conditions. O(n) — reads every document. Fine for small-to-medium datasets (thousands of records). For large datasets, use a secondary index to narrow first.

## Strategy 4: Index + Filter (Best of Both)

Use a secondary index on the most selective field to narrow results, then apply additional filters in app code.

```c
/* Dept index narrows to ~25% of docs, then filter salary in app */

/* 1. Alt key read gets first Engineering doc */
rc = rznsq_read(conn, "\"dept\"", "\"Engineering\"", buf, &len, 0, &ropts);

/* 2. Browse all docs, but only process Engineering ones */
rznsq_position_options popts = {0};
popts.generic = 1;
rznsq_position(conn, "\"_id\"", "\"\"", &popts);

while (rznsq_next_result(conn, buf, &len) == 0) {
    buf[len] = '\0';

    if (filter_dept(buf, "Engineering") &&
        filter_salary(buf, 100000, 999999)) {
        printf("%s\n", buf);
    }

    len = sizeof(buf);
}
rznsq_close_result(conn);
```

**When to use**: Large datasets where one field is highly selective. The index helps verify existence quickly; the browse handles multi-match enumeration.

## Hashed Key Mode

When using hashed keys (`RZNSQ_FLAG_HASHED`), the primary key is hashed into a 128-byte VSAM key. This gives O(1)-like lookup performance but browse returns documents in hash order (not sorted).

```c
/* Open in hashed mode */
rznsq_open_options oopts = {0};
oopts.primary_key = "\"_id\"";
oopts.flags = RZNSQ_FLAG_HASHED;
rznsq_open(&conn, dsname, 0, &oopts);

/* Read works the same — key is hashed internally */
rznsq_read(conn, "\"_id\"", "\"emp001\"", buf, &len, 0, &ropts);

/* Browse returns hash order (not key order) */
```

| Feature | Ordered (default) | Hashed |
|---|---|---|
| Read by key | O(log n) | O(1)-like |
| Range browse | Sorted order | Not meaningful |
| Browse all | Sorted by key | Hash order (random) |
| Key size | 4-251 bytes | 128 bytes (fixed) |

**When to use hashed**: High-volume exact key lookups where you don't need sorted browse. Don't use if you need range queries or sorted iteration.

## Performance Guidelines

| Dataset size | Recommended approach |
|---|---|
| < 100 docs | Browse + filter (simple, no index overhead) |
| 100 - 10,000 docs | Secondary index on most-queried field + browse filter |
| > 10,000 docs | Secondary indexes + hashed keys for frequent lookups |

### Tips

- **Index the most selective field** — if you filter by dept (3 values) and salary (many values), index dept to cut 67% of records, filter salary in app.
- **Hashed mode for key-heavy workloads** — if 90% of operations are read-by-key, use hashed mode for faster lookups.
- **Keep documents small** — smaller records mean faster browse scans. VSAM reads full records.
- **Pre-allocate enough space** — VSAM KSDS performance degrades with CI/CA splits. Size your MEGABYTES appropriately.

## Sample Programs

Working examples are in the `samples/` directory:

| File | Description |
|---|---|
| [`search.c`](samples/search.c) | Browse + filter by dept, salary, combined |
| [`search_indexed.c`](samples/search_indexed.c) | Secondary index lookup + browse filter |
| [`test_hashed.c`](samples/test_hashed.c) | Hashed key mode CRUD and browse |

Each sample has matching JCL files to create/delete the required datasets. See the `samples/Makefile` for build instructions.
