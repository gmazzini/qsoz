// Gianluca Mazzini @2022- Version 4.12
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include "qsoz_db.h"
#include <sqlite3.h>
#include "qsoz_completion.h"
#include "qsoz_util.h"
#include "qsoz_user.h"

#define CALL_SIZE 7
#define INITIAL_CAPACITY 1024UL
#define LOAD_NUM 7UL
#define LOAD_DEN 10UL
#define QSOZ_GRAPH_DB "/home/tools/mcp/work/qrzweb/graph.db"

typedef struct {
  char call[CALL_SIZE];
} CompletionSlot;

typedef struct {
  CompletionSlot *slot;
  unsigned long cap;
  unsigned long count;
} CompletionSet;

typedef struct {
  unsigned long log_rows;
  unsigned long graph_rows;
  unsigned long valid_rows;
  unsigned long calls;
  unsigned long bigrams;
  unsigned long trigrams;
} CompletionStats;

static void set_error(char *err,unsigned long cap,const char *text) {
  unsigned long n;

  if(err==NULL || cap==0)return;
  n=(unsigned long)strlen(text);
  if(n>=cap)n=cap-1UL;
  memcpy(err,text,(size_t)n);
  err[n]='\0';
}

static void set_sqlite_error(sqlite3 *db,char *err,unsigned long cap,const char *prefix) {
  char tmp[512];

  snprintf(tmp,sizeof(tmp),"%s: %s",prefix,db==NULL?"unknown SQLite error":sqlite3_errmsg(db));
  set_error(err,cap,tmp);
}

static unsigned long call_hash(const char *s) {
  unsigned long h;

  h=2166136261UL;
  for(;*s!='\0';s++) {
    h^=(unsigned char)*s;
    h*=16777619UL;
  }
  return h;
}

static int set_rehash(CompletionSet *set,unsigned long cap) {
  CompletionSlot *slot;
  unsigned long i,pos,mask;

  slot=(CompletionSlot *)calloc((size_t)cap,sizeof(CompletionSlot));
  if(slot==NULL)return 0;
  mask=cap-1UL;
  for(i=0;i<set->cap;i++) {
    if(set->slot[i].call[0]=='\0')continue;
    pos=call_hash(set->slot[i].call)&mask;
    for(;slot[pos].call[0]!='\0';pos=(pos+1UL)&mask) {
    }
    memcpy(slot[pos].call,set->slot[i].call,CALL_SIZE);
  }
  free(set->slot);
  set->slot=slot;
  set->cap=cap;
  return 1;
}

static int set_add(CompletionSet *set,const char *call) {
  unsigned long pos,mask;

  if(set->cap==0) {
    if(!set_rehash(set,INITIAL_CAPACITY))return 0;
  } else if((set->count+1UL)*LOAD_DEN>=set->cap*LOAD_NUM) {
    if(set->cap>((unsigned long)-1)/2UL)return 0;
    if(!set_rehash(set,set->cap*2UL))return 0;
  }
  mask=set->cap-1UL;
  pos=call_hash(call)&mask;
  for(;set->slot[pos].call[0]!='\0';pos=(pos+1UL)&mask) {
    if(strcmp(set->slot[pos].call,call)==0)return 1;
  }
  memcpy(set->slot[pos].call,call,strlen(call)+1U);
  set->count++;
  return 1;
}

static int normalize_call(const char *src,char out[CALL_SIZE]) {
  const char *p,*end;
  unsigned long n;
  unsigned char c;

  if(src==NULL)return 0;
  p=src;
  end=src+strlen(src);
  for(;p<end && *p==' ';p++) {
  }
  for(;end>p && end[-1]==' ';end--) {
  }
  if(p==end)return 0;
  n=0;
  for(;p<end;p++) {
    c=(unsigned char)*p;
    if(c>='a' && c<='z')c=(unsigned char)(c-'a'+'A');
    else if(!((c>='A' && c<='Z') || (c>='0' && c<='9')))return 0;
    if(n<CALL_SIZE-1UL)out[n++]=(char)c;
  }
  out[n]='\0';
  return n>0;
}

