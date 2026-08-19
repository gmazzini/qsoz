# qsoz

Web logger radioamatoriale di Gianluca Mazzini / IK4LZH.

Questo README descrive lo stato reale del progetto `qsoz` in `/home/tools/mcp/work/qsoz` al 18 agosto 2026. È pensato come documento master del progetto: architettura, flussi, protocollo tra browser e CGI, database, dipendenze, servizi esterni, contest scoring, import/export, radio control, deployment e responsabilità di ogni file presente nella directory.

La release applicativa mostrata dalla UI è definita in `qsoz_version.h` ed è attualmente **3.10**. I singoli sorgenti mantengono versioni storiche proprie e non devono essere interpretati come numero globale di release.

---

## 1. Obiettivo del progetto

`qsoz` è un logger web personale/multiutente orientato all'uso radio reale. Le funzioni principali sono:

- login autenticato e sessione temporanea OTA;
- inserimento manuale di QSO con Start/End;
- lettura e controllo della radio;
- elenco, ricerca e correzione dei QSO;
- risoluzione CTY/DXCC, distanza, bearing e Maidenhead;
- lookup callbook QRZ.com e QRZ.ru tramite servizio locale;
- suggerimento callsign fuzzy;
- statistiche generali, attività e curiosità;
- DX Cluster con statistiche QSO/QSL contestuali;
- gestione contest, score e grafico temporale;
- import ADIF, formato storico LZH e Cabrillo;
- export ADIF e Cabrillo;
- import conferme QSL LoTW/eQSL/QRZ;
- aggiornamento amministrativo del database CTY da BigCTY;
- ricostruzione amministrativa del database di completion `aux2/aux3`;
- analisi globale FT8/MFSK con pagina dedicata `ft8.chaos.cc` generata interamente da CGI C.

L'architettura corrente mantiene CGI C piccoli e un CGI principale (`pproc.cgi`), con JavaScript vanilla sul browser e MariaDB come storage permanente.

---

## 2. Percorsi e deployment reale

Sorgente di lavoro:

```text
/home/tools/mcp/work/qsoz
```

DocumentRoot Apache reale:

```text
/home/www/log
```

Il sito di produzione usa symlink diretti dal DocumentRoot ai file di `qsoz`. Al momento risultano collegati:

```text
/home/www/log/index.html  -> /home/tools/mcp/work/qsoz/index.html
/home/www/log/qsoz.css    -> /home/tools/mcp/work/qsoz/qsoz.css
/home/www/log/pguess.cgi  -> /home/tools/mcp/work/qsoz/pguess.cgi
/home/www/log/pproc.cgi   -> /home/tools/mcp/work/qsoz/pproc.cgi
/home/www/log/plogin.cgi  -> /home/tools/mcp/work/qsoz/plogin.cgi
/home/www/log/pcmd.cgi    -> /home/tools/mcp/work/qsoz/pcmd.cgi
/home/www/log/pradio.cgi  -> /home/tools/mcp/work/qsoz/pradio.cgi
/home/www/log/ptime.cgi   -> /home/tools/mcp/work/qsoz/ptime.cgi
/home/www/log/pcty.cgi    -> /home/tools/mcp/work/qsoz/pcty.cgi
```

Il virtual host Apache corrente contiene:

```text
DirectoryIndex index.html
AddHandler cgi-script .cgi
DocumentRoot /home/www/log
Options +ExecCGI -Indexes -MultiViews
```

`index.html` è servito come HTML statico. Non è necessario né desiderato un handler PHP per `.html`.

Esiste anche `deploy_qsoz_test.sh`, che può creare un deployment di test sotto:

```text
/home/www/log/qsoz
```

Attualmente quel deployment di test non è installato.

**Nota importante:** `deploy_qsoz_test.sh` non include ancora `pcty.cgi` nella propria lista di symlink, mentre il deployment di produzione lo usa. È una differenza reale dello stato corrente e non viene corretta automaticamente da questo README.

---

## 3. Architettura complessiva

```text
Browser
  |
  +-- index.html + qsoz.css
  |
  +-- plogin.cgi ---------> MariaDB user
  |       |                    |
  |       +-- OTA 16 char -----+
  |
  +-- pproc.cgi ----------> MariaDB log/who/cty/aux1
  |       |
  |       +--> libradio_data.a
  |       |      ADIF / CTY / locator / distance / bearing
  |       |
  |       +--> libradio_client.a --> callbookd :22223
  |       |                              |
  |       |                              +--> QRZ.com / QRZ.ru
  |       |                              +--> aggiorna who
  |       |
  |       +--> qsoz_net ----------> dxcluster :22222
  |       |
  |       +--> pscore.o ----------> contest scoring
  |
  +-- pguess.cgi ---------> aux2 / aux3
  +-- pcmd.cgi -----------> modifica log
  +-- pradio.cgi ---------> TS-890S o rigctld
  +-- ptime.cgi ----------> epoch / release
  +-- pcty.cgi -----------> BigCTY -> cty atomic swap
```

I servizi radio condivisi non sono duplicati dentro qsoz. La sorgente canonica è:

```text
/home/tools/mcp/work/data
```

In particolare:

```text
libradio_data.a
radio_data.h
libradio_client.a
radio_client.h
callbookd
dxcluster
```

---

## 4. Servizi esterni locali

### 4.1 callbookd

Servizio systemd attualmente attivo:

```text
callbookd.service
/home/tools/mcp/work/data/callbookd
```

Default usato da qsoz:

```text
127.0.0.1:22223
callbook_timeout=5 s
```

`qsoz` non contiene credenziali QRZ.com/QRZ.ru, sessioni HTTP o parser XML dei provider. Chiede il lookup a `callbookd` tramite:

```c
radio_callbook_lookup(host,port,source,callsign,timeout,response,cap)
```

Esiti canonici:

```text
RADIO_CALLBOOK_OK        1
RADIO_CALLBOOK_NOTFOUND  0
RADIO_CALLBOOK_ERROR    -1
```

`callbookd` aggiorna la tabella `who` e il CGI poi legge i dati dal DB.

### 4.2 dxcluster

Servizio systemd attualmente attivo:

```text
dxcluster.service
/home/tools/mcp/work/data/dxcluster
```

Default usato da qsoz:

```text
127.0.0.1:22222
cluster_timeout=5 s
```

`pproc.cgi` invia al servizio:

```text
<numero_spot>,<filtro>\n
```

e riceve righe:

```text
epoch,spotter,frequency,dx
```

Il CGI arricchisce poi gli spot usando CTY, storico `log` e cache `aux1`.

---

## 5. Configurazione `qsoz.conf`

File privato:

```text
/home/tools/mcp/work/qsoz/qsoz.conf
```

Permessi correnti:

```text
640 mcp:www-data
```

Nel file reale oggi sono presenti soltanto i parametri DB. Gli altri valori vengono dai default di `qsoz_config.c`.

Chiavi supportate:

```ini
db_host=127.0.0.1
db_user=...
db_pass=...
db_name=...
db_port=3306

callbook_host=127.0.0.1
callbook_port=22223
callbook_timeout=5

cluster_host=127.0.0.1
cluster_port=22222
cluster_timeout=5
```

