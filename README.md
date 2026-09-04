# qsoz

`qsoz` is a fast web-based amateur-radio logger written primarily in C.

The current application release is defined only by `QSOZ_RELEASE` in `qsoz_version.h` and is **3.11**. Individual source-file headers keep their own implementation revisions and are not application release numbers.

The project is designed around small CGI executables, a minimal browser frontend, MariaDB storage, and shared local radio/callbook services. The main goals are low latency, predictable behavior, small dependencies, and preservation of the operating semantics accumulated in the logger over time.

## Current layout

Working directory:

```text
/home/tools/mcp/work/qsoz
```

Main production DocumentRoot:

```text
/home/www/log
```

FT8 analytics DocumentRoot:

```text
/home/www/ft8
```

Main production files are symbolic links to the working tree. The FT8 site uses a separate symbolic link to `pft8.cgi`.

## Architecture

```text
Browser
  |
  +-- index.html / qsoz.css
  |
  +-- plogin.cgi -------- authentication and OTA session
  +-- ptime.cgi --------- server time and global release
  +-- pguess.cgi -------- callsign completion
  +-- pradio.cgi -------- radio polling/control
  +-- pcmd.cgi ---------- direct QSO edit/delete
  +-- pproc.cgi --------- main QSO/report/import/export/contest CGI
  +-- pcty.cgi ---------- CTY database maintenance, IK4LZH only
  +-- pcompletion.cgi --- completion database rebuild, IK4LZH only

MariaDB
  +-- user
  +-- log
  +-- who
  +-- cty
  +-- aux1
  +-- aux2 / aux3
  +-- other operational tables used by reports, imports and qrzweb

Shared local services
  +-- callbook service
  +-- DX Cluster service
  +-- radio client/data libraries under /home/tools/mcp/work/data

Separate FT8 site
  +-- pft8.cgi ---------- server-side FT8/MFSK analysis and SVG rendering
```

`pproc.cgi` remains the main application endpoint. Administrative operations that are logically independent and potentially expensive are intentionally kept in separate CGI programs.

## Build

Build everything with:

```sh
cd /home/tools/mcp/work/qsoz
make
```

Clean generated objects and CGI executables with:

```sh
make clean
```

The build uses:

```text
-O3 -std=gnu89 -Wall -Wextra
```

Main build dependencies:

```text
C compiler
MariaDB client development files and mariadb_config
libsodium
libcurl
libzip
libm
/home/tools/mcp/work/data/libradio_data.a
/home/tools/mcp/work/data/libradio_client.a
```

Current CGI targets:

```text
pguess.cgi
pcmd.cgi
plogin.cgi
pradio.cgi
ptime.cgi
pproc.cgi
pcty.cgi
pcompletion.cgi
pft8.cgi
```

Generated `.o` and `.cgi` files are build artifacts and must not be edited manually.

## Configuration

All qsoz database and local-service settings are read from:

```text
/home/tools/mcp/work/qsoz/qsoz.conf
```

The configuration is parsed by `qsoz_config.c` and contains these logical fields:

```text
db_host
db_user
db_pass
db_name
db_port
callbook_host
callbook_port
callbook_timeout
cluster_host
cluster_port
cluster_timeout
```

The file contains credentials and must never be exposed through the web server, copied into source files, printed in logs, or committed to a public repository.

Current production permissions are intended to allow the CGI user to read the configuration without making it public:

```text
640 mcp:www-data qsoz.conf
```

## Authentication and OTA sessions

Login is handled by `plogin.cgi`.

Passwords are verified with libsodium. A successful login returns a temporary 16-character OTA token together with the user page size and filter state. The browser then sends the OTA token with subsequent operations.

The `user` table stores the OTA token, its creation/last-use time and validity duration. Server-side authorization must always validate the OTA token; hiding a browser control is never considered a security boundary.

Administrative CGI programs additionally verify the authenticated callsign. `pcty.cgi` and `pcompletion.cgi` are restricted to `IK4LZH` on the server side.

## Browser frontend

`index.html` is intentionally small and uses vanilla JavaScript only. The page title is:

```html
<title>LOG by IK4LZH</title>
```

No release number is hardcoded in the title. The displayed release is fetched from:

```text
ptime.cgi?release
```

which returns `QSOZ_RELEASE` from `qsoz_version.h`.

The browser maintains:

- current OTA token;
- pagination offset and page size;
- current QSO Start timestamp;
- local filter/check state;
- radio memories and current radio state;
- two independent output areas.

Calls to `pguess.cgi` are made while editing the callsign field. Most application actions go to `pproc.cgi`. CTY and completion maintenance use their dedicated CGI endpoints.