static int load_calls(sqlite3 *db,CompletionSet *set,const char *table,unsigned long *scanned,
                      unsigned long *valid,char *err,unsigned long errcap) {
  sqlite3_stmt *stmt;
  const unsigned char *text;
  char query[256],call[CALL_SIZE];
  int rc;

  snprintf(query,sizeof(query),"select callsign from %s where callsign is not null and callsign<>''",table);
  stmt=NULL;
  rc=sqlite3_prepare_v2(db,query,-1,&stmt,NULL);
  if(rc!=SQLITE_OK) {
    set_sqlite_error(db,err,errcap,"completion source query failed");
    return 0;
  }
  for(;;) {
    rc=sqlite3_step(stmt);
    if(rc==SQLITE_DONE)break;
    if(rc!=SQLITE_ROW) {
      set_sqlite_error(db,err,errcap,"completion source read failed");
      sqlite3_finalize(stmt);
      return 0;
    }
    text=sqlite3_column_text(stmt,0);
    (*scanned)++;
    if(text==NULL || !normalize_call((const char *)text,call))continue;
    (*valid)++;
    if(!set_add(set,call)) {
      sqlite3_finalize(stmt);
      set_error(err,errcap,"completion callsign hash allocation failed");
      return 0;
    }
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int lock_rebuild(char *err,unsigned long errcap) {
  int fd;

  fd=open(QSOZ_COMPLETION_LOCK,O_CREAT|O_RDWR,0644);
  if(fd<0) {
    set_error(err,errcap,"cannot open completion rebuild lock");
    return -1;
  }
  if(flock(fd,LOCK_EX|LOCK_NB)!=0) {
    if(errno==EWOULDBLOCK)set_error(err,errcap,"completion rebuild is already running");
    else set_error(err,errcap,"cannot lock completion rebuild");
    close(fd);
    return -1;
  }
  return fd;
}

static int gram_seen(char seen[][4],int count,const char *gram) {
  int i;

  for(i=0;i<count;i++)if(strcmp(seen[i],gram)==0)return 1;
  return 0;
}

static int insert_grams(sqlite3 *db,const CompletionSet *set,const char *table,int gram_len,
                        unsigned long *rows,char *err,unsigned long errcap) {
  sqlite3_stmt *stmt;
  char sql[128],gram[4],seen[5][4];
  unsigned long i,n;
  int pos,seen_count,rc;

  snprintf(sql,sizeof(sql),"insert into %s(callsign,gram) values(?,?)",table);
  stmt=NULL;
  rc=sqlite3_prepare_v2(db,sql,-1,&stmt,NULL);
  if(rc!=SQLITE_OK) {
    set_sqlite_error(db,err,errcap,"cannot prepare completion insert");
    return 0;
  }
  *rows=0;
  for(i=0;i<set->cap;i++) {
    if(set->slot[i].call[0]=='\0')continue;
    n=(unsigned long)strlen(set->slot[i].call);
    if(n<(unsigned long)gram_len)continue;
    seen_count=0;
    for(pos=0;pos<=((int)n-gram_len);pos++) {
      memcpy(gram,set->slot[i].call+pos,(size_t)gram_len);
      gram[gram_len]='\0';
      if(gram_seen(seen,seen_count,gram))continue;
      memcpy(seen[seen_count],gram,(size_t)gram_len+1U);
      seen_count++;
      sqlite3_bind_text(stmt,1,set->slot[i].call,-1,SQLITE_STATIC);
      sqlite3_bind_text(stmt,2,gram,-1,SQLITE_TRANSIENT);
      rc=sqlite3_step(stmt);
      if(rc!=SQLITE_DONE) {
        set_sqlite_error(db,err,errcap,"completion insert failed");
        sqlite3_finalize(stmt);
        return 0;
      }
      sqlite3_reset(stmt);
      sqlite3_clear_bindings(stmt);
      (*rows)++;
    }
  }
  sqlite3_finalize(stmt);
  return 1;
}

static int sqlite_exec(sqlite3 *db,const char *sql,char *err,unsigned long errcap,const char *what) {
  char *msg;
  int rc;

  msg=NULL;
  rc=sqlite3_exec(db,sql,NULL,NULL,&msg);
  if(rc==SQLITE_OK)return 1;
  if(msg!=NULL) {
    char tmp[512];

    snprintf(tmp,sizeof(tmp),"%s: %s",what,msg);
    set_error(err,errcap,tmp);
    sqlite3_free(msg);
  } else set_sqlite_error(db,err,errcap,what);
  return 0;
}

static int sqlite_quick_check(sqlite3 *db,char *err,unsigned long errcap) {
  sqlite3_stmt *stmt;
  const unsigned char *text;
  int rc,ok;

  stmt=NULL;
  rc=sqlite3_prepare_v2(db,"pragma quick_check",-1,&stmt,NULL);
  if(rc!=SQLITE_OK) {
    set_sqlite_error(db,err,errcap,"completion integrity check prepare failed");
    return 0;
  }
  rc=sqlite3_step(stmt);
  text=rc==SQLITE_ROW?sqlite3_column_text(stmt,0):NULL;
  ok=text!=NULL && strcmp((const char *)text,"ok")==0;
  if(!ok)set_sqlite_error(db,err,errcap,"completion integrity check failed");
  sqlite3_finalize(stmt);
  return ok;
}

static int sqlite_count(sqlite3 *db,const char *table,unsigned long *count,char *err,unsigned long errcap) {
  sqlite3_stmt *stmt;
  char sql[128];
  int rc;

  snprintf(sql,sizeof(sql),"select count(*) from %s",table);
  stmt=NULL;
  rc=sqlite3_prepare_v2(db,sql,-1,&stmt,NULL);
  if(rc!=SQLITE_OK) {
    set_sqlite_error(db,err,errcap,"completion validation prepare failed");
    return 0;
  }
  rc=sqlite3_step(stmt);
  if(rc!=SQLITE_ROW) {
    set_sqlite_error(db,err,errcap,"completion validation failed");
    sqlite3_finalize(stmt);
    return 0;
  }
  *count=(unsigned long)sqlite3_column_int64(stmt,0);
  sqlite3_finalize(stmt);
  return 1;
}

static int build_sqlite(const CompletionSet *set,CompletionStats *stats,char *err,unsigned long errcap) {
  sqlite3 *db;
  char path[512];
  unsigned long check2,check3;
  int rc,ok;

  db=NULL;
  ok=0;
  snprintf(path,sizeof(path),"%s.new.%ld",QSOZ_COMPLETION_DB,(long)getpid());
  unlink(path);
  rc=sqlite3_open_v2(path,&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,NULL);
  if(rc!=SQLITE_OK) {
    set_sqlite_error(db,err,errcap,"cannot create completion database");
    if(db!=NULL)sqlite3_close(db);
    unlink(path);
    return 0;
  }
  if(!sqlite_exec(db,"pragma journal_mode=off; pragma synchronous=full; pragma temp_store=memory;",err,errcap,"cannot configure completion database"))goto end;
  if(!sqlite_exec(db,"create table bigram(callsign text not null,gram text not null,primary key(callsign,gram)) without rowid; create table trigram(callsign text not null,gram text not null,primary key(callsign,gram)) without rowid;",err,errcap,"cannot create completion tables"))goto end;
  if(!sqlite_exec(db,"begin immediate",err,errcap,"cannot start completion build"))goto end;
  if(!insert_grams(db,set,"bigram",2,&stats->bigrams,err,errcap))goto rollback;
  if(!insert_grams(db,set,"trigram",3,&stats->trigrams,err,errcap))goto rollback;
  if(!sqlite_exec(db,"commit",err,errcap,"cannot commit completion data"))goto end;
  if(!sqlite_exec(db,"create index bigram_gram on bigram(gram,callsign); create index trigram_gram on trigram(gram,callsign);",err,errcap,"cannot index completion database"))goto end;
  check2=0;
  check3=0;
  if(!sqlite_count(db,"bigram",&check2,err,errcap) || !sqlite_count(db,"trigram",&check3,err,errcap))goto end;
  if(check2!=stats->bigrams || check3!=stats->trigrams) {
    set_error(err,errcap,"completion database row count mismatch");
    goto end;
  }
  if(!sqlite_exec(db,"pragma optimize",err,errcap,"cannot optimize completion database"))goto end;
  if(!sqlite_quick_check(db,err,errcap))goto end;
  if(sqlite3_close(db)!=SQLITE_OK) {
    db=NULL;
    set_error(err,errcap,"cannot close completion database");
    goto cleanup;
  }
  db=NULL;
  if(rename(path,QSOZ_COMPLETION_DB)!=0) {
    set_error(err,errcap,"cannot install completion database");
    goto cleanup;
  }
  ok=1;
  goto cleanup;

rollback:
  sqlite3_exec(db,"rollback",NULL,NULL,NULL);
end:
  if(db!=NULL)sqlite3_close(db);
cleanup:
  if(!ok)unlink(path);
  return ok;
}

static int completion_rebuild(CompletionStats *stats,char *err,unsigned long errcap) {
  CompletionSet set;
  sqlite3 *logdb,*graphdb;
  int lockfd,ok,rc;

  memset(&set,0,sizeof(set));
  memset(stats,0,sizeof(*stats));
  err[0]='\0';
  ok=0;
  logdb=NULL;
  graphdb=NULL;
  rc=sqlite3_open_v2(QSOZ_LOG_DB,&logdb,SQLITE_OPEN_READONLY,NULL);
  if(rc!=SQLITE_OK) {set_sqlite_error(logdb,err,errcap,"cannot open log database"); goto end_db;}
  rc=sqlite3_open_v2(QSOZ_GRAPH_DB,&graphdb,SQLITE_OPEN_READONLY,NULL);
  if(rc!=SQLITE_OK) {set_sqlite_error(graphdb,err,errcap,"cannot open graph database"); goto end_db;}
  sqlite3_busy_timeout(logdb,5000);
  sqlite3_busy_timeout(graphdb,5000);
  lockfd=lock_rebuild(err,errcap);
  if(lockfd<0)goto end_db;
  ok=0;
  if(!load_calls(logdb,&set,"log",&stats->log_rows,&stats->valid_rows,err,errcap))goto end;
  if(!load_calls(graphdb,&set,"contacts",&stats->graph_rows,&stats->valid_rows,err,errcap))goto end;
  if(set.count==0) {
    set_error(err,errcap,"completion source contains no valid callsigns");
    goto end;
  }
  stats->calls=set.count;
  if(!build_sqlite(&set,stats,err,errcap))goto end;
  ok=1;

end:
  free(set.slot);
  flock(lockfd,LOCK_UN);
  close(lockfd);
end_db:
  if(graphdb!=NULL)sqlite3_close(graphdb);
  if(logdb!=NULL)sqlite3_close(logdb);
  return ok;
}

static int read_ota(char *ota,unsigned long cap) {
  size_t n;

  n=fread(ota,1,(size_t)cap-1U,stdin);
  if(ferror(stdin))return 0;
  if(!feof(stdin) && n==(size_t)cap-1U)return 0;
  ota[n]='\0';
  for(;n>0 && (ota[n-1]=='\r' || ota[n-1]=='\n' || ota[n-1]==' ' || ota[n-1]=='\t');n--)ota[n-1]='\0';
  return qsoz_token_valid(ota);
}

int qsoz_completion_main(void) {
  CompletionStats stats;
  char ota[64],err[512];

  err[0]='\0';
  printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--33-->");
  if(!read_ota(ota,sizeof(ota))) {
    printf("<pre><b>Login expired</b></pre>");
    return 0;
  }
  if(!qsoz_user_admin(ota)) {
    set_error(err,sizeof(err),"completion rebuild is restricted to IK4LZH");
    goto fail;
  }
  if(!completion_rebuild(&stats,err,sizeof(err)))goto fail;

  printf("<pre><b>Completion updated</b>\n");
  printf("log rows scanned: %lu\n",stats.log_rows);
  printf("graph rows scanned: %lu\n",stats.graph_rows);
  printf("valid source rows: %lu\n",stats.valid_rows);
  printf("unique callsigns: %lu\n",stats.calls);
  printf("bigrams: %lu\n",stats.bigrams);
  printf("trigrams: %lu\n",stats.trigrams);
  printf("</pre>");
  return 0;

fail:
  printf("<pre><b>Completion update failed</b>\n%s\n</pre>",err[0]?err:"unknown error");
  return 0;
}
