// Gianluca Mazzini @2022- Version 3.02
#ifndef QSOZ_UTIL_H
#define QSOZ_UTIL_H

int qsoz_band(int mhz);
const char *qsoz_mode(const char *mode);
const char *qsoz_wpx(const char *callsign);
const char *qsoz_pacc_area(const char *callsign,int dxcc);
long qsoz_min_long(long a,long b);
const char *qsoz_elapsed(long seconds);
int qsoz_nfields(const char *s);
int qsoz_token_valid(const char *token);
int qsoz_copy(char *dst,unsigned long cap,const char *src);

#endif