## Main browser request protocol

Requests to `pproc.cgi` contain 13 fixed CSV fields followed by an optional Base64 payload. The frontend currently sends:

```text
0   OTA
1   action id (a01 ... a31)
2   base / pagination offset
3   page size
4   callsign
5   TX frequency
6   mode
7   TX report
8   RX report
9   contest
10  contest TX exchange
11  contest RX exchange
12  action-specific state
+   optional Base64 file payload
```

`qsoz_request.c` performs bounded parsing. The decoded file payload is limited to 20,000,000 bytes. Base64 parsing checks malformed input, truncation, padding and overflow.

CGI responses include an HTML comment identifying the action number so the browser can route the result to the appropriate output pane.

## UI actions

The current action map is:

| Action | UI label | Purpose |
| --- | --- | --- |
| `a01` | List | Reset and display the main QSO list |
| `a02` | Up | Previous main-list page |
| `a03` | Down | Next main-list page |
| `a04` | R | Refresh the main list |
| `a05` | G | Locate a list offset from a `YYYYMMDD` date entered in Call |
| `a06` | LFind | Reset callsign search |
| `a07` | Up | Previous callsign-search page |
| `a08` | Down | Next callsign-search page |
| `a09` | Apply | Resolve unresolved `log.dxcc` values through CTY |
| `a10` | Report | Band/mode, unique, WPX, DXCC and QSL statistics |
| `a11` | Curio | Callsign/band/mode/QSL rankings |
| `a12` | Activity | Year/month/day activity statistics |
| `a13` | Cluster | Enriched DX Cluster view |
| `a14` | ConGraph | Contest score graph |
| `a15` | adi-> | ADIF import |
| `a16` | lzh-> | Historical LZH-format import |
| `a17` | QSL.lotw | LoTW confirmation import |
| `a18` | QSL.eqsl | eQSL confirmation import |
| `a19` | QSL.qrz | QRZ confirmation import |
| `a20` | ->adi | ADIF export |
| `a21` | ->cbr | Cabrillo export |
| `a22` | cbr-> | Cabrillo import |
| `a23` | Start | Start QSO context and analyze the remote station |
| `a24` | QRZ.com | QRZ.com lookup through the local callbook service |
| `a25` | QRZ.ru | QRZ.ru lookup through the local callbook service |
| `a26` | End | Close and store the QSO |
| `a27` | ConList | List contests present in the log |
| `a28` | LCon | Reset selected-contest QSO list |
| `a29` | Up | Previous contest-list page |
| `a30` | Down | Next contest-list page |
| `a31` | ConScore | Calculate contest score |
| `a32` | CTY | Run `pcty.cgi`, IK4LZH only |
| `a33` | Completion | Run `pcompletion.cgi`, IK4LZH only |

`a32` and `a33` do not pass through `pproc.cgi`.

## QSO Start/End flow

### Start (`a23`)

The browser sends the current callsign, frequency, mode and reports. The server builds the QSO context without inserting a log row yet.

The Start operation:

1. records the server-side Start timestamp;
2. resolves the remote station through the CTY database;
3. displays country/base prefix, DXCC, continent, CQ/ITU zones, coordinates and GMT shift;
4. resolves the operator station as well;
5. calculates CTY-based distance and bearing;
6. uses Maidenhead locators from `who` when available for locator-based distance/bearing;
7. shows previous QSOs and QSL information relevant to the station;
8. returns the Start timestamp to the browser.

The browser keeps that Start timestamp until `a26`.

### End (`a26`)

End validates the current fields, builds the final database row and stores the QSO. Contest exchange and selected contest state are included when applicable.

The Start/End split is deliberate: Start performs lookups and operator feedback, while End is the persistent write operation.

## Frequencies, bands and modes

Shared band/mode normalization is implemented in `qsoz_util.c` and in the shared radio-data layer where appropriate.

The logger works internally with radio frequencies and maps them to amateur bands for reports, QSO history and contest calculations. Mode families used by reporting/scoring normalize operating modes into the categories required by the corresponding function or contest.

Do not duplicate band/mode tables in new CGI programs if an existing shared helper already provides the required semantics.

## Database

MariaDB is the persistent store. The schema is shared with related radio tools, so database changes must preserve compatibility with existing consumers.

### `user`

Stores authentication/session and user preferences, including:

```text
mycall
password hash
OTA token
OTA time / duration
page size
filter state
radio selection
user-defined fields
```

`mycall` is the logical user key.

### `log`

The central QSO table. It stores timestamps, operator callsign, remote callsign, frequencies, mode, signal reports, contest data, DXCC and QSL state.

