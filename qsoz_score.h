// Gianluca Mazzini @2022- Version 4.1
#ifndef QSOZ_SCORE_H
#define QSOZ_SCORE_H

#include <mysql/mysql.h>

int conscore_supported(const char *contest);
void conscore_setup(MYSQL *con,char tok[][100],char *mycall);
void conscore(MYSQL *con,char tok[][100],char *mycall,long long start,long long end);

#endif