`qsoz_config_load()` rifiuta chiavi sconosciute e valori numerici fuori intervallo. `db_user` e `db_name` sono obbligatori; host e porte hanno default.

Il file contiene segreti DB e non deve essere pubblicato, copiato nell'HTML o reso scaricabile via web.

---

## 6. Autenticazione e sessione OTA

L'autenticazione è gestita da `plogin.cgi`.

### Password

La tabella `user` contiene `passwd_hash`. Il backend usa libsodium:

```c
crypto_pwhash_str_verify()
crypto_pwhash_str_needs_rehash()
crypto_pwhash_str()
```

Se l'hash è valido ma non usa più i parametri interattivi correnti, viene rigenerato automaticamente dopo un login riuscito.

La password viene azzerata dalla memoria con `sodium_memzero()` prima dell'uscita.

### OTA

Dopo login corretto viene generato un token casuale base62 di 16 caratteri:

```text
0-9 A-Z a-z
```

Il token viene salvato in `user.ota` insieme a `lastota`.

Ogni CGI che modifica o legge dati privati verifica:

```text
user.ota = token
AND lastota + durationota > now
```

`durationota` ha default DB 86400 secondi.

Il browser conserva l'OTA soltanto nella variabile JavaScript `ota`; non viene usato un cookie applicativo qsoz.

Risposta login:

```text
OTA,mypage,filter
```

Se il login fallisce:

```text
,0,
```

---

## 7. Frontend `index.html`

`index.html` contiene tutta la UI e il JavaScript applicativo, senza framework.

Header sorgente corrente:

```text
Gianluca Mazzini @2022- Version 3.02
```

Il titolo HTML storico resta:

```text
LOG by IK4LZH v2.1
```

La release reale mostrata nella pagina viene invece letta dinamicamente da:

```text
ptime.cgi?release
```

che restituisce `QSOZ_RELEASE`, oggi 3.08.

### 7.1 Campi QSO

Campi principali:

```text
call       callsign
freq       frequenza visualizzata in kHz
mode       modo
sigtx      rapporto inviato
sigrx      rapporto ricevuto
contest    contest ID
contx      exchange inviato
conrx      exchange ricevuto
```

I campi vengono convertiti in uppercase e la virgola viene eliminata perché la virgola è delimitatore del protocollo interno CGI.

### 7.2 Due aree output

La pagina ha due pannelli:

```text
out   output principale a sinistra
out2  output secondario a destra
```

Le azioni 9-22 sono normalmente inviate a `out2`; le altre a `out`.

Il pulsante CTY `a32` usa esplicitamente `out2`.

### 7.3 Stato locale `v[1..22]`

Il browser usa un array locale di flag:

```js
let v = new Array(22+1).fill(0);
```

Significato UI:

```text
v[1]      Rig On/Off
v[8]      Contest On/Off
v[9]      filtro PH
v[10]     filtro CW
v[11]     filtro DG
v[12]     banda 10
v[13]     banda 15
v[14]     banda 20
v[15]     banda 40
v[16]     banda 80
v[17]     banda 160
v[18]     banda 12
v[19]     banda 17
v[20]     banda 30
v[21]     banda 60
v[22]     ConTX++ locale
```

Al login, i 13 caratteri di `user.filter` inizializzano `v[9]..v[21]`.

Per la richiesta Cluster (`a13`) il browser invia i 13 flag `v[9]..v[21]` come filtro corrente.

`v[22]` non fa parte di quel filtro persistente; quando è attivo, dopo `End` incrementa localmente `contx` di 1.

### 7.4 Radio memories locali

Tre coppie di pulsanti memorizzano nel browser frequenza/modo:

```text
b02 / b03  slot 1
b04 / b05  slot 2
b06 / b07  slot 3
```

Il pulsante di richiamo aggiorna anche la radio se RigOO è attivo.

### 7.5 Poll radio

Se `v[1]==1`, `radioread()` interroga `pradio.cgi` ogni 10 secondi.

Il backend restituisce:

```text
frequency_hz,mode
```

Il browser visualizza la frequenza come Hz / 1000 con una cifra decimale.

### 7.6 Clock UTC

`timeupdate()` aggiorna il display ogni secondo. Ogni 60 cicli sincronizza la differenza tra clock browser e server interrogando `ptime.cgi`.

### 7.7 Grafico contest

`ConGraph` riceve da `pproc.cgi` una `<div class="gchart">` con dati JSON nel `data-rows`.

Il frontend disegna SVG responsive con quattro serie:

```text
QSO
Points
Mults
Score
```

I campioni del backend sono finestre da 15 minuti.

---

## 8. Protocollo browser -> `pproc.cgi`

Il frontend costruisce:

```js
[
  ota,
  action,
  base,
  mypage,
  call,
  freq,
  mode,
  sigtx,
  sigrx,
  contest,
  contx,
  conrx,
  extra,
  data
].join(",")
```

`qsoz_request_read()` interpreta i primi **13 campi** terminati da virgola e tutto ciò che segue come payload base64.

Mappa:

```text
field 0   OTA
field 1   action, esempio a23
field 2   base/offset
field 3   mypage
field 4   call
field 5   freq
field 6   mode
field 7   sigtx
field 8   sigrx
field 9   contest
field 10  contest TX
field 11  contest RX
field 12  extra
payload   file base64
```

`extra` viene usato principalmente per:

```text
a13 Cluster   -> filtro 13 bit
a26 End       -> timestamp Start salvato dal browser
```

Dimensione massima payload decodificato:

```text
20,000,000 byte
```

Il decoder accetta Base64 classico e URL-safe (`+/-`, `/_`) e controlla padding, troncamenti e overflow.

---

## 9. Mappa completa azioni UI `aXX`

### Liste

```text
a01 List      reset offset e lista generale
a02 ↑         pagina precedente lista generale
a03 ↓         pagina successiva lista generale
a04 R         refresh lista generale
a05 G         calcola offset usando una data YYYYMMDD inserita in Call

a06 LFind     reset ricerca callsign
a07 ↑         pagina precedente ricerca
a08 ↓         pagina successiva ricerca

a28 LCon      reset lista contest selezionato
a29 ↑         pagina precedente contest
a30 ↓         pagina successiva contest
```

La ricerca `LFind` usa SQL `callsign LIKE <call>`; il valore viene escaped ma non vengono aggiunti wildcard automaticamente, quindi eventuali `%`/`_` hanno semantica SQL LIKE se presenti nell'input.

### Analisi e manutenzione

```text
a09 Apply      risolve log.dxcc=0 tramite CTY
a10 Report     statistiche banda/modo, unique, WPX, DXCC, QSL
a11 Curio      classifiche callsign/band/mode/QSL
a12 Activity   statistiche anno/mese/giorno
a13 Cluster    DX Cluster arricchito
a14 ConGraph   grafico score contest a intervalli 15 min
a27 ConList    elenco contest presenti nel log
a31 ConScore   score contest
```

### File e QSL

```text
a15 adi->      import ADIF
a16 lzh->      import formato storico LZH
a17 QSL.lotw   importa conferme LoTW da ADIF
a18 QSL.eqsl   importa conferme eQSL da ADIF
a19 QSL.qrz    importa conferme QRZ da ADIF
a20 ->adi      export ADIF
a21 ->cbr      export Cabrillo
a22 cbr->      import Cabrillo
```