The table is large and performance-sensitive. Avoid unnecessary full-table scans in interactive paths. Existing indexes and their historical query behavior must be reviewed before adding/removing indexes.

### `who`

Stores station/callsign metadata such as locator information used for distance/bearing calculations and other station context.

### `cty`

Local CTY/prefix database used for callsign resolution. It contains prefix, base prefix, country/entity information, DXCC, continent, CQ/ITU zone, coordinates and GMT shift.

The database is maintained by `pcty.cgi` rather than by normal QSO actions.

### `aux2` and `aux3`

Completion indexes used by `pguess.cgi`:

```text
aux2  callsign + 2-character gram
aux3  callsign + 3-character gram
```

Both tables have a primary key on `(callsign, gram)` and an index on `gram`.

They are rebuilt by `pcompletion.cgi` from valid callsigns found in `log` and `wc`.

## Callsign completion (`pguess.cgi`)

`pguess.cgi` is a dedicated low-latency endpoint called while the user types a callsign.

It uses trigram and bigram overlap from `aux3` and `aux2` to obtain a bounded candidate set, then completes fuzzy ranking in C using exact edit-distance logic. The endpoint returns clickable callsign suggestions as HTML.

The completion database is intentionally precomputed so the interactive request does not repeatedly scan the QSO log.

## Completion database rebuild (`pcompletion.cgi`)

`pcompletion.cgi` is an independent administrative CGI and is restricted to `IK4LZH` both in the UI and on the server side.

Its source set is equivalent to the historical completion SQL logic:

```text
all callsigns from log
UNION
all callsigns from wc
```

Normalization rules are:

1. trim surrounding spaces;
2. uppercase ASCII letters;
3. reject the full normalized callsign unless every character is `A-Z` or `0-9`;
4. only after validation, keep the first six characters;
5. deduplicate callsigns;
6. generate distinct bigrams and trigrams for each callsign.

Validation is deliberately performed before truncation. A callsign containing `/` or another invalid character must not become valid merely because the invalid part would be removed by truncation.

The implementation keeps most of the expensive transformation work in C:

- source rows are streamed from MariaDB;
- normalized callsigns are deduplicated in an in-memory open-addressing hash table;
- bigrams/trigrams are generated in C;
- rows are inserted in large multi-value batches.

For safe replacement, the CGI builds temporary tables first:

```text
aux2_new
aux3_new
```

Indexes are created after bulk insertion. Row counts are validated, then both live tables are replaced with one atomic `RENAME TABLE` operation. Users of `pguess.cgi` therefore continue to see valid `aux2`/`aux3` data throughout the rebuild.

A MariaDB named lock prevents two completion rebuilds from running concurrently.

## CTY maintenance (`pcty.cgi`)

`pcty.cgi` is the dedicated CTY update endpoint and is restricted to `IK4LZH`.

It downloads and processes the CTY source, builds replacement data and uses staging/swap semantics rather than leaving the live CTY table partially updated.

Normal QSO processing must not perform CTY database maintenance implicitly.

## Radio control (`pradio.cgi` and shared radio layer)

Radio access is isolated from the main reporting/database code.

The current stack supports the TS-890S path and a `rigctld` path. Shared radio protocol/data code lives under:

```text
/home/tools/mcp/work/data
```

and is linked into `pproc.cgi` through:

```text
libradio_data.a
libradio_client.a
```

`pradio.cgi` handles browser radio operations, while `pproc.cgi` uses the shared libraries for QSO-related radio data.

Do not duplicate protocol constants or radio-state decoding inside unrelated CGI programs.

## Callbook service

QRZ.com and QRZ.ru lookups are requested through the configured local callbook service rather than embedding remote-service credentials or HTTP logic in the browser.

The two UI actions are:

```text
a24 QRZ.com
a25 QRZ.ru
```

Remote credentials belong to the callbook service/configuration layer and must not be copied into `qsoz` source code or README files.

## DX Cluster

`a13` provides an enriched DX Cluster view using the configured cluster host/port and local QSO/CTY context.

Network access uses bounded timeouts. Cluster connectivity failures must remain local to the cluster action and must not block unrelated logger functions.

## Import, export and QSL confirmation

Supported file operations are intentionally handled by explicit actions:

```text
a15  ADIF import
a16  historical LZH import
a17  LoTW confirmation import
a18  eQSL confirmation import
a19  QRZ confirmation import
a20  ADIF export
a21  Cabrillo export
a22  Cabrillo import
```

Uploaded data is sent as a Base64 payload through the bounded request parser. Imports must validate fields before constructing database writes. Exports are generated from the authenticated user's data and current action parameters.

## Reports and activity

