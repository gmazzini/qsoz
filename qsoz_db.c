// Gianluca Mazzini @2022- Version 4.12
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qsoz_db.h"

struct QsozDb {
  sqlite3 *db;
  sqlite3_stmt *pending;
  char error[512];
  unsigned int errcode;
  long long changes;
};

struct QsozResult {
  QsozDb *owner;
  sqlite3_stmt *stmt;
  char **row;
  int columns;
};

static void set_db_error(QsozDb *db,int rc) {
  const char *text;

  if(db==NULL)return;
  db->errcode=(unsigned int)rc;
  text=db->db!=NULL?sqlite3_errmsg(db->db):"SQLite error";
  snprintf(db->error,sizeof(db->error),"%s",text);
}

QsozDb *qsoz_db_open(void) {
  QsozDb *db;
  int rc;

  db=(QsozDb *)calloc(1,sizeof(*db));
  if(db==NULL)return NULL;
  rc=sqlite3_open_v2(QSOZ_LOG_DB,&db->db,SQLITE_OPEN_READWRITE,NULL);
  if(rc!=SQLITE_OK) {
    set_db_error(db,rc);
    if(db->db!=NULL)sqlite3_close(db->db);
    free(db);
    return NULL;
  }
  sqlite3_busy_timeout(db->db,5000);
  return db;
}

void qsoz_db_close(QsozDb *db) {
  if(db==NULL)return;
  if(db->pending!=NULL)sqlite3_finalize(db->pending);
  if(db->db!=NULL)sqlite3_close(db->db);
  free(db);
}

int qsoz_db_query(QsozDb *db,const char *sql) {
  sqlite3_stmt *stmt;
  int rc;

  if(db==NULL || db->db==NULL || sql==NULL)return 1;
  if(db->pending!=NULL) {
    sqlite3_finalize(db->pending);
    db->pending=NULL;
  }
  db->error[0]='\0';
  db->errcode=0;
  db->changes=0;
  stmt=NULL;
  rc=sqlite3_prepare_v2(db->db,sql,-1,&stmt,NULL);
  if(rc!=SQLITE_OK) {
    set_db_error(db,rc);
    if(stmt!=NULL)sqlite3_finalize(stmt);
    return 1;
  }
  if(sqlite3_column_count(stmt)>0) {
    db->pending=stmt;
    return 0;
  }
  rc=sqlite3_step(stmt);
  if(rc!=SQLITE_DONE) {
    set_db_error(db,rc);
    sqlite3_finalize(stmt);
    return 1;
  }
  db->changes=(long long)sqlite3_changes(db->db);
  sqlite3_finalize(stmt);
  return 0;
}

QsozResult *qsoz_db_result(QsozDb *db) {
  QsozResult *res;

  if(db==NULL || db->pending==NULL)return NULL;
  res=(QsozResult *)calloc(1,sizeof(*res));
  if(res==NULL) {
    sqlite3_finalize(db->pending);
    db->pending=NULL;
    db->errcode=(unsigned int)SQLITE_NOMEM;
    snprintf(db->error,sizeof(db->error),"out of memory");
    return NULL;
  }
  res->owner=db;
  res->stmt=db->pending;
  db->pending=NULL;
  res->columns=sqlite3_column_count(res->stmt);
  res->row=(char **)calloc((size_t)res->columns+1U,sizeof(char *));
  if(res->row==NULL) {
    sqlite3_finalize(res->stmt);
    free(res);
    db->errcode=(unsigned int)SQLITE_NOMEM;
    snprintf(db->error,sizeof(db->error),"out of memory");
    return NULL;
  }
  return res;
}

QsozRow qsoz_db_fetch(QsozResult *res) {
  const unsigned char *text;
  int i,rc;

  if(res==NULL || res->stmt==NULL)return NULL;
  rc=sqlite3_step(res->stmt);
  if(rc==SQLITE_DONE)return NULL;
  if(rc!=SQLITE_ROW) {
    set_db_error(res->owner,rc);
    return NULL;
  }
  for(i=0;i<res->columns;i++) {
    text=sqlite3_column_text(res->stmt,i);
    res->row[i]=(char *)text;
  }
  res->row[res->columns]=NULL;
  return res->row;
}

void qsoz_db_result_free(QsozResult *res) {
  if(res==NULL)return;
  if(res->stmt!=NULL)sqlite3_finalize(res->stmt);
  free(res->row);
  free(res);
}

unsigned int qsoz_db_errno(QsozDb *db) {
  return db!=NULL?db->errcode:1U;
}

const char *qsoz_db_error(QsozDb *db) {
  return db!=NULL?db->error:"database unavailable";
}

long long qsoz_db_changes(QsozDb *db) {
  return db!=NULL?db->changes:-1LL;
}

unsigned long qsoz_db_escape_raw(char *dst,const char *src,unsigned long len) {
  unsigned long i,n;

  n=0;
  for(i=0;i<len;i++) {
    if(src[i]=='\'')dst[n++]='\'';
    dst[n++]=src[i];
  }
  dst[n]='\0';
  return n;
}

int qsoz_db_escape(QsozDb *db,char *dst,unsigned long cap,const char *src) {
  unsigned long len,n;

  (void)db;
  if(dst==NULL || cap==0 || src==NULL)return 0;
  len=(unsigned long)strlen(src);
  if(len>(cap-1UL)/2UL)return 0;
  n=qsoz_db_escape_raw(dst,src,len);
  if(n>=cap)return 0;
  return 1;
}

int qsoz_db_log_values(QsozDb *db,char *dst,unsigned long cap,
                       const char *mycall,const char *callsign,const char *mode,
                       long freqtx,long freqrx,const char *signaltx,const char *signalrx,
                       const char *contesttx,const char *contestrx,const char *contest,
                       int dxcc,long long open_epoch,long long close_epoch) {
  char emycall[64],ecall[256],emode[128],estx[128],esrx[128];
  char ecotx[128],ecorx[128],econtest[256],tmp[2048];
  int n;

  if(dst==NULL || cap==0)return 0;
  if(!qsoz_db_escape(db,emycall,sizeof(emycall),mycall) ||
     !qsoz_db_escape(db,ecall,sizeof(ecall),callsign) ||
     !qsoz_db_escape(db,emode,sizeof(emode),mode) ||
     !qsoz_db_escape(db,estx,sizeof(estx),signaltx) ||
     !qsoz_db_escape(db,esrx,sizeof(esrx),signalrx) ||
     !qsoz_db_escape(db,ecotx,sizeof(ecotx),contesttx) ||
     !qsoz_db_escape(db,ecorx,sizeof(ecorx),contestrx) ||
     !qsoz_db_escape(db,econtest,sizeof(econtest),contest))return 0;
  n=snprintf(tmp,sizeof(tmp),"('%s','%s','%s',%ld,%ld,'%s','%s','%s','%s','%s',%d,%lld,%lld)",
             emycall,ecall,emode,freqtx,freqrx,estx,esrx,ecotx,ecorx,econtest,dxcc,open_epoch,close_epoch);
  if(n<0 || (unsigned long)n>=sizeof(tmp) || (unsigned long)n>=cap)return 0;
  memcpy(dst,tmp,(size_t)n+1U);
  return 1;
}
