# RezNoSQL

A lightweight JSON document store for z/OS, built on plain VSAM KSDS.

RezNoSQL provides an API compatible with IBM's [EzNoSQL](https://www.ibm.com/docs/en/zos/3.1.0?topic=guide-eznosql-c-api-reference) (`znsq_*`) but **does not require Parallel Sysplex, Coupling Facility, or VSAM RLS**. It works on any z/OS system with standard VSAM.

## Why RezNoSQL?

EzNoSQL requires infrastructure that many z/OS installations don't have:

| Requirement | EzNoSQL | RezNoSQL |
|---|---|---|
| Parallel Sysplex | Yes | **No** |
| Coupling Facility | Yes | **No** |
| VSAM RLS (RLSINIT=YES) | Yes | **No** |
| DATABASE(JSON) attribute | Yes | **No** |

RezNoSQL uses standard VSAM KSDS with the z/OS C runtime (`fopen`, `flocate`, `fread`, `fwrite`, `fdelrec`) and IDCAMS for dataset management. It runs on any z/OS system.

## Features

- **22 API functions** mirroring EzNoSQL's interface
- **CRUD operations**: write, read, update, delete JSON documents
- **Sequential browse**: position, next_result, close_result
- **Browse-context operations**: write_result, update_result, delete_result
- **Secondary indexes**: unique and non-unique, with composite keys
- **Auto-generated keys**: timestamp-based 32-char hex IDs when no primary key is specified
- **Configurable key size**: 4-251 bytes (default 251, matching EzNoSQL)
- **EzNoSQL compatibility aliases**: `#include "reznosql.h"` and use `znsq_*` names unchanged
- **Shared and static library**: builds as `libreznosql.so` (DLL) or `libreznosql.a`
- **Single-file implementation**: ~550 lines of C, no external dependencies

## Requirements

- **z/OS V1.9 or later** (V1.9+ for C99 support in XL C/C++; tested on V2.4)
- IBM XL C/C++ compiler (`xlc`) — compiled with extended C99 (`-qlanglvl=extc99`) for z/OS VSAM extensions
- IDCAMS (standard z/OS utility)
- z/OS UNIX System Services (USS)

## Build

```sh
# Shared library + demo (default)
make

# Static library
make static

# Demo linked statically
make demo_static

# Full sample (self-contained test)
make reznosql_sample

# Clean
make clean
```

Build outputs:
| File | Description |
|---|---|
| `libreznosql.so` | Shared library (DLL) |
| `libreznosql.x` | Side deck for linking |
| `libreznosql.a` | Static library |
| `demo` | Demo program (shared link) |

## Quick Start

### 1. Create the VSAM dataset

Submit `create.jcl` to define a KSDS cluster (edit the dataset name and space as needed):

```
submit create.jcl
```

### 2. Build and run the demo

```sh
make
LIBPATH=. ./demo
```

Output:
```
=== RezNoSQL Demo ===

Opened IBMUSER.REZNOSQL.DEMO

--- Write ---
  Wrote: {"_id":"emp001","name":"Alice","dept":"Engineering","salary":95000}
  Wrote: {"_id":"emp002","name":"Bob","dept":"Marketing","salary":82000}
  ...

--- Read ---
  {"_id":"emp001","name":"Alice","dept":"Engineering","salary":95000}
  ...

--- Update ---
  Updated emp002
  Verify: {"_id":"emp002","name":"Bob","dept":"Engineering","salary":88000,"note":"Transferred from Marketing"}

--- Browse All ---
  [1] {"_id":"emp001","name":"Alice","dept":"Engineering","salary":95000}
  [2] {"_id":"emp002","name":"Bob","dept":"Engineering","salary":88000,"note":"Transferred from Marketing"}
  ...
  Total: 5 documents
```

### 3. Clean up

```
submit delete.jcl
```

## API Overview

```c
#include "reznosql.h"

rznsq_connection_t conn = NULL;
rznsq_open_options oopts = {0};
oopts.primary_key = "\"_id\"";
oopts.key_size = 32;

/* Open */
rznsq_open(&conn, "IBMUSER.MY.DATA", 0, &oopts);

/* Write */
rznsq_write_options wopts = {0};
wopts.key_name = "\"_id\"";
const char *doc = "{\"_id\":\"k1\",\"msg\":\"hello\"}";
rznsq_write(conn, doc, strlen(doc), &wopts);

/* Read */
char buf[4096];
size_t len = sizeof(buf);
rznsq_read_options ropts = {0};
rznsq_read(conn, "\"_id\"", "\"k1\"", buf, &len, 0, &ropts);

/* Update */
rznsq_update_options uopts = {0};
const char *newdoc = "{\"_id\":\"k1\",\"msg\":\"updated\"}";
rznsq_update(conn, "\"_id\"", "\"k1\"", newdoc, strlen(newdoc), &uopts);

/* Delete */
rznsq_delete_options dopts = {0};
rznsq_delete(conn, "\"_id\"", "\"k1\"", &dopts);

/* Browse */
rznsq_position_options popts = {0};
popts.generic = 1;
rznsq_position(conn, "\"_id\"", "\"\"", &popts);
while (rznsq_next_result(conn, buf, &len) == 0) {
    buf[len] = '\0';
    printf("%s\n", buf);
    len = sizeof(buf);
}
rznsq_close_result(conn);

/* Close */
rznsq_close(conn);
```

