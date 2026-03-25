# EzNoSQL vs RezNoSQL-KSDS Comparison

## What's the Same

| Feature | EzNoSQL (`znsq_*`) | RezNoSQL-KSDS (`rznsq_*`) | Same? |
|---|---|---|---|
| **API prefix** | `znsq_` | `rznsq_` | Similar |
| **VSAM backend** | KSDS with DATABASE(JSON) | Plain KSDS | Same base type |
| **JSON document format** | UTF-8 JSON | UTF-8 JSON | Same |
| **Dataset naming** | 1-44 EBCDIC chars | 1-44 EBCDIC chars | Same |
| **Connection handle** | `znsq_connection_t` (opaque `void*`) | `rznsq_connection_t` (opaque `void*`) | Same |
| **Return code structure** | 4-byte: severity in high bits, reason in low bits | 4-byte: severity in high bits, reason in low bits | Same |
| **RC severity levels** | 0/4/8/12 | 0x0000/0x0004/0x0008/0x000C (shifted) | Same concept |
| **`err()` macro** | `znsq_err(rc)` extracts reason from RC | `rznsq_err(rc)` extracts reason from RC | Same |
| **Auto-key name** | `"znsq_id"` | `"rznsq_id"` | Same concept |
| **Auto-key injection** | Prepends `"znsq_id":"<val>"` to JSON on write | Prepends `"rznsq_id":"<val>"` to JSON on write | Same |
| **Auto-key trigger** | `primary_key = NULL` at create | `primary_key = NULL` at open | Same |
| **Ordered key limit** | 251 bytes | 251 bytes (default) | Same |
| **Key size configurable** | No (fixed 251 ordered, 128 hashed) | Yes (4-251, default 251) | RezNoSQL more flexible |

### Data Management APIs

| Feature | EzNoSQL (`znsq_*`) | RezNoSQL-KSDS (`rznsq_*`) | Same? |
|---|---|---|---|
| **`create(dsname, options)`** | Creates VSAM dataset | Creates VSAM KSDS via IDCAMS | Same API shape |
| **`destroy(dsname)`** | Deletes dataset | Deletes dataset via IDCAMS | Same |
| **`create_index(options)`** | Create secondary index (inactive) | Create secondary index KSDS | Same |
| **`add_index(options)`** | Build + activate index | Open + activate index | Same |
| **`drop_index(options)`** | Disable index | Close + deactivate index | Same |
| **`report_stats(conn, ...)`** | Returns JSON stats report | Returns stats struct | Similar |
| **Non-unique index flag** | `VSAMDB_CREATE_INDEX_FLAG_NON_UNIQUE` (bit 0) | `RZNSQ_INDEX_NON_UNIQUE` (1) | Same |
| **Index maintained on write/update/delete** | Yes (engine-managed) | Yes (app-managed) | Same behavior |

### Connection Management APIs

| Feature | EzNoSQL (`znsq_*`) | RezNoSQL-KSDS (`rznsq_*`) | Same? |
|---|---|---|---|
| **`open(conn, dsname, flags, options)`** | Opens connection | Opens connection | Same signature |
| **`close(conn)`** | Closes connection + indexes | Closes connection + indexes | Same |

### Document CRUD APIs

| Feature | EzNoSQL (`znsq_*`) | RezNoSQL-KSDS (`rznsq_*`) | Same? |
|---|---|---|---|
| **`write(conn, doc, len, options)`** | Insert JSON doc, key extracted from JSON | Insert JSON doc, key extracted from JSON | Same |
| **`read(conn, key_name, key_value, buf, len, flags, options)`** | Read by primary or alt key | Read by primary or alt key | Same |
| **`update(conn, key_name, key_value, doc, len)`** | Replace doc by key | Delete + rewrite by key | Same API, different internals |
| **`delete(conn, key_name, key_value)`** | Delete by key | Delete by key | Same |

### Browse APIs

| Feature | EzNoSQL (`znsq_*`) | RezNoSQL-KSDS (`rznsq_*`) | Same? |
|---|---|---|---|
| **`position(conn, ..., key, key_value, search, ...)`** | Start browse (EQ or GE) | Start browse (EQ or GE via `generic` flag) | Same |
| **`next_result(conn, buf, len)`** | Sequential read | Sequential read | Same |
| **`close_result(conn)`** | End browse | End browse | Same |
| **`write_result(conn, doc, len, options)`** | Insert during browse | Insert during browse | Same |
| **`delete_result(conn)`** | Delete last-read record | Delete last-read record via `fdelrec()` | Same |
| **`update_result(conn, doc, len)`** | Update last-read record | Delete + rewrite last-read record | Same |

