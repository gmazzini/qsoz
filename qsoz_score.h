// Gianluca Mazzini @2022- Version 4.12
#ifndef QSOZ_SCORE_H
#define QSOZ_SCORE_H

#include "qsoz_db.h"

int conscore_supported(const char *contest);
void conscore_setup(char tok[][100],char *mycall);
void conscore(QsozDb *con,char tok[][100],char *mycall,long long start,long long end);

#endif