## EzNoSQL Compatibility

RezNoSQL includes `znsq_*` preprocessor aliases for all types, constants, and functions. Code written for EzNoSQL can compile against RezNoSQL by replacing `#include <znsq.h>` with `#include "reznosql.h"`:

```c
#include "reznosql.h"

/* Use standard EzNoSQL names — they map to rznsq_* */
znsq_connection_t conn;
znsq_open(&conn, dsname, 0, &opts);
znsq_write(conn, doc, len, &wopts);
znsq_read(conn, key, val, buf, &blen, 0, &ropts);
znsq_close(conn);
```

See [COMPARISON.md](COMPARISON.md) for a detailed feature-by-feature comparison and [SEARCH_GUIDE.md](SEARCH_GUIDE.md) for search strategies and performance guidelines.

## API Reference

### Data Management

| Function | Description |
|---|---|
| `rznsq_create(dsname, options)` | Create VSAM KSDS dataset via IDCAMS |
| `rznsq_destroy(dsname)` | Delete dataset via IDCAMS |
| `rznsq_create_index(options)` | Create secondary index (AIX KSDS) |
| `rznsq_add_index(conn, options)` | Activate secondary index |
| `rznsq_drop_index(conn, aix_name)` | Deactivate secondary index |
| `rznsq_report_stats(conn, stats)` | Get operation counters |

### Connection

| Function | Description |
|---|---|
| `rznsq_open(conn, dsname, flags, options)` | Open dataset connection |
| `rznsq_close(conn)` | Close connection and all indexes |

### Document CRUD

| Function | Description |
|---|---|
| `rznsq_write(conn, doc, len, options)` | Insert JSON document |
| `rznsq_read(conn, key_name, key_value, buf, len, flags, options)` | Read by primary or alternate key |
| `rznsq_update(conn, key_name, key_value, doc, len, options)` | Replace document |
| `rznsq_delete(conn, key_name, key_value, options)` | Delete document |

### Browse

| Function | Description |
|---|---|
| `rznsq_position(conn, key_name, key_value, options)` | Start sequential browse |
| `rznsq_next_result(conn, buf, len)` | Read next document |
| `rznsq_close_result(conn)` | End browse |
| `rznsq_write_result(conn, doc, len, options)` | Insert during browse |
| `rznsq_delete_result(conn)` | Delete last-read document |
| `rznsq_update_result(conn, doc, len)` | Update last-read document |

### Transaction Stubs

| Function | Description |
|---|---|
| `rznsq_commit(conn)` | No-op (returns 0) |
| `rznsq_abort(conn)` | No-op (returns 0) |

### Diagnostics

| Function | Description |
|---|---|
| `rznsq_last_result(conn)` | Last error detail string |
| `rznsq_err(rc)` | Extract reason code from return code |

## Record Layout

```
+-------------------+----------+---------------------------+
| Key (key_size)    | DocLen(4)| JSON Document (variable)  |
+-------------------+----------+---------------------------+
```

- **Key**: null-padded to `key_size` bytes (default 251)
- **DocLen**: 4-byte big-endian unsigned int
- **JSON**: UTF-8 document, up to 32 KB

## Files

| File | Description |
|---|---|
| `reznosql.h` | Public API header |
| `reznosql.c` | Implementation |
| `Makefile` | Build rules (shared/static library, demo) |
| `demo.c` | Simple CRUD demo |
| `create.jcl` | JCL to create demo dataset |
| `delete.jcl` | JCL to delete demo dataset |
| `reznosql_sample.c` | Comprehensive test (indexes, auto-keys, browse ops) |
| `COMPARISON.md` | EzNoSQL vs RezNoSQL comparison |
| `SEARCH_GUIDE.md` | Search strategies and performance guidelines |
| `samples/` | Search sample programs (browse+filter, indexed, hashed) |

## Using RezNoSQL in Your Program

Copy `reznosql.h` and `reznosql.c` (or the built library) to your project.

### Link with shared library

```sh
# Compile your program
xlc -qlanglvl=extc99 -qDLL -o myapp myapp.c libreznosql.x

# Run (set LIBPATH so the DLL is found)
LIBPATH=/path/to/reznosql ./myapp
```

### Link with static library

```sh
xlc -qlanglvl=extc99 -o myapp myapp.c libreznosql.a
```

### Compile directly (no library needed)

```sh
xlc -qlanglvl=extc99 -o myapp myapp.c reznosql.c
```
