// Gianluca Mazzini @2022- Version 3.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mysql/mysql.h>
#include "qsoz_config.h"
#include "qsoz_util.h"

#define CALL_SIZE 7
#define INITIAL_CAPACITY 1024UL
#define LOAD_NUM 7UL
#define LOAD_DEN 10UL
#define SQL_BUFFER (1024UL*1024UL)

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
  unsigned long wc_rows;
  unsigned long valid_rows;
  unsigned long calls;
  unsigned long bigrams;
  unsigned long trigrams;
} CompletionStats;

static void set_error(char *err,unsigned long cap,const char *text) {
  unsigned long n;

  if(err==NULL || cap==0)return;
  n=(unsigned long)strlen(text);
  if(n>=cap)n=cap-1;
  memcpy(err,text,(size_t)n);
  err[n]='\0';
}

static void set_mysql_error(MYSQL *con,char *err,unsigned long cap,const char *prefix) {
  char tmp[512];

  snprintf(tmp,sizeof(tmp),"%s: %s",prefix,mysql_error(con));
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

static int load_calls(MYSQL *con,CompletionSet *set,const char *table,unsigned long *scanned,
                      unsigned long *valid,char *err,unsigned long errcap) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  char query[256],call[CALL_SIZE];

  snprintf(query,sizeof(query),"select callsign from %s where callsign is not null and callsign<>''",table);
  if(mysql_query(con,query)!=0) {
    set_mysql_error(con,err,errcap,"completion source query failed");
    return 0;
  }
  res=mysql_use_result(con);
  if(res==NULL) {
    set_mysql_error(con,err,errcap,"completion source result failed");
    return 0;
  }
  for(;;) {
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    (*scanned)++;
    if(!normalize_call(row[0],call))continue;
    (*valid)++;
    if(!set_add(set,call)) {
      mysql_free_result(res);
      set_error(err,errcap,"completion callsign hash allocation failed");
      return 0;
    }
  }
  if(mysql_errno(con)!=0) {
    mysql_free_result(res);
    set_mysql_error(con,err,errcap,"completion source read failed");
    return 0;
  }
  mysql_free_result(res);
  return 1;
}

static int query_simple(MYSQL *con,const char *query,char *err,unsigned long errcap,const char *what) {
  if(mysql_query(con,query)==0)return 1;
  set_mysql_error(con,err,errcap,what);
  return 0;
}

static int acquire_lock(MYSQL *con,char *err,unsigned long errcap) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  int ok;

  if(mysql_query(con,"select get_lock('qsoz_completion_rebuild',0)")!=0) {
    set_mysql_error(con,err,errcap,"completion lock failed");
    return 0;
  }
  res=mysql_store_result(con);
  if(res==NULL) {
    set_mysql_error(con,err,errcap,"completion lock result failed");
    return 0;
  }
  row=mysql_fetch_row(res);
  ok=row!=NULL && row[0]!=NULL && atoi(row[0])==1;
  mysql_free_result(res);
  if(!ok)set_error(err,errcap,"completion rebuild is already running");
  return ok;
}

static void release_lock(MYSQL *con) {
  MYSQL_RES *res;

  if(mysql_query(con,"select release_lock('qsoz_completion_rebuild')")!=0)return;
  res=mysql_store_result(con);
  if(res!=NULL)mysql_free_result(res);
}

static int gram_seen(char seen[][4],int count,const char *gram) {
  int i;

  for(i=0;i<count;i++)if(strcmp(seen[i],gram)==0)return 1;
  return 0;
}

static int flush_insert(MYSQL *con,char *sql,unsigned long len,unsigned long prefix_len,
                        char *err,unsigned long errcap) {
  if(len==prefix_len)return 1;
  if(mysql_query(con,sql)==0)return 1;
  set_mysql_error(con,err,errcap,"completion batch insert failed");
  return 0;
}