### QSO e callbook

```text
a23 Start      apre logicamente un QSO, analizza callsign e storico
a24 QRZ.com    lookup callbook via callbookd
a25 QRZ.ru     lookup callbook via callbookd
a26 End        scrive il QSO nel database
```

### CTY

```text
a32 CTY        chiama pcty.cgi; solo IK4LZH può aggiornare CTY
```

---

## 10. Flusso Start / End di un QSO

### Start (`a23`)

Il browser invia callsign, frequenza, modo e rapporti correnti.

Il server:

1. salva nell'output il timestamp `Start: YYYY-MM-DD HH:MM:SS`;
2. esegue `radio_cty_lookup()` del corrispondente;
3. mostra base CTY, nome country, DXCC, continente, CQ/ITU zone, coordinate e GMT shift;
4. risolve CTY anche per `mycall`;
5. calcola distanza e bearing dalle coordinate CTY;
6. se esistono locator Maidenhead in `who`, calcola anche distanza/bearing da locator;
7. conta QSO precedenti con lo stesso DXCC;
8. se `who` non contiene il callsign, chiede un lookup QRZ.com a `callbookd`;
9. mostra dati callbook e immagine, se disponibili;
10. legge tutto lo storico del callsign nel log;
11. mostra fino agli ultimi 5 QSO;
12. aggrega storico per banda/modo e QSL LoTW/eQSL/QRZ.

Il browser estrae il testo dopo `Start:` e lo conserva nella variabile locale `start`.

### End (`a26`)

Il browser reinvia il timestamp `start` nel field 12.

Il server:

1. valida campi essenziali;
2. converte frequenza UI kHz in Hz moltiplicando per 1000;
3. converte `start` in epoch UTC;
4. risolve DXCC via CTY;
5. costruisce i valori SQL con `qsoz_db_log_values()`;
6. inserisce in `log` con `open=start`, `close=time(NULL)`.

Se ConTX++ (`v[22]`) è attivo il browser incrementa poi localmente l'exchange TX.

---

## 11. Frequenze e modi

Nel database `log.freqtx` e `log.freqrx` sono memorizzati in **Hz**.

La UI usa valori in **kHz** con una cifra decimale, per esempio:

```text
14200.0
```

che diventa:

```text
14200000 Hz
```

`qsoz_band()` ricava la banda dal MHz intero e restituisce valori storici in decimi di metro:

```text
1 MHz       -> 1600
3 MHz       -> 800
5 MHz       -> 600
7 MHz       -> 400
10 MHz      -> 300
14 MHz      -> 200
18 MHz      -> 170
21 MHz      -> 150
24 MHz      -> 120
28/29 MHz   -> 100
50 MHz      -> 60
144/145 MHz -> 20
430-433 MHz -> 7
```

Nel contest scorer viene poi spesso usato `/10`, ottenendo 160, 80, 40, 20, ecc.

`qsoz_mode()` normalizza:

```text
CW                                      -> CW
FT8 RTTY MFSK FT4 PKT TOR AMTOR PSK     -> DG
SSB USB LSB FM AM                        -> PH
altro                                    -> ND
```

---

## 12. Database

### 12.1 `user`

```text
mycall          varchar(20) PK
passwd_hash     varchar(128)
ota             varchar(16)
lastota         bigint
durationota     bigint default 86400
mypage          smallint default 25
filter          varchar(20) default 1001110000000
radio           varchar(50)
udef1           varchar(20)
udef2           varchar(20)
```

Indici:

```text
PRIMARY KEY(mycall)
KEY ota(ota,lastota)
KEY ota_2(ota)
```

Responsabilità:

- autenticazione;
- sessione OTA;
- paginazione predefinita;
- filtro cluster iniziale;
- configurazione radio per utente;
- due comandi radio user-defined.

### 12.2 `log`

```text
mycall       varchar(20)
callsign     varchar(20)
open         bigint epoch UTC
close        bigint epoch UTC
mode         varchar(8)
freqtx       bigint Hz
freqrx       bigint Hz
signaltx     varchar(8)
signalrx     varchar(8)
contesttx    varchar(10)
contestrx    varchar(10)
contest      varchar(20)
lotw         tinyint default 0
eqsl         tinyint default 0
qrz          tinyint default 0
dxcc         smallint default 0
```

Primary key:

```text
(open,mycall,callsign,freqtx)
```

Indici correnti:

```text
(open,mycall)
(dxcc,mycall)
(mycall,callsign)
(contest,mycall)
(mycall)
```

È la tabella centrale del progetto.

### 12.3 `who`

Cache/anagrafica callbook:

```text
callsign PK
firstname
lastname
addr1
addr2
state
zip
country
grid
email
cqzone
ituzone
born
image
time
src
```

Viene aggiornata principalmente da `callbookd`; qsoz la legge per Start, lookup e export Cabrillo.

### 12.4 `cty`

```text
base
name
dxcc
cont
cqzone
ituzone
latitude
longitude
gmtshift
prefix
```

Indici:

```text
KEY dxcc(dxcc)
KEY prefix(prefix)
```

È usata da `radio_cty_lookup()` e dal contest scorer.

### 12.5 `aux1`

Cache cluster DXCC per utente:

```text
mycall
dxcc
qso
qsl
time
PRIMARY KEY(dxcc,mycall)
```

`pproc` considera una riga fresca per 3600 secondi (`TIMEOUT_AUX1`).

Quando manca o scade:

```text
qso = count(*) sul log per DXCC
qsl = sum(lotw)+sum(eqsl)+sum(qrz)
```

La cache viene aggiornata con `REPLACE INTO aux1`.

### 12.6 `aux2` / `aux3`

Tabelle per il suggerimento callsign:

```text
aux2(callsign varchar(6), gram char(2))
aux3(callsign varchar(6), gram char(3))
```

Entrambe hanno PK `(callsign,gram)` e indice `gram`.

`pguess.cgi` usa bigrammi/trigrammi per limitare il set candidato prima di calcolare Levenshtein.

---

## 13. `pguess.cgi`: suggerimento callsign

Sorgente: `pguess.c`.

Limiti correnti:

```text
input massimo       20 caratteri
callsign candidato   6 caratteri
candidati SQL       400
output finale        50
```

Algoritmo:

1. riceve il testo raw del campo Call;
2. estrae trigrammi e bigrammi dell'input;
3. interroga `aux3` e `aux2` e somma il numero di grammi comuni;
4. prende massimo 400 candidati;
5. calcola Levenshtein esatto in C;
6. calcola distanza normalizzata `lev/max(len1,len2)`;
7. ordina per:
   - distanza normalizzata crescente;
   - Levenshtein crescente;
   - grammi comuni decrescente;
   - callsign alfabetico;
8. mostra massimo 50 pulsanti, 5 per riga.

Il click chiama `cmd4()` e copia il callsign nel campo Call.

---

## 14. `pcmd.cgi`: modifica puntuale dei QSO

Riceve:

```text
OTA,open,callsign,command
```

Prima recupera `mycall` dalla sessione OTA valida.

Comandi supportati:

```text
DEL / DELETE
FT / FREQTX
FR / FREQRX
M / MODE
ST / SIGNALTX
SR / SIGNALRX
C / CALL
DTS / DATETIMESTART
DTE / DATETIMEEND
CO / CONTEST
COT / CONTESTTX
COR / CONTESTRX
```

La riga è identificata da:

```text
mycall + callsign + open
```

Le frequenze immesse nel comando vengono moltiplicate per 1000 prima di essere salvate.

Le date devono essere nel formato gestito da `qsoz_datetime_to_epoch()`:

```text
YYYY-MM-DD HH:MM:SS
```

---

## 15. `pradio.cgi`: controllo radio

`user.radio` definisce il backend.

Formati supportati:

```text
TS890S,host,port,user,password
RIGCTLD,host,port
```

Il CGI accetta:

```text
OTA,R,...        read
OTA,S,freq:mode  set
OTA,U,1          invia udef1 (TS890S)
OTA,U,2          invia udef2 (TS890S)
```

### TS-890S

Il backend usa il protocollo TCP Kenwood e autentica la connessione:

```text
##CN;
##ID0...;
```

Lettura:

```text
FA;   frequenza
OM0;  modo
```

Scrittura:

```text
FA%011ld;
OM0x;
```

La tabella `modets890s[]` mappa i 16 codici a LSB/USB/CW/FM/AM/FSK/CW-R/FSK-R/PSK/... .

### rigctld

Lettura attuale:

```text
sfim\n
```

Il codice interpreta le righe di risposta e ricava frequenza e modo.

Scrittura:

```text
F <Hz>\n
M <mode> 0\n
```

### Nota architetturale

`pradio.c` contiene ancora proprie routine TCP (`connect_tcp`, `send_all`) invece di riusare `qsoz_net.c`. È lo stato corrente, non un errore documentale.

---

## 16. `ptime.cgi`

Senza query restituisce:

```text
Unix epoch corrente
```

Con:

```text
?release
```

restituisce:

```text
QSOZ_RELEASE
```

Il file non usa MariaDB.

---

## 17. CTY updater `pcty.cgi`

Il pulsante CTY è una funzione amministrativa riservata a **IK4LZH**.

Il CGI:

1. riceve OTA;
2. valida sintassi token;
3. apre il DB;
4. verifica che la sessione corrisponda a IK4LZH;
5. scarica:

```text
https://www.country-files.com/bigcty/download/bigcty.zip
```

6. verifica HTTP/TLS e firma ZIP `PK`;
7. legge `cty.csv` e `README.TXT` direttamente dalla memoria;
8. crea `cty_new`;
9. importa tutte le righe con prepared statement;
10. richiede almeno 20.000 prefix come controllo di integrità;
11. esegue swap atomico:

```sql
RENAME TABLE cty TO cty_old, cty_new TO cty;
```

12. elimina `cty_old`.

Limiti:

```text
ZIP download max  8 MiB
cty.csv max       16 MiB
linea CSV max     131072 byte
```

Override BigCTY gestiti nel prefix:

```text
{continent}
(CQ zone)
[ITU zone]
<latitude/longitude>
~GMT shift~
=exact-call prefix
```

In caso di errore di import viene eliminata `cty_new`; la tabella `cty` attiva non viene sostituita prima che l'import sia valido.

---

## 18. Libreria radio condivisa `work/data`

`qsoz` linka:

```text
/home/tools/mcp/work/data/libradio_data.a
/home/tools/mcp/work/data/libradio_client.a
```

### `radio_data.h`

API usate:

```c
radio_adif_extract()
radio_cty_lookup()
radio_adif_time()
radio_locator_to_latlon()
radio_distance_km()
radio_bearing_deg()
radio_locator_distance_bearing()
```

Il layer è la sorgente canonica per:

- parsing ADIF;
- lookup CTY/DXCC;
- conversione date ADIF UTC;
- locator Maidenhead 2/4/6 caratteri;
- distanza great-circle con raggio terrestre 6371 km;
- initial bearing normalizzato 0..360.

Non duplicare queste funzioni in qsoz.

### `radio_client.h`

Fornisce il client per `callbookd`. Le credenziali remote devono restare in `work/data/radio.conf`, non in `qsoz.conf`.

---

## 19. Import ADIF (`a15`)

Campi letti:

```text
call
freq
freq_rx
rst_sent
rst_rcvd
mode
time_on
time_off
stx_string
stx
srx_string
srx
contest_id
qso_date
qso_date_off
```

Comportamento:

- se `TIME_ON` o `TIME_OFF` hanno solo HHMM, aggiunge secondi `00`;
- se manca `QSO_DATE_OFF`, usa `QSO_DATE`;
- se manca `TIME_OFF`, usa `TIME_ON`;
- risolve DXCC con CTY;
- converte MHz ADIF in Hz moltiplicando per 1.000.000;
- preferisce `STX_STRING` a `STX` e `SRX_STRING` a `SRX`;
- usa `INSERT IGNORE` sul log;
- riporta QSO processati e nuovi QSO inseriti.

---

## 20. Import storico LZH (`a16`)

Formato riconosciuto per sezioni:

```text
D<date>
F<frequency>
M<mode>
<time> <callsign> [sigtx] [sigrx]
```

Se i rapporti mancano, default:

```text
59 / 59
```

La frequenza di sezione viene moltiplicata per 1000 e usata sia come TX sia come RX.

Open e close coincidono con il timestamp del record importato.

---

## 21. Import Cabrillo (`a22`)

Il parser legge `CONTEST:` una volta e poi le righe `QSO:`.

Campi principali:

```text
frequency
mode
date
time
sigtx
contesttx
callsign
sigrx
contestrx
```

Prima di inserire cerca un possibile QSO già esistente con:

```text
stesso callsign
±180 secondi
frequenza entro ±1.7 MHz
```

Se trova un QSO esistente aggiorna soltanto:

```text
contesttx
contestrx
contest
```

Se non lo trova crea il QSO.

---

## 22. Import QSL (`a17`, `a18`, `a19`)

Campi ADIF comuni:

```text
CALL
TIME_ON
QSO_DATE
```

Campo conferma per servizio:

```text
LoTW  APP_LoTW_RXQSL
eQSL  EQSL_QSLRDATE
QRZ   app_qrzlog_status
```

La conferma viene associata a un QSO con stesso callsign entro:

```text
±240 secondi
```

Colonne aggiornate:

```text
log.lotw
log.eqsl
log.qrz
```

L'output distingue:

```text
QSL processed
new QSL inserted
QSO missed
```

---

## 23. Export ADIF (`a20`)

Il payload contiene un record ADIF di controllo con:

```text
export_from
export_to
export_contest
```

Si può esportare:

- un intervallo temporale;
- oppure tutti i QSO di un contest.

Il file viene creato in:

```text
/home/www/log/files/<random>.adi
```

Campi emessi:

```text
CALL
QSO_DATE
TIME_ON
QSO_DATE_OFF
TIME_OFF
FREQ
FREQ_RX
MODE
RST_SENT
RST_RCVD
STX_STRING
SRX_STRING
CONTEST_ID
```

Header storico:

```text
<LZHlogger:9>PROGRAMID
<EOH>
```

---

## 24. Export Cabrillo (`a21`)

Crea:

```text
/home/www/log/files/<random>.cbr
```

Header base attuale:

```text
START-OF-LOG: 3.0
CREATED-BY: IK4LZH logger
CONTEST: xxxxxx
CALLSIGN: <mycall>
OPERATORS: <mycall>
CATEGORY-OPERATOR: SINGLE-OP
CATEGORY-ASSISTED: ASSISTED
CATEGORY-BAND: ALL
CATEGORY-POWER: LOW
CATEGORY-TRANSMITTER: ONE
```

Nome, indirizzo ed email sono letti da `who` per `mycall`.

Club fisso:

```text
Italian Contest Club
```

Le righe QSO usano frequenza kHz, modo normalizzato `PH/CW/DG`, timestamp UTC ed exchange TX/RX.

Il file termina con:

```text
END-OF-LOG:
```

---

## 25. Report (`a10`)

Analizza tutto il log fino a 433 MHz.

Prima sezione, per banda/modo:

```text
QSO
QSO uniq per callsign
QSO WPX uniq
QSL LoTW
eQSL
QRZ
```

Seconda sezione, per DXCC:

```text
QSO
callsign unici
WPX unici
QSL LoTW/eQSL/QRZ
country name da cty
```

L'aggregazione usa `qsoz_stats` in memoria.

---

## 26. Curio (`a11`)

Genera sei classifiche ordinate per frequenza:

```text
call
band
mode
lotw
eqsl
qrz
```

Mostra massimo `mypage` elementi per colonna.

---

## 27. Activity (`a12`)

Aggrega il log in tre granularità:

```text
anno       YYYY
mese       YYYY-MM, ultimi ~2 anni
giorno     YYYY-MM-DD, ultimo ~mese
```

Metriche:

```text
QSO
CW
DG
PH
callsign unici
WPX unici
DXCC unici
LoTW
eQSL
QRZ
```

---

## 28. DX Cluster (`a13`)

`mypage` determina il numero richiesto di spot, limitato a:

```text
1..1000
```

Per ogni spot vengono calcolati:

```text
timestamp
callsign DX
frequenza
QSO totali con quel DXCC
QSL totali con quel DXCC
QSO totali con quel callsign
QSL totali con quel callsign
tempo dall'ultimo QSO con quel callsign
spotter
```

### Cache DXCC

La cache `aux1` dura 3600 secondi.

Le statistiche callsign invece vengono calcolate direttamente da `log` con una query batch `IN (...)`.

Il pulsante a fianco dello spot richiama `cmd3()`:

- copia il callsign nel campo Call;
- copia la frequenza;
- se la radio è attiva invia la frequenza alla radio.

---

## 29. Contest list (`a27`)

Query:

```text
contest, min(open), max(open), count(callsign)
```

per ogni contest non vuoto dell'utente.

Se `conscore_supported()` riconosce il prefix del contest, la riga viene marcata `Scorable`.

Il click sul nome imposta il campo Contest.

---

## 30. Contest scoring: architettura

Implementazione: `pscore.c` / `pscore.h`.

Il contest ID viene riconosciuto per **prefix match**, non per uguaglianza esatta.

Esempio:

```text
CQWWSSB24
```

viene riconosciuto dal tipo:

```text
CQWWSSB
```

Questo permette di conservare anno/variante nel campo `log.contest` pur riusando la stessa regola.

`conscore_setup()`:

1. identifica `contype`;
2. resetta le statistiche;
3. risolve il DXCC di `mycall`;
4. carica da `cty` per ogni DXCC:
   - continente;
   - CQ zone;
   - ITU zone.

`conscore()` legge:

```text
callsign
freqtx
dxcc
contesttx
contestrx
mode
open
```

nel periodo richiesto.

### Struttura statistica usata dallo scorer

Convenzione principale di `data3[0][bucket]`:

```text
bucket 0  chiavi QSO / conteggio QSO validi
bucket 1  punti QSO
bucket 2  moltiplicatori per banda/gruppo
bucket 3  moltiplicatori globali usati nel totale
bucket 4  etichette/gruppi di output per banda/modo
bucket 5  scratch/helper per alcune regole
```

`incdata3(...,ss,dd)` permette di rappresentare un elemento unico: alla prima apparizione assegna `ss`; sui duplicati aggiunge `dd`. Molte regole contest usano `dd=0`, quindi un duplicato della stessa chiave non incrementa QSO/punti/multiplier.

`ConScore` calcola il totale corrente come:

```text
score = somma punti bucket 1 * numero moltiplicatori bucket 3
```

Per RAC, se i moltiplicatori risultano zero viene forzato 1 come comportamento esistente.

`ConGraph` richiama lo stesso scorer su finestre da 900 secondi e invia al browser:

```text
epoch, qso, points, multipliers, score
```

### Contest supportati

Sono attualmente 62:

```text
CQWWSSB
CQWWCW
CQWPXSSB
CQWPXCW
CQWWDIGI
4080
IARUHF
CQ160SSB
CQ160CW
SPDX
LZDX
OKOMSSB
OKOMCW
HADX
ARIDX
KOSSSB
KOSCW
RDAC
ARRLSSB
ARRLCW
RDXC
JIDXSSB
JIDXCW
YODX
CQM
WAESSB
WAECW
WAERTTY
CQ28
UBASSB
UBACW
IOTA
EUHF
ARISEZ
EURASIA
WAG
CQWPXRTTY
SACSSB
SACCW
PACC
AASSB
AACW
HOLYLANDDX
EUDX
UNDX
URDXC
CQBB
BSC
RRTC
UCC
PADANG
ARRL10
ARRLRU
ARRLRTTY
FTROUNDUP
RCC
ARKTIKA
9ADX
EIUKDXSSB
EIUKDXCW
RAC
ARRLFIELDDAY
```

Le regole sono codificate esplicitamente in `switch(contype)` e usano, a seconda del contest:

- DXCC;
- continente;
- CQ zone;
- ITU zone;
- banda;
- modo;
- exchange ricevuto;
- prefix WPX;
- area derivata dal callsign;
- locator/distanza;
- ora UTC;
- callsign speciali.

Commenti/limitazioni esplicite presenti nel sorgente:

```text
WAE SSB/CW/RTTY: no QTC
IOTA: no island handling completo
PACC: logica aree custom
ARRL Field Day: nessuna dichiarazione power, solo QSO points
```

Queste sono regole del codice storico del progetto; questo README non le sostituisce con regolamenti esterni più recenti.

### UBA

`pscore.c` contiene helper dedicati:

```text
uba_eu_dxcc()
uba_prefix()
uba_section()
```

Per UBA SSB/CW gestisce inoltre un bonus finale basato sul numero/proporzione di QSO belgi.

---

## 31. `qsoz_stats`: aggregatore in memoria

`qsoz_stats.c` implementa un sistema a:

```text
5 channels
400 buckets per channel
max 200000 item per bucket
label max 31 caratteri + NUL
```

Ogni bucket mantiene:

- array dinamico `Data3`;
- capacità;
- hash table open-addressing FNV-1a per lookup O(1) medio.

Le strutture vengono allocate con `qsoz_stats_init()`, riusate/reset con `qsoz_stats_reset()` e liberate con `qsoz_stats_free()`.