The main reporting operations are:

```text
a10  Report
a11  Curio
a12  Activity
```

They reuse shared band/mode, WPX, time and in-memory aggregation helpers where possible.

`qsoz_stats.c` provides the bounded in-memory aggregation structure used by reporting and contest scoring. The limits in `qsoz_stats.h` are part of the current resource-control design and should not be increased casually.

## Contest support

Contest functions are integrated into the main logger rather than implemented as separate programs.

Relevant actions:

```text
a14  contest score graph
a27  contest list
a28  selected-contest QSO list
a29  previous contest page
a30  next contest page
a31  contest score
```

Contest scoring is implemented primarily in `pscore.c` with shared statistics, utility and radio data. The scorer supports the contest families encoded in the current source and preserves contest-specific exchange, multiplier and scoring semantics.

When changing contest logic, verify both the individual contest rule and regressions in unrelated contests. `.score_before.txt` is a local regression reference and is not part of the runtime application.

## Shared utility modules

### `qsoz_config.c` / `qsoz_config.h`

Loads the fixed project configuration into a bounded `QsozConfig` structure.

### `qsoz_db.c` / `qsoz_db.h`

Database helpers for bounded escaping and construction of QSO values used by database writes.

### `qsoz_html.c` / `qsoz_html.h`

HTML/attribute/JavaScript-safe output helpers used where generated content contains external or database-derived strings.

### `qsoz_net.c` / `qsoz_net.h`

Small TCP client helpers with timeout handling and line-oriented reads.

### `qsoz_request.c` / `qsoz_request.h`

Bounded parser for the 13-field request protocol plus optional Base64 payload.

### `qsoz_stats.c` / `qsoz_stats.h`

Bounded in-memory keyed aggregation used by reports and contest scoring.

### `qsoz_time.c` / `qsoz_time.h`

UTC/date/epoch conversion helpers.

### `qsoz_util.c` / `qsoz_util.h`

Shared band, mode, WPX, PACC, elapsed-time, token and bounded-copy helpers.

## FT8/MFSK analytics (`pft8.cgi`)

`pft8.cgi` serves the separate site:

```text
https://ft8.chaos.cc/
```

It replaces the previous PHP/Google-Charts implementation with one compiled CGI. The CGI generates its own HTML, CSS and SVG; no JavaScript chart library is loaded by the page.

The data source is the shared QSO `log` table. The analysis considers rows with:

```text
mode = FT8 or MFSK
signaltx in [-35,+35]
signalrx in [-35,+35]
(signaltx - signalrx) in [-35,+35]
```

The same accepted set is used for the displayed probability distribution and its denominator, so PDF normalization, QSO count, average and standard deviation are consistent.

The page currently provides:

- TX-RX probability distributions by amateur band and overall;
- QSO count, average TX-RX difference and standard deviation;
- time/CQ-zone activity visualization.

The CQ-zone time bucket calculation intentionally preserves the semantics of the previous implementation.

### FT8 cache

A complete FT8/MFSK scan is expensive on a large log, so `pft8.cgi` stores only the computed aggregates in:

```text
/home/www/ft8/.pft8.cache
```

The cache is binary and is not a rendered-page cache. It is accepted only when its internal magic/version and database key match.

The current invalidation key includes information derived from:

```text
log UPDATE_TIME
cty UPDATE_TIME
MAX(log.open)
log TABLE_ROWS estimate
```

If the key changes, the CGI recomputes the aggregate data and replaces the cache atomically. If the key is unchanged, the CGI reads the small aggregate cache and renders the SVG output without scanning the QSO table again.

Apache already compresses the generated HTML/SVG with `mod_deflate`; manual compression in the CGI is unnecessary.

## Production deployment

### Main logger

The main site is served from:

```text
/home/www/log
```

The operational files are symbolic links into `/home/tools/mcp/work/qsoz`. The expected CGI setup is equivalent to:

```apache
DirectoryIndex index.html
AddHandler cgi-script .cgi

<Directory /home/www/log>
  Options +ExecCGI -Indexes -MultiViews
  Require all granted
</Directory>
```

The application must not expose `qsoz.conf` through the DocumentRoot.

### FT8 site

The current FT8 link is:

```text
/home/www/ft8/pft8.cgi -> /home/tools/mcp/work/qsoz/pft8.cgi
```

The HTTPS virtual host uses:

```apache
DocumentRoot /home/www/ft8
DirectoryIndex pft8.cgi
AddHandler cgi-script .cgi

<Directory /home/www/ft8>
  Options +ExecCGI -Indexes -MultiViews
  Require all granted
</Directory>
```