static int insert_grams(MYSQL *con,const CompletionSet *set,const char *table,int gram_len,
                        unsigned long *rows,char *err,unsigned long errcap) {
  char *sql;
  char prefix[128],entry[64],gram[4],seen[5][4];
  unsigned long i,n,len,prefix_len,entry_len;
  int pos,seen_count,first;

  snprintf(prefix,sizeof(prefix),"insert into %s (callsign,gram) values ",table);
  prefix_len=(unsigned long)strlen(prefix);
  sql=(char *)malloc((size_t)SQL_BUFFER);
  if(sql==NULL) {
    set_error(err,errcap,"completion SQL buffer allocation failed");
    return 0;
  }
  memcpy(sql,prefix,(size_t)prefix_len+1U);
  len=prefix_len;
  first=1;
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
      snprintf(entry,sizeof(entry),"%s('%s','%s')",first?"":",",set->slot[i].call,gram);
      entry_len=(unsigned long)strlen(entry);
      if(len+entry_len+1UL>=SQL_BUFFER) {
        if(!flush_insert(con,sql,len,prefix_len,err,errcap)) {
          free(sql);
          return 0;
        }
        memcpy(sql,prefix,(size_t)prefix_len+1U);
        len=prefix_len;
        first=1;
        snprintf(entry,sizeof(entry),"('%s','%s')",set->slot[i].call,gram);
        entry_len=(unsigned long)strlen(entry);
      }
      memcpy(sql+len,entry,(size_t)entry_len+1U);
      len+=entry_len;
      first=0;
      (*rows)++;
    }
  }
  if(!flush_insert(con,sql,len,prefix_len,err,errcap)) {
    free(sql);
    return 0;
  }
  free(sql);
  return 1;
}

static int table_count(MYSQL *con,const char *table,unsigned long *count,char *err,unsigned long errcap) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  char query[128];

  snprintf(query,sizeof(query),"select count(*) from %s",table);
  if(mysql_query(con,query)!=0) {
    set_mysql_error(con,err,errcap,"completion validation query failed");
    return 0;
  }
  res=mysql_store_result(con);
  if(res==NULL) {
    set_mysql_error(con,err,errcap,"completion validation result failed");
    return 0;
  }
  row=mysql_fetch_row(res);
  if(row==NULL || row[0]==NULL) {
    mysql_free_result(res);
    set_error(err,errcap,"completion validation returned no count");
    return 0;
  }
  *count=strtoul(row[0],NULL,10);
  mysql_free_result(res);
  return 1;
}

static void cleanup_staging(MYSQL *con) {
  mysql_query(con,"drop table if exists aux2_new,aux3_new");
}