`qsoz_stats_sort_bucket()` ordina per label e ricostruisce l'hash per mantenere lookup corretti dopo il sort.

`cmp3()` ordina per `num` decrescente ed è usato nelle classifiche.

---

## 32. `qsoz_util`

Funzioni:

```text
qsoz_band()         MHz -> codice banda
qsoz_mode()         modo raw -> CW/DG/PH/ND
qsoz_wpx()          prefix WPX
qsoz_pacc_area()    area speciale per PACC
qsoz_min_long()
qsoz_elapsed()      secondi -> m/h/D/M/Y
qsoz_nfields()      conta token whitespace
qsoz_token_valid()  token OTA base62 lungo 16
qsoz_copy()         copia bounded
```

### WPX

Gestisce callsign normali e slash portable/reciprocal, ignorando designatori:

```text
A E J P M MM AM QRP QRPP
```

---

## 33. `qsoz_time`

Tutto il progetto usa epoch e UTC.

API:

```text
qsoz_datetime_to_epoch()
qsoz_epoch_to_datetime()
qsoz_datetime_epoch()
qsoz_date_clock_epoch()
qsoz_epoch_text()
```

Formati accettati:

```text
YYYY-MM-DD HH:MM:SS
YYYY-MM-DD + HH:MM[:SS]
YYYYMMDD + HHMM[SS]
```

La validazione ricostruisce la data via `timegm()` e controlla che non sia stata normalizzata a una data diversa.

---

## 34. `qsoz_html`

Tre encoder distinti evitano di mescolare contesti:

```text
qsoz_html_text()        testo HTML
qsoz_html_attr()        attributo HTML quoted
qsoz_html_js_sq_attr()  stringa JS single-quoted dentro attributo HTML
```

Sono usati per callsign, dati callbook, URL immagine e callback inline.

---

## 35. `qsoz_db`

`qsoz_db_escape()` è il wrapper bounded per `mysql_real_escape_string()`.

`qsoz_db_log_values()` costruisce la tupla SQL comune per inserire un QSO:

```text
mycall
callsign
mode
freqtx
freqrx
signaltx
signalrx
contesttx
contestrx
contest
dxcc
open
close
```

È riusata da Start/End e dagli importer per evitare duplicazioni di escaping e formato.

---

## 36. `qsoz_net`

Utility TCP condivise:

```text
qsoz_tcp_connect()
qsoz_send_all()
qsoz_line_reader_init()
qsoz_read_line()
```

Caratteristiche:

- IPv4/IPv6 via `getaddrinfo()`;
- connect non-blocking con `select()` e timeout;
- ripristino socket blocking dopo connessione;
- timeout RX/TX;
- gestione send parziali;
- `MSG_NOSIGNAL`;
- parser line-oriented con buffer residuo.

È usato da `pproc.cgi` per il servizio DX Cluster.

---

## 37. `qsoz_request`

Implementa il parser del protocollo POST custom di `pproc.cgi`.

Costanti:

```text
13 campi
100 byte per campo
payload max 20 MB
```

Il payload base64 viene allocato dinamicamente partendo da 4096 byte e crescendo fino al limite.

---

## 38. File-by-file inventory

### File sorgente / configurazione

#### `index.html`
UI completa, JavaScript, protocollo CGI, grafico SVG, controllo radio, import/export frontend.

#### `qsoz.css`
Stile globale, bottoni, layout split `out/out2`, input, chart SVG.

#### `Makefile`
Build di tutti i CGI e moduli condivisi. Usa `-O3 -std=gnu89 -Wall -Wextra`.

#### `qsoz_version.h`
Release globale mostrata all'utente. Corrente: `3.08`.

#### `qsoz.conf`
Configurazione privata runtime. Non è sorgente da pubblicare.

#### `qsoz_config.c`, `qsoz_config.h`
Parser config DB/callbook/cluster.

#### `qsoz_db.c`, `qsoz_db.h`
Escaping DB e builder comune QSO.

#### `qsoz_html.c`, `qsoz_html.h`
Escaping per HTML/attributi/JS inline.

#### `qsoz_net.c`, `qsoz_net.h`
TCP con timeout e line reader.

#### `qsoz_request.c`, `qsoz_request.h`
Parser 13 campi + payload Base64.

#### `qsoz_stats.c`, `qsoz_stats.h`
Aggregazione hash dinamica usata da report e scoring.

#### `qsoz_time.c`, `qsoz_time.h`
Conversioni data/epoch UTC.

#### `qsoz_util.c`, `qsoz_util.h`
Bande, modi, WPX, PACC, OTA validation e helper generali.

#### `pguess.c`
Fuzzy callsign suggestion.

#### `pcmd.c`
Editor/delete puntuale di QSO esistenti.

#### `plogin.c`
Login libsodium, password rehash e OTA.

#### `pradio.c`
Radio control TS-890S / rigctld.

#### `ptime.c`
Clock server e release endpoint.

#### `pcty.c`
Updater BigCTY amministrativo con swap atomico.

#### `pcompletion.c`
CGI amministrativo riservato a IK4LZH per ricostruire `aux2` e `aux3` da tutti i callsign validi presenti in `log` e `wc`. Normalizzazione, deduplica e generazione bigrammi/trigrammi sono eseguite in C; il DB usa tabelle staging e swap atomico finale.

#### `pft8.c`
CGI pubblico per `ft8.chaos.cc`. Analizza tutti i QSO FT8/MFSK, genera direttamente HTML/CSS/SVG senza librerie grafiche esterne e usa una cache binaria invalidata automaticamente quando cambiano `log` o `cty`.

#### `pproc.c`
CGI principale: liste, report, activity, import/export, QSO, callbook, cluster, contest.

#### `pscore.c`, `pscore.h`
Motore contest scoring 62 famiglie.

#### `deploy_qsoz_test.sh`
Install/remove symlink per ambiente `/home/www/log/qsoz`; richiede root.

### Artefatti di build

Non modificare manualmente:

```text
pcmd.cgi
pcompletion.cgi
pcty.cgi
pft8.cgi
pguess.cgi
plogin.cgi
pproc.cgi
pradio.cgi
ptime.cgi

pscore.o
qsoz_config.o
qsoz_db.o
qsoz_html.o
qsoz_net.o
qsoz_request.o
qsoz_stats.o
qsoz_time.o
qsoz_util.o

```

Vengono rigenerati dai sorgenti/Makefile o da build manuali.

### File diagnostici/storici

#### `build_auth.log`
Log di una build con warning hardening aggiuntivi e link storico `-lcrypto` su `plogin`.

#### `build_auth2.log`
Secondo log equivalente di validazione auth/build.

#### `.build_no_md5.log`
Log della build successiva senza dipendenza MD5/OpenSSL nel login; libsodium resta il meccanismo password.

Questi file descrivono build passate; **non** sostituiscono il Makefile corrente.

#### `.score_before.txt`
Baseline/regressione score storica per contest selezionati, per esempio:

```text
9ADX23       9768
AASSB21      5133
CQWPXSSB24   3925188
CQWWDIGI24   15717
RAC24        3330
...
```

Serve come riferimento per evitare variazioni involontarie del motore score.

---

## 39. Versioni correnti dei sorgenti