`pft8.cgi` is the default application for `ft8.chaos.cc` and does not depend on the old PHP files.

## Test deployment

`deploy_qsoz_test.sh` installs a test set of symbolic links under:

```text
/home/www/log/qsoz
```

Run as root:

```sh
sudo ./deploy_qsoz_test.sh install
sudo ./deploy_qsoz_test.sh remove
```

The script also adjusts `qsoz.conf` group/permissions so Apache can read it during the test deployment.

The script is a convenience for the main qsoz interface; it is not the deployment mechanism for the separate FT8 virtual host.

## Security invariants

The following rules are part of the application design:

1. Never expose or duplicate credentials from `qsoz.conf` or other local service configurations.
2. Never trust a hidden browser control as authorization; validate OTA and privileges in the CGI.
3. Escape SQL data with the existing database helpers or MariaDB escaping before constructing SQL text.
4. Escape database/external strings before embedding them in HTML, attributes or JavaScript.
5. Keep file-upload size and parser bounds intact unless there is a demonstrated need to change them.
6. Keep CTY and completion rebuilds isolated from normal QSO processing.
7. Use staging plus atomic replacement for large administrative database rebuilds when readers must remain available.
8. Do not print secrets, password hashes, OTA tokens or remote-service credentials to diagnostic output.
9. Preserve timeout handling for network/radio services so an unavailable external component cannot block the logger indefinitely.
10. Treat the shared MariaDB schema and shared radio libraries as interfaces used by other projects; review compatibility before changing them.

## Coding standard

C code in this project follows the current project conventions:

- C89/gnu89 source style;
- declarations at the beginning of the function/block;
- initialization after declarations;
- prefer `for` to `while` when it keeps the code simpler;
- use standard-library functions instead of unnecessary helper wrappers;
- avoid allocations and copies that do not provide a measurable benefit;
- no unused variables, functions or dead code;
- comments in English only and only when they add information;
- `//` comments;
- opening brace on the same line as the statement, with one space before it;
- two-space indentation;
- small functions with explicit bounds and error paths;
- performance-sensitive code should stream or aggregate rather than materialize unnecessary intermediate data.

Source headers keep the historical project start year and the file's own revision, for example:

```c
// Gianluca Mazzini @2022- Version 3.xx
```

The application release is independent and comes only from `qsoz_version.h`.

## File inventory

### Frontend and build

```text
index.html             browser UI and request orchestration
qsoz.css               main logger stylesheet
Makefile               production build
qsoz_version.h         global application release
qsoz.conf              private runtime configuration
deploy_qsoz_test.sh    test symlink deployment helper
```

### CGI sources

```text
plogin.c       login and OTA creation
ptime.c        server epoch and release endpoint
pguess.c       interactive callsign suggestions
pcmd.c         direct QSO edit/delete
pradio.c       radio polling/control
pproc.c        main application CGI
pcty.c         CTY administrative rebuild
pcompletion.c  callsign-completion administrative rebuild
pft8.c         standalone FT8/MFSK analytics CGI
```

### Shared qsoz sources

```text
pscore.c / pscore.h
qsoz_config.c / qsoz_config.h
qsoz_db.c / qsoz_db.h
qsoz_html.c / qsoz_html.h
qsoz_net.c / qsoz_net.h
qsoz_request.c / qsoz_request.h
qsoz_stats.c / qsoz_stats.h
qsoz_time.c / qsoz_time.h
qsoz_util.c / qsoz_util.h
```

### Local non-runtime references

The working directory may contain local build logs and regression references such as:

```text
build_auth.log
build_auth2.log
.build_no_md5.log
.score_before.txt
```

They are not required by the running application and must not be confused with runtime configuration or generated binaries.

## Maintenance checklist

Before changing qsoz:

1. Identify whether the behavior belongs in `pproc.cgi` or deserves an independent CGI.
2. Check for an existing shared helper before adding duplicate logic.
3. Preserve OTA authorization and privilege checks.
4. Preserve the current database semantics before attempting optimization.
5. Compile with the existing `-Wall -Wextra` flags and resolve new warnings.
6. Test the specific changed path and at least one unaffected core path.
7. For database rebuilds, verify source counts/result counts before replacing live tables.
8. For performance changes, measure the real bottleneck instead of assuming SQL or C is responsible.
9. Keep `qsoz_version.h` as the single global release source.
10. Update this README to describe the resulting current state, not the implementation history.

## Related projects

`qrzweb` is a separate project even though it shares parts of the same radio/database ecosystem. Its source is maintained separately under:

```text
/home/tools/mcp/work/qrzweb
```

Do not merge qrzweb-specific web workflows into qsoz merely because some database tables are shared.