static int completion_rebuild(MYSQL *con,CompletionStats *stats,char *err,unsigned long errcap) {
  CompletionSet set;
  unsigned long check2,check3;
  int locked,swapped,ok;

  memset(&set,0,sizeof(set));
  memset(stats,0,sizeof(*stats));
  err[0]='\0';
  locked=0;
  swapped=0;
  ok=0;
  if(!acquire_lock(con,err,errcap))goto end;
  locked=1;

  if(!load_calls(con,&set,"log",&stats->log_rows,&stats->valid_rows,err,errcap))goto end;
  if(!load_calls(con,&set,"wc",&stats->wc_rows,&stats->valid_rows,err,errcap))goto end;
  if(set.count==0) {
    set_error(err,errcap,"completion source contains no valid callsigns");
    goto end;
  }
  stats->calls=set.count;

  if(!query_simple(con,"drop table if exists aux2_new,aux3_new,aux2_old,aux3_old",err,errcap,"completion staging cleanup failed"))goto end;
  if(!query_simple(con,"create table aux2_new like aux2",err,errcap,"cannot create aux2_new"))goto end;
  if(!query_simple(con,"create table aux3_new like aux3",err,errcap,"cannot create aux3_new"))goto end;
  if(!query_simple(con,"alter table aux2_new drop primary key,drop index gram",err,errcap,"cannot prepare aux2_new"))goto end;
  if(!query_simple(con,"alter table aux3_new drop primary key,drop index gram",err,errcap,"cannot prepare aux3_new"))goto end;

  if(!insert_grams(con,&set,"aux2_new",2,&stats->bigrams,err,errcap))goto end;
  if(!insert_grams(con,&set,"aux3_new",3,&stats->trigrams,err,errcap))goto end;

  if(!query_simple(con,"alter table aux2_new add primary key (callsign,gram),add key gram (gram)",err,errcap,"cannot index aux2_new"))goto end;
  if(!query_simple(con,"alter table aux3_new add primary key (callsign,gram),add key gram (gram)",err,errcap,"cannot index aux3_new"))goto end;

  check2=0;
  check3=0;
  if(!table_count(con,"aux2_new",&check2,err,errcap) || !table_count(con,"aux3_new",&check3,err,errcap))goto end;
  if(check2!=stats->bigrams || check3!=stats->trigrams) {
    set_error(err,errcap,"completion staging row count mismatch");
    goto end;
  }

  if(!query_simple(con,"rename table aux2 to aux2_old,aux2_new to aux2,aux3 to aux3_old,aux3_new to aux3",err,errcap,"completion atomic swap failed"))goto end;
  swapped=1;
  if(mysql_query(con,"drop table if exists aux2_old,aux3_old")!=0)fprintf(stderr,"pcompletion: old table cleanup failed: %s\n",mysql_error(con));
  ok=1;

end:
  if(!swapped)cleanup_staging(con);
  free(set.slot);
  if(locked)release_lock(con);
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

static int auth_admin(MYSQL *con,const char *ota) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  char query[256];
  int ok;

  snprintf(query,sizeof(query),"select mycall from user where ota='%s' and lastota+durationota>%ld limit 1",ota,(long)time(NULL));
  if(mysql_query(con,query)!=0)return 0;
  res=mysql_store_result(con);
  if(res==NULL)return 0;
  row=mysql_fetch_row(res);
  ok=row!=NULL && row[0]!=NULL && strcmp(row[0],"IK4LZH")==0;
  mysql_free_result(res);
  return ok;
}

int main(void) {
  QsozConfig cfg;
  CompletionStats stats;
  MYSQL *con;
  char ota[64],err[512];

  con=NULL;
  err[0]='\0';
  printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--33-->");
  if(!read_ota(ota,sizeof(ota))) {
    printf("<pre><b>Login expired</b></pre>");
    return 0;
  }
  if(!qsoz_config_load(&cfg,QSOZ_CONFIG_FILE,err,sizeof(err)))goto fail;
  con=mysql_init(NULL);
  if(con==NULL) {
    set_error(err,sizeof(err),"database initialization failed");
    goto fail;
  }
  if(mysql_real_connect(con,cfg.db_host,cfg.db_user,cfg.db_pass,cfg.db_name,cfg.db_port,NULL,0)==NULL) {
    set_error(err,sizeof(err),"database connection failed");
    goto fail;
  }
  if(!auth_admin(con,ota)) {
    set_error(err,sizeof(err),"completion rebuild is restricted to IK4LZH");
    goto fail;
  }
  if(!completion_rebuild(con,&stats,err,sizeof(err)))goto fail;

  printf("<pre><b>Completion updated</b>\n");
  printf("log rows scanned: %lu\n",stats.log_rows);
  printf("wc rows scanned: %lu\n",stats.wc_rows);
  printf("valid source rows: %lu\n",stats.valid_rows);
  printf("unique callsigns: %lu\n",stats.calls);
  printf("aux2 bigrams: %lu\n",stats.bigrams);
  printf("aux3 trigrams: %lu\n",stats.trigrams);
  printf("</pre>");
  mysql_close(con);
  return 0;

fail:
  printf("<pre><b>Completion update failed</b>\n%s\n</pre>",err[0]?err:"unknown error");
  if(con!=NULL)mysql_close(con);
  return 0;
}