Le intestazioni dei file non sono uniformate deliberatamente a una sola release. Stato letto:

```text
qsoz_version.h   3.10 global release
index.html       3.03
Makefile         3.04
pproc.c          3.04
pguess.c         3.01
pcmd.c           3.01
plogin.c         3.03
pradio.c         3.01
ptime.c          3.02
pcty.c           3.0
pcompletion.c    3.0
pft8.c           3.01
pscore.c         3.02
pscore.h         3.01
qsoz_config.*    3.0
qsoz_db.*        3.01
qsoz_html.*      3.0
qsoz_net.*       3.0
qsoz_request.*   3.0
qsoz_stats.*     3.02
qsoz_time.*      3.01
qsoz_util.*      3.02
```

`qsoz_version.h` è l'unico numero da usare come release globale della UI.

---

## 40. Build

Build completa:

```sh
cd /home/tools/mcp/work/qsoz
make
```

Pulizia:

```sh
make clean
```

`make clean` elimina:

```text
*.o
*.cgi
```

Dipendenze di compilazione/runtime principali:

```text
C compiler
MariaDB client + mariadb_config
libsodium
libcurl
libzip
libm
/home/tools/mcp/work/data/libradio_data.a
/home/tools/mcp/work/data/libradio_client.a
```

Target:

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

`pproc.cgi` è il target più dipendente e linka entrambi i layer radio condivisi.

---

## 41. Deploy di test

Script:

```sh
sudo ./deploy_qsoz_test.sh install
sudo ./deploy_qsoz_test.sh remove
```

Install:

- crea `/home/www/log/qsoz`;
- crea symlink verso la directory di lavoro;
- cambia `qsoz.conf` a `640 mcp:www-data`;
- rende il sito raggiungibile sotto `/qsoz/`.

Remove:

- rimuove i symlink creati;
- prova a rimuovere la directory;
- riporta `qsoz.conf` a `600` e gruppo `mcp`.

Come già indicato, lo script corrente non gestisce `pcty.cgi`.

---

## 42. Sicurezza e invarianti importanti

### Non duplicare credenziali callbook

QRZ.com e QRZ.ru devono restare confinati in `work/data/callbookd` / `radio.conf`.

### Non bypassare l'OTA

Le azioni CGI sensibili devono continuare a verificare `user.ota` e scadenza.

### Escaping SQL

Dati utente/callsign/contest devono passare da `mysql_real_escape_string()` o wrapper `qsoz_db_escape()` prima di entrare in query costruite dinamicamente.

### Escaping HTML

Dati provenienti da DB/provider non devono essere stampati direttamente in HTML/JS quando possono contenere caratteri speciali. Usare `qsoz_html_*` secondo il contesto.

### CTY

L'aggiornamento deve restare atomico: import completo su staging prima dello swap.

### Radio shared layer

ADIF, CTY, locator, distance e bearing hanno una implementazione canonica in `work/data`; non ricopiarla nel progetto.

### qrzweb è separato

Il progetto Web Contacts QRZ sviluppato parallelamente vive in:

```text
/home/tools/mcp/work/qrzweb
```

Condivide dati MariaDB rilevanti (`log`, `who` e proprie tabelle `wc/wcsent`) ma non fa parte del build qsoz e deve restare architetturalmente separato dal logger/UI. qsoz usa `callbookd`; qrzweb gestisce invece la specifica logica Web Contacts.

---

## 43. Convenzioni di codice del progetto

Per nuovi interventi C mantenere lo stile consolidato del progetto:

- C89 / `gnu89`;
- dichiarazioni all'inizio dei blocchi;
- niente dichiarazioni dentro `for`;
- inizializzazione dopo la dichiarazione;
- preferire `for` a `while` dove sensato;
- evitare C99/C11 e POSIX se non necessari;
- usare la libreria standard o i moduli qsoz/data esistenti prima di creare helper duplicati;
- attenzione a performance e memoria;
- nessun codice/variabile inutilizzato;
- funzioni piccole e leggibili;
- commenti in inglese con `//`;
- graffa aperta sulla stessa riga;
- indentazione 2 spazi;
- stile compatto.

Quando si modifica comportamento storico delicato, prima verificare il codice reale e preservare la semantica esistente salvo decisione esplicita.

---

## 44. Aspetti noti da ricordare

Questi punti non sono automaticamente “bug da correggere”; sono caratteristiche o differenze dello stato attuale da tenere presenti prima di modificare il progetto.

1. `deploy_qsoz_test.sh` non include `pcty.cgi`, produzione sì.
2. `pradio.c` duplica parte del networking invece di usare `qsoz_net`.
3. `index.html` porta ancora il titolo storico `v2.1`, mentre la release dinamica è 3.08.
4. I numeri versione nelle intestazioni dei singoli file non coincidono con la release globale.
5. `pguess` usa ancora candidati callsign max 6 perché `aux2/aux3` hanno `callsign varchar(6)`, mentre il logger supporta callsign fino a 20.
6. `ConTX++` è uno stato locale browser (`v[22]`), non parte del filtro cluster 13-bit.
7. Le regole contest sono implementazioni storiche specifiche del progetto e includono esplicite semplificazioni per alcuni contest.
8. I file `build_auth*.log`, `.build_no_md5.log` e `.score_before.txt` sono riferimenti diagnostici/storici, non sorgenti runtime.
9. Gli export ADIF/Cabrillo scrivono file sotto `/home/www/log/files`; la gestione/retention di quei file non è implementata in qsoz.
10. Il filtro `LFind` è una `LIKE` SQL: non viene aggiunto `%` automaticamente.

---

## 45. Checklist di manutenzione

Dopo modifiche a moduli comuni:

```text
qsoz_util       -> controllare WPX e scoring
qsoz_stats      -> controllare Report, Activity, Curio, ConScore, ConGraph
qsoz_time       -> controllare Start/End, import/export, pcmd
qsoz_html       -> controllare output callbook/list/cluster
qsoz_request    -> controllare tutti i pulsanti pproc e upload file
qsoz_db         -> controllare Start/End + importer
radio_data      -> controllare CTY, ADIF, locator, scoring
radio_client    -> controllare QRZ.com/QRZ.ru e Start
pscore          -> confrontare baseline score
```

Test WPX disponibile:

```sh
./```

Per il motore contest conservare `.score_before.txt` come baseline finché non esiste una suite automatizzata più completa.

---

## 46. Principio di evoluzione

qsoz contiene logiche costruite e verificate nel tempo, soprattutto su scoring, import/export, radio e flusso operativo. La regola di manutenzione deve essere:

> preservare la strategia e la semantica che funzionano; sostituire meccanismi fragili solo quando il nuovo comportamento è misurabile e verificato.

Prima di rimuovere una funzione, una regola contest, un campo, una query o una scelta operativa storica, verificarne gli utilizzi e discuterne l'effetto. Ottimizzazioni interne sono desiderabili quando non cambiano il risultato osservabile.

---

## 47. Completion database rebuild (`a33`, `pcompletion.cgi`)

Release 3.09 aggiunge una funzione amministrativa dedicata alla ricostruzione del database di completion usato da `pguess.cgi`.

UI:

```text
a33 Completion
```

