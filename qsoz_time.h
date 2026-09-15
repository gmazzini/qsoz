// Gianluca Mazzini @2022- Version 3.01
#ifndef QSOZ_TIME_H
#define QSOZ_TIME_H

#include <time.h>

int qsoz_datetime_to_epoch(const char *datetime,time_t *epoch);
int qsoz_epoch_to_datetime(time_t epoch,char *out,unsigned long cap);
time_t qsoz_datetime_epoch(const char *datetime);
time_t qsoz_date_clock_epoch(const char *date,const char *clock);
char *qsoz_epoch_text(time_t epoch);

#endif
