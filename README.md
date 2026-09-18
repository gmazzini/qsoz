# qsoz

`qsoz` 4.13 is a fast web-based amateur-radio logger written in GNU89-compatible C.

## Runtime architecture

```text
Browser
  |
gmhttpd
  |
qsoz
  |
  +-- embedded HTML / CSS / JavaScript
  +-- authentication and OTA sessions
  +-- QSO processing and editing
  +-- callsign completion
  +-- radio access
  +-- reports and contest scoring
  +-- CTY maintenance
  +-- FT8 analytics
  |
  +-- log.db        authoritative QSO log
  +-- user.db       users and sessions
  +-- cty.db        CTY data
  +-- cache.db      derived DXCC cache
  +-- completion.db derived callsign completion index
  +-- work/data     shared radio and callbook services
```

`gmhttpd` executes the single `qsoz` program. Routing, authentication, authorization and application dispatch are handled internally. The browser frontend is compiled into the executable.

When started by gmhttpd as root, qsoz drops permanently to the `mcp` account before dispatch. This keeps SQLite database, WAL and SHM ownership consistent with the persistent `adif_rx` writer.

## Source layout

```text
qsoz.c              dispatcher and public entry point
qsoz_frontend.c     embedded browser frontend
qsoz_login.c        login handling
qsoz_user.c         users, authentication and sessions
qsoz_proc.c         QSO, report, import and export operations
qsoz_cmd.c          direct QSO edit/delete
qsoz_guess.c        callsign completion
qsoz_radio.c        radio polling/control
qsoz_cty.c          CTY maintenance
qsoz_completion.c   completion database rebuild
qsoz_ft8.c          FT8/MFSK analytics
qsoz_score.c        contest scoring
qsoz_contest.c      contest details and exports
qsoz_config.c       configuration
qsoz_db.c           SQLite QSO-log access
qsoz_cache.c        derived DXCC cache
qsoz_html.c         HTML escaping
qsoz_net.c          network helpers
qsoz_request.c      bounded request parser
qsoz_stats.c        in-memory statistics
qsoz_time.c         UTC/date helpers
qsoz_util.c         shared utility functions
```

## HTTP routing

A request without `op` serves the embedded application page.

```text
qsoz                 frontend
qsoz?op=login        authentication
qsoz?op=proc         main application actions
qsoz?op=cmd          direct QSO edit/delete
qsoz?op=guess        callsign completion
qsoz?op=radio        radio operations
qsoz?op=time         server UTC epoch
qsoz?op=release      application release
qsoz?op=cty          CTY maintenance
qsoz?op=completion   completion rebuild
qsoz?op=users        user administration
qsoz?op=ft8          FT8 analytics
```

## QSO log

Authoritative database:

```text
/home/tools/mcp/work/qsoz/log.db
```

The `log` table uses the primary key:

```text
(open,mycall,callsign,freqtx)
```

Secondary indexes cover:

```text
(open,mycall)
(dxcc,mycall)
(mycall,callsign)
(contest,mycall)
(mycall)
```

SQLite WAL is enabled. `qsoz` and `adif_rx` both access the database as user `mcp`.

`log_meta` contains a persistent log revision. SQLite triggers increment it on `INSERT`, `UPDATE` and `DELETE`. FT8 analytics use this revision to invalidate their cache only when QSO data changes.

## FT8 analytics

FT8/MFSK analytics are available through:

```text
qsoz?op=ft8
```

The derived runtime cache is:

```text
/home/tools/mcp/work/qsoz/tmpdata/ft8.cache
```

The cache key includes the QSO-log revision and CTY database identity. Unchanged requests reuse the cache directly.

## Users and authentication

User database:

```text
/home/tools/mcp/work/qsoz/user.db
```

Passwords are verified with libsodium. Successful login creates a temporary 16-character OTA token, and protected operations validate the token server-side.

The user database contains the operational user/session fields used by the application. Administrative user operations are restricted server-side to the configured administrator identity.

## CTY database

CTY database:

```text
/home/tools/mcp/work/qsoz/cty.db
```

It contains the complete prefix dataset and indexes used by callsign and DXCC lookup. CTY rebuild creates and validates a new SQLite database beside the active file and installs it with an atomic rename. `tmpdata/cty.lock` prevents concurrent rebuilds.

## Callsign completion

Completion database:

```text
/home/tools/mcp/work/qsoz/completion.db
```

It is derived from callsigns in:

```text
/home/tools/mcp/work/qsoz/log.db
/home/tools/mcp/work/qrzweb/graph.db
```

The database contains `bigram` and `trigram` tables and can be regenerated. `tmpdata/completion.lock` prevents concurrent rebuilds.

## Derived DXCC cache

Database:

```text
/home/tools/mcp/work/qsoz/cache.db
```

The `dxcc_stats` table caches per-user DXCC QSO/QSL counts for Cluster display. It is derived entirely from `log.db` and can be regenerated.

## Callbook

qsoz uses the shared `work/data` radio API. Callbook records are stored in:

```text
/home/tools/mcp/work/data/who.db
```

Network access is provided by the local callbook service configured in `qsoz.conf`.

## Contest support

Contest functions include scoring, operating-time analysis, unique callsigns by band and continent, cumulative band growth and rolling one-hour QSO rate sampled every five minutes.

Generated contest and ADIF/Cabrillo files are written under:

```text
/home/www/log/files
```

The directory is writable by the `mcp` account used by qsoz.

## Configuration

Runtime configuration:

```text
/home/tools/mcp/work/qsoz/qsoz.conf
```

It contains the local callbook and DX Cluster endpoint settings. Private configuration must not be exposed by the web server or committed publicly.

## Build

```sh
cd /home/tools/mcp/work/qsoz
make
```

The result is the single executable:

```text
qsoz
```

Clean build products with:

```sh
make clean
```

## Operational checks

Current release:

```sh
QUERY_STRING=op=release ./qsoz
```

FT8 analytics can be exercised locally with:

```sh
QUERY_STRING=op=ft8 ./qsoz
```
