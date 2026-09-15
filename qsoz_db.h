// Gianluca Mazzini @2022- Version 3.01
#ifndef QSOZ_DB_H
#define QSOZ_DB_H

#include <mysql/mysql.h>

int qsoz_db_escape(MYSQL *con,char *dst,unsigned long cap,const char *src);
int qsoz_db_log_values(MYSQL *con,char *dst,unsigned long cap,
                       const char *mycall,const char *callsign,const char *mode,
                       long freqtx,long freqrx,const char *signaltx,const char *signalrx,
                       const char *contesttx,const char *contestrx,const char *contest,
                       int dxcc,long long open_epoch,long long close_epoch);

#endif
