// Gianluca Mazzini @2022- Version 4.12
#ifndef QSOZ_DB_H
#define QSOZ_DB_H

#include <sqlite3.h>

#define QSOZ_LOG_DB "/home/tools/mcp/work/qsoz/log.db"

typedef struct QsozDb QsozDb;
typedef struct QsozResult QsozResult;
typedef char **QsozRow;

QsozDb *qsoz_db_open(void);
void qsoz_db_close(QsozDb *db);
int qsoz_db_query(QsozDb *db,const char *sql);
QsozResult *qsoz_db_result(QsozDb *db);
QsozRow qsoz_db_fetch(QsozResult *res);
void qsoz_db_result_free(QsozResult *res);
unsigned int qsoz_db_errno(QsozDb *db);
const char *qsoz_db_error(QsozDb *db);
long long qsoz_db_changes(QsozDb *db);
unsigned long qsoz_db_escape_raw(char *dst,const char *src,unsigned long len);
int qsoz_db_escape(QsozDb *db,char *dst,unsigned long cap,const char *src);
int qsoz_db_log_values(QsozDb *db,char *dst,unsigned long cap,
                       const char *mycall,const char *callsign,const char *mode,
                       long freqtx,long freqrx,const char *signaltx,const char *signalrx,
                       const char *contesttx,const char *contestrx,const char *contest,
                       int dxcc,long long open_epoch,long long close_epoch);

#endif
