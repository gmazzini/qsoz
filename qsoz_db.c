// Gianluca Mazzini @2022- Version 3.01
#include <stdio.h>
#include <string.h>
#include "qsoz_db.h"

int qsoz_db_escape(MYSQL *con,char *dst,unsigned long cap,const char *src) {
  unsigned long len,n;

  if(con==NULL || dst==NULL || cap==0 || src==NULL)return 0;
  len=(unsigned long)strlen(src);
  if(len>(cap-1UL)/2UL)return 0;
  n=mysql_real_escape_string(con,dst,src,len);
  if(n>=cap)return 0;
  dst[n]='\0';
  return 1;
}

int qsoz_db_log_values(MYSQL *con,char *dst,unsigned long cap,
                       const char *mycall,const char *callsign,const char *mode,
                       long freqtx,long freqrx,const char *signaltx,const char *signalrx,
                       const char *contesttx,const char *contestrx,const char *contest,
                       int dxcc,long long open_epoch,long long close_epoch) {
  char emycall[64],ecall[256],emode[128],estx[128],esrx[128];
  char ecotx[128],ecorx[128],econtest[256],tmp[2048];
  int n;

  if(dst==NULL || cap==0)return 0;
  if(!qsoz_db_escape(con,emycall,sizeof(emycall),mycall) ||
     !qsoz_db_escape(con,ecall,sizeof(ecall),callsign) ||
     !qsoz_db_escape(con,emode,sizeof(emode),mode) ||
     !qsoz_db_escape(con,estx,sizeof(estx),signaltx) ||
     !qsoz_db_escape(con,esrx,sizeof(esrx),signalrx) ||
     !qsoz_db_escape(con,ecotx,sizeof(ecotx),contesttx) ||
     !qsoz_db_escape(con,ecorx,sizeof(ecorx),contestrx) ||
     !qsoz_db_escape(con,econtest,sizeof(econtest),contest))return 0;
  n=snprintf(tmp,sizeof(tmp),"('%s','%s','%s',%ld,%ld,'%s','%s','%s','%s','%s',%d,%lld,%lld)",
             emycall,ecall,emode,freqtx,freqrx,estx,esrx,ecotx,ecorx,econtest,dxcc,open_epoch,close_epoch);
  if(n<0 || (unsigned long)n>=sizeof(tmp) || (unsigned long)n>=cap)return 0;
  memcpy(dst,tmp,(size_t)n+1U);
  return 1;
}