### Transaction APIs

| Feature | EzNoSQL (`znsq_*`) | RezNoSQL-KSDS (`rznsq_*`) | Same? |
|---|---|---|---|
| **`commit(conn)`** | Commit transaction | No-op (returns 0) | Same API, stub |
| **`abort(conn)`** | Rollback transaction | No-op (returns 0) | Same API, stub |

### Diagnostic APIs

| Feature | EzNoSQL (`znsq_*`) | RezNoSQL-KSDS (`rznsq_*`) | Same? |
|---|---|---|---|
| **`last_result()`** | Diagnostic text report | Last error string | Same concept |

### Option Structs

| Feature | EzNoSQL (`znsq_*`) | RezNoSQL-KSDS (`rznsq_*`) | Same? |
|---|---|---|---|
| **create_options.max_space** | MB | MB | Same |
| **create_options.avg_doc_size** | Yes | Yes | Same |
| **create_options.primary_key** | UTF-8 key name, NULL=auto | UTF-8 key name, NULL=auto | Same |
| **write_options.key_name** | Key name for extraction | Key name for extraction | Same |
| **position_options search method** | `enum znsq_search_method` (EQ=0, GE=1) | `generic` flag (0=EQ, 1=GE) | Same concept |

### Reason Codes

| Code | EzNoSQL | RezNoSQL-KSDS | Same? |
|---|---|---|---|
| **Not found** | x'10' | 0x0010 | Same |
| **Duplicate key** | x'14' | 0x0014 | Same |
| **End of data** | x'43' | 0x0043 | Same |
| **Buffer too small** | x'51' | 0x0051 | Same |

## What's Different

| Feature | EzNoSQL (`znsq_*`) | RezNoSQL-KSDS (`rznsq_*`) |
|---|---|---|
| **Requires RLS + Coupling Facility** | Yes | No |
| **Requires Parallel Sysplex** | Yes | No |
| **DATABASE(JSON) attribute** | Yes | No |
| **Record overhead** | 172 bytes (keyed), 306 bytes (auto-key) | key_size + 4 bytes (e.g. 255 for default 251) |
| **Max document size** | 62 KB (recoverable), ~2 GB (non-recoverable) | ~32 KB |
| **Max database size** | 128 TB | Limited by VSAM KSDS allocation |
| **Auto-key length** | 122 bytes (EBCDIC hex) | 32 bytes (ASCII hex: timestamp+seq) |
| **Transactions (commit/abort)** | Real commit/rollback via TVS | No-op stubs |
| **Document-level locking** | NRI/CR/CRE via CF | None |
| **Unordered (hashed) keys** | Yes (128-byte hashed) | No |
| **Descending key order** | Yes (flag on create/open) | No |
| **Result set token** | `znsq_result_set_t` (int32_t) | Implicit (single browse per connection) |
| **Multi-level keys** | Yes (`\` separator, e.g. `"Address\Street"`) | No |
| **Array indexing** | Yes (one entry per array element) | No |
| **`path_name` for indexes** | Required | Not needed |
| **Stats format** | JSON report (453+ bytes) | C struct |
| **`version` field in options** | Yes (V1/V2) | No |
| **`storclas` / `mgmtclas` / `dataclas`** | Configurable in create_options | Auto-assigned by SMS ACS |
| **`log_options`** | LOG_NONE / LOG_UNDO / LOG_ALL | Always LOG_NONE equivalent |
| **`integrity` on open/read** | NRI / CR / CRE | Always NRI equivalent |
| **`autocommit` option** | Configurable | N/A (no transactions) |
| **`timeout` for lock wait** | Configurable (seconds) | N/A |
| **`set_autocommit()`** | Toggles autocommit | Not implemented |
| **Dataset creation mechanism** | `znsq_create()` internal (not IDCAMS) | IDCAMS DEFINE CLUSTER via `system()` |
| **Implementation** | IBM closed-source, z/OS system service | ~550 lines of C, single file, open source |
| **Language bindings** | C, Java, Python | C only |


## API Function Count

| Category | EzNoSQL | RezNoSQL-KSDS |
|---|---|---|
| Data Management | 6 (create, destroy, create_index, add_index, drop_index, report_stats) | 6 (same) |
| Connection | 2 (open, close) | 2 (same) |
| Document CRUD | 4 (write, read, update, delete) | 4 (same) |
| Browse | 6 (position, next_result, close_result, write_result, delete_result, update_result) | 6 (same) |
| Transaction | 3 (commit, abort, set_autocommit) | 2 (commit, abort - both no-op) |
| Diagnostics | 2 (last_result, err macro) | 2 (same) |
| **Total** | **23** | **22** |