Il bottone compare dopo il login soltanto quando il callsign inserito nel login è `IK4LZH`. Questa è una comodità UI, non il controllo di sicurezza: `pcompletion.cgi` verifica nuovamente l'OTA lato server e rifiuta qualsiasi sessione che non appartenga a IK4LZH.

L'output viene mostrato nel pannello destro `out2`, come l'aggiornamento CTY.

Endpoint e sorgente unico:

```text
pcompletion.cgi
pcompletion.c
```

Non esiste un modulo separato `qsoz_completion`: la funzione appartiene esclusivamente a questo CGI amministrativo.

### Sorgenti e semantica

Vengono considerati tutti i callsign di `log` di tutti gli utenti e tutti quelli di `wc`.

La trasformazione riproduce la precedente procedura SQL:

1. trim degli spazi iniziali/finali;
2. uppercase ASCII;
3. accettazione solo se l'intero callsign normalizzato è `[A-Z0-9]+`;
4. solo dopo la validazione, conservazione dei primi 6 caratteri;
5. deduplicazione globale tra `log` e `wc`;
6. generazione di tutti i bigrammi consecutivi in `aux2`;
7. generazione di tutti i trigrammi consecutivi in `aux3`;
8. eliminazione dei grammi duplicati per lo stesso callsign.

È importante che la validazione avvenga prima del taglio a 6 caratteri: un callsign contenente `/` o altri caratteri non validi resta escluso, esattamente come nella SQL originale.

### Implementazione C

MariaDB viene usato per streaming dei callsign, insert batch, creazione indici e swap finale. Normalizzazione, validazione, deduplica e generazione dei grammi avvengono in C.

I callsign normalizzati sono conservati in una hash table open-addressing dinamica. Gli insert dei grammi vengono aggregati in query batch con buffer fino a circa 1 MiB.

Le tabelle staging vengono create con `CREATE TABLE ... LIKE`, poi gli indici vengono temporaneamente rimossi durante il caricamento e ricreati soltanto a fine inserimento:

```text
PRIMARY KEY(callsign,gram)
KEY gram(gram)
```

### Staging e swap atomico

Il rebuild non esegue più `TRUNCATE` sulle tabelle di produzione attive.

Flusso:

```text
log + wc
   |
   v
hash callsign unica in RAM
   |
   v
aux2_new / aux3_new senza indici
   |
   v
insert batch bigrammi / trigrammi
   |
   v
creazione PK + indice gram
   |
   v
validazione COUNT(*)
   |
   v
RENAME TABLE atomico
```

Lo swap finale è unico:

```sql
RENAME TABLE
  aux2 TO aux2_old,
  aux2_new TO aux2,
  aux3 TO aux3_old,
  aux3_new TO aux3;
```

Fino a quel momento `pguess.cgi` continua a utilizzare le vecchie `aux2/aux3`. Se il rebuild fallisce prima dello swap, le tabelle attive restano intatte.

Dopo uno swap riuscito `aux2_old/aux3_old` vengono eliminate.

### Lock amministrativo

Il CGI acquisisce l'advisory lock MariaDB:

```text
qsoz_completion_rebuild
```

con timeout zero. Due rebuild non possono quindi essere eseguiti contemporaneamente.

### Validazione reale del 18 agosto 2026

Il nuovo algoritmo C è stato eseguito sul database reale e confrontato con query read-only equivalenti alla procedura SQL originale.

Risultato C:

```text
log rows scanned:    1210077
wc rows scanned:      243762
valid source rows:   1427110
unique callsigns:     239966
aux2 bigrams:        1064138
aux3 trigrams:        825541
elapsed:              circa 10 secondi
```

Riferimento SQL indipendente:

```text
calls       239966
aux2_ref   1064138
aux3_ref    825541
```

I conteggi coincidono esattamente.

### Relazione con `pguess.cgi`

`pguess.cgi` non è stato modificato. Continua a usare `aux3` per i trigrammi e `aux2` per i bigrammi, seleziona fino a 400 candidati e completa l'ordinamento fuzzy con Levenshtein in C.


---

## FT8 symmetricity CGI

Il progetto contiene anche `pft8.c`, compilato come `pft8.cgi`, che sostituisce la precedente pagina PHP `ft8.chaos.cc` basata su `symmetricity.php`, `utility.php` e Google Charts.

Il CGI è autonomo lato presentazione: genera direttamente HTML, CSS e SVG e non carica librerie JavaScript o grafiche esterne. Usa soltanto MariaDB e `libm` in fase di link.

Analizza globalmente i QSO `mode='FT8'` o `mode='MFSK'` di tutti gli utenti. Sono inclusi nella statistica soltanto record con `signaltx`, `signalrx` e delta `signaltx-signalrx` tutti nell'intervallo `-35..+35 dB`. Il denominatore della distribuzione, media e deviazione standard usa esattamente lo stesso insieme di QSO visualizzato.

Le sezioni prodotte sono:

- PDF di `TX-RX` per banda 160/80/60/40/30/20/17/15/12/10 metri e totale;
- QSO, media e deviazione standard per banda e totale;
- andamento temporale per CQ zone, mantenendo il bucket temporale storico della precedente implementazione PHP.

### Cache FT8

Una scansione completa interessa oltre un milione di QSO FT8/MFSK e richiede circa 2.6 secondi sul database corrente. Per evitare di ripeterla a ogni accesso, `pft8.cgi` mantiene una cache binaria runtime:

```text
/home/www/ft8/.pft8.cache
```

La cache contiene soltanto gli aggregati già calcolati, non l'HTML. La chiave di validità comprende:

```text
UPDATE_TIME di log
UPDATE_TIME di cty
MAX(log.open)
stima TABLE_ROWS di log
```

Se uno di questi valori cambia il CGI rifà l'analisi completa e sostituisce la cache atomicamente. Se non cambia, carica direttamente gli aggregati e genera gli SVG senza scandire `log`.

Misure effettuate sul deployment reale il 19 agosto 2026:

```text
prima richiesta, cache assente: circa 2.66 s
richiesta successiva, cache valida: circa 0.016 s
cache binaria: circa 43 KB
HTML/SVG non compresso: circa 404 KB
risposta HTTP gzip: circa 46 KB
```

Apache ha già `mod_deflate` attivo e invia `Content-Encoding: gzip`, quindi non è necessario comprimere manualmente l'output nel CGI.

### Deployment reale `ft8.chaos.cc`

Il CGI è esposto tramite:

```text
/home/www/ft8/pft8.cgi -> /home/tools/mcp/work/qsoz/pft8.cgi
```

Il virtual host HTTPS è configurato con `pft8.cgi` come unico file di indice predefinito e con esecuzione CGI esplicita:

```apache
DocumentRoot /home/www/ft8
DirectoryIndex pft8.cgi
AddHandler cgi-script .cgi

<Directory /home/www/ft8>
  Options +ExecCGI -Indexes -MultiViews
  Require all granted
</Directory>
```

Il precedente handler PHP non è più necessario per la pagina di default. I vecchi symlink `symmetricity.php`, `utility.php` e `local.php` possono essere rimossi separatamente quando non servono più; `pft8.cgi` non dipende da essi.

La pagina pubblica corrente è quindi:

```text
https://ft8.chaos.cc/
```
