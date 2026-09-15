# qsoz

`qsoz` is a fast web-based amateur-radio logger written in C.

The current application release is **4.4**. Release 4.1 introduces a single-entry architecture: gmhttpd executes one program, `qsoz`, and all application routing, authentication, frontend delivery and feature dispatching are handled internally.

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
  +-- MariaDB
  +-- local callbook service
  +-- local DX Cluster service
  +-- shared radio libraries under /home/tools/mcp/work/data
```

gmhttpd does not need to know the internal application phases. It only needs to execute `qsoz`. Authentication, authorization, session validation and operation routing are application responsibilities.

There are no separate CGI executables in release 4.1 and the browser frontend is compiled into the executable. `index.html` and `qsoz.css` are not runtime files.

## Source layout

The program remains modular internally even though it produces one executable. Main modules are:

```text
qsoz.c              dispatcher and single public entry point
qsoz_frontend.c     embedded browser frontend
qsoz_login.c        authentication and OTA sessions
qsoz_proc.c         main QSO/report/import/export operations
qsoz_cmd.c          direct QSO edit/delete
qsoz_guess.c        callsign completion
qsoz_radio.c        radio polling/control
qsoz_cty.c          CTY maintenance
qsoz_completion.c   completion database rebuild
qsoz_ft8.c          FT8/MFSK analytics
tmpdata/ft8.cache   FT8 analytics cache (runtime, visible file)
qsoz_score.c        contest scoring
qsoz_config.c       configuration
qsoz_db.c           database helpers
qsoz_html.c         HTML escaping
qsoz_net.c          network helpers
qsoz_request.c      bounded request parser
qsoz_stats.c        in-memory statistics
qsoz_time.c         UTC/date helpers
qsoz_util.c         shared utility functions
```

## HTTP routing

A request without `op` serves the embedded application page. Internal operations currently use the query argument `op`:

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
qsoz?op=ft8          FT8 analytics
```

The routing syntax is internal to qsoz and may evolve without requiring gmhttpd to expose additional executables.

## Build

Build the complete application with:

```sh
cd /home/tools/mcp/work/qsoz
make
```

The result is a single executable:

```text
qsoz
```

Clean generated files with:

```sh
make clean
```

The build uses GNU89-compatible C with optimization and strict warnings.

## Configuration

Application and local-service configuration remains external in:

```text
/home/tools/mcp/work/qsoz/qsoz.conf
```

It contains database, callbook and cluster settings and must not be exposed by the web server or committed publicly.

## Authentication

Authentication is handled internally by `qsoz`. Passwords are verified with libsodium and successful login creates a temporary 16-character OTA token. Every protected operation validates the OTA token server-side. Administrative operations additionally verify the authenticated callsign.

## Application behavior

The functional behavior of the 3.x logger is preserved in the 4.1 architecture: QSO Start/End flow, completion, radio control, callbook access, DX Cluster, import/export, QSL confirmation, reports, activity analysis, contest support, CTY maintenance and FT8 analytics remain implemented by their corresponding C modules.

### Contest details

`ConDetails` keeps the contest score and adds operating time with pauses of at least one hour removed, unique callsigns by band and continent, cumulative unique callsigns by band over time, and rolling one-hour QSO rate sampled every five minutes. Band-growth and QSO-rate data can also be downloaded as CSV through relative `/files/...` URLs.

The architectural rule for 4.1 is simple: **one public executable, modular internal code, application-owned routing and authentication**.
