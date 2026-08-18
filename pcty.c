// Gianluca Mazzini @2022- Version 3.0
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <zip.h>
#include <mysql/mysql.h>
#include "qsoz_config.h"
#include "qsoz_util.h"

#define CTY_URL "https://www.country-files.com/bigcty/download/bigcty.zip"
#define DOWNLOAD_MAX (8UL*1024UL*1024UL)
#define CSV_MAX (16UL*1024UL*1024UL)
#define LINE_MAX_SIZE 131072UL
#define FIELD_COUNT 10

static const char *create_sql=
  "CREATE TABLE cty_new ("
  "base varchar(10) NOT NULL,"
  "name varchar(50) NOT NULL,"
  "dxcc int(11) NOT NULL,"
  "cont varchar(2) NOT NULL,"
  "cqzone int(11) NOT NULL,"
  "ituzone int(11) NOT NULL,"
  "latitude float NOT NULL,"
  "longitude float NOT NULL,"
  "gmtshift float NOT NULL,"
  "prefix varchar(20) NOT NULL,"
  "KEY dxcc (dxcc),KEY prefix (prefix)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci";

static const char *insert_sql=
  "INSERT INTO cty_new (base,name,dxcc,cont,cqzone,ituzone,latitude,longitude,gmtshift,prefix) VALUES (?,?,?,?,?,?,?,?,?,?)";

typedef struct {
  unsigned char *ptr;
  unsigned long len;
  unsigned long cap;
  int failed;
} Mem;

static size_t write_cb(void *ptr,size_t size,size_t nmemb,void *userdata) {
  Mem *m;
  unsigned long add,need,cap;
  unsigned char *p;

  m=(Mem *)userdata;
  if(size!=0 && nmemb>((size_t)-1)/size){m->failed=1; return 0;}
  add=(unsigned long)(size*nmemb);
  if(add>DOWNLOAD_MAX || m->len>DOWNLOAD_MAX-add){m->failed=1; return 0;}
  need=m->len+add;
  if(need>m->cap){
    cap=m->cap?m->cap:65536UL;
    for(;cap<need;){
      if(cap>DOWNLOAD_MAX/2){cap=DOWNLOAD_MAX; break;}
      cap*=2;
    }
    p=(unsigned char *)realloc(m->ptr,(size_t)cap);
    if(p==NULL){m->failed=1; return 0;}
    m->ptr=p;
    m->cap=cap;
  }
  memcpy(m->ptr+m->len,ptr,(size_t)add);
  m->len+=add;
  return size*nmemb;
}

static int download_zip(Mem *m,char *err,unsigned long errcap) {
  CURL *curl;
  CURLcode rc;
  long status;

  curl=curl_easy_init();
  if(curl==NULL){snprintf(err,(size_t)errcap,"curl initialization failed"); return 0;}
  curl_easy_setopt(curl,CURLOPT_URL,CTY_URL);
  curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,write_cb);
  curl_easy_setopt(curl,CURLOPT_WRITEDATA,m);
  curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
  curl_easy_setopt(curl,CURLOPT_MAXREDIRS,5L);
  curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,15L);
  curl_easy_setopt(curl,CURLOPT_TIMEOUT,60L);
  curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
  curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
  curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
  curl_easy_setopt(curl,CURLOPT_USERAGENT,"qsoz-pcty/3.0");
  rc=curl_easy_perform(curl);
  status=0;
  if(rc==CURLE_OK)curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
  curl_easy_cleanup(curl);
  if(rc!=CURLE_OK || m->failed){snprintf(err,(size_t)errcap,"download failed"); return 0;}
  if(status<200 || status>=300){snprintf(err,(size_t)errcap,"HTTP status %ld",status); return 0;}
  if(m->len<4 || m->ptr[0]!='P' || m->ptr[1]!='K'){snprintf(err,(size_t)errcap,"invalid ZIP download"); return 0;}
  return 1;
}

static int zip_read_file(zip_t *za,const char *name,unsigned char **out,unsigned long *outlen,unsigned long max,char *err,unsigned long errcap) {
  zip_int64_t idx,nread,total;
  zip_stat_t st;
  zip_file_t *zf;
  unsigned char *buf;

  idx=zip_name_locate(za,name,0);
  if(idx<0){snprintf(err,(size_t)errcap,"%s missing from ZIP",name); return 0;}
  zip_stat_init(&st);
  if(zip_stat_index(za,(zip_uint64_t)idx,0,&st)!=0 || !(st.valid&ZIP_STAT_SIZE) || st.size==0 || st.size>max){snprintf(err,(size_t)errcap,"invalid %s size",name); return 0;}
  buf=(unsigned char *)malloc((size_t)st.size+1);
  if(buf==NULL){snprintf(err,(size_t)errcap,"memory allocation failed"); return 0;}
  zf=zip_fopen_index(za,(zip_uint64_t)idx,0);
  if(zf==NULL){free(buf); snprintf(err,(size_t)errcap,"cannot open %s",name); return 0;}
  total=0;
  for(;total<(zip_int64_t)st.size;){
    nread=zip_fread(zf,buf+total,(zip_uint64_t)(st.size-(zip_uint64_t)total));
    if(nread<=0){zip_fclose(zf); free(buf); snprintf(err,(size_t)errcap,"cannot read %s",name); return 0;}
    total+=nread;
  }
  zip_fclose(zf);
  buf[st.size]='\0';
  *out=buf;
  *outlen=(unsigned long)st.size;
  return 1;
}

static zip_t *open_zip(const Mem *m,zip_source_t **source,char *err,unsigned long errcap) {
  zip_error_t ze;
  zip_t *za;

  zip_error_init(&ze);
  *source=zip_source_buffer_create(m->ptr,(zip_uint64_t)m->len,0,&ze);
  if(*source==NULL){snprintf(err,(size_t)errcap,"ZIP source failed"); zip_error_fini(&ze); return NULL;}
  za=zip_open_from_source(*source,ZIP_RDONLY,&ze);
  if(za==NULL){snprintf(err,(size_t)errcap,"ZIP open failed"); zip_source_free(*source); *source=NULL; zip_error_fini(&ze); return NULL;}
  zip_error_fini(&ze);
  return za;
}

static int csv_split(char *line,char *field[FIELD_COUNT]) {
  char *src,*dst;
  int n,quoted;

  src=line;
  dst=line;
  n=0;
  for(;n<FIELD_COUNT;n++){
    field[n]=dst;
    quoted=0;
    if(*src=='"'){quoted=1; src++;}
    for(;;){
      if(*src=='\0'){
        *dst='\0';
        return n==FIELD_COUNT-1;
      }
      if(quoted && *src=='"'){
        if(src[1]=='"'){*dst++='"'; src+=2; continue;}
        quoted=0;
        src++;
        continue;
      }
      if(!quoted && *src==','){
        *dst++='\0';
        src++;
        break;
      }
      *dst++=*src++;
    }
  }
  return *src=='\0';
}

static int cut_override(char *s,char open,char close,char *value,unsigned long cap) {
  char *a,*b;
  unsigned long n;

  a=strchr(s,open);
  if(a==NULL)return 0;
  b=strchr(a+1,close);
  if(b==NULL)return -1;
  n=(unsigned long)(b-a-1);
  if(n==0 || n>=cap)return -1;
  memcpy(value,a+1,(size_t)n);
  value[n]='\0';
  memmove(a,b+1,strlen(b+1)+1);
  return 1;
}

static int parse_long_value(const char *s,long *value) {
  char *end;

  *value=strtol(s,&end,10);
  return end!=s && *end=='\0';
}

static int parse_double_value(const char *s,double *value) {
  char *end;

  *value=strtod(s,&end);
  return end!=s && *end=='\0';
}

static int bind_text(MYSQL_BIND *b,const char *s,unsigned long *len) {
  *len=(unsigned long)strlen(s);
  memset(b,0,sizeof(*b));
  b->buffer_type=MYSQL_TYPE_STRING;
  b->buffer=(void *)s;
  b->buffer_length=*len;
  b->length=len;
  return 1;
}

static int insert_prefix(MYSQL_STMT *stmt,const char *base,const char *name,long dxcc,const char *cont,long cq,long itu,double lat,double lon,double gmt,const char *prefix) {
  MYSQL_BIND b[FIELD_COUNT];
  unsigned long len[4];

  memset(b,0,sizeof(b));
  bind_text(&b[0],base,&len[0]);
  bind_text(&b[1],name,&len[1]);
  b[2].buffer_type=MYSQL_TYPE_LONG; b[2].buffer=&dxcc;
  bind_text(&b[3],cont,&len[2]);
  b[4].buffer_type=MYSQL_TYPE_LONG; b[4].buffer=&cq;
  b[5].buffer_type=MYSQL_TYPE_LONG; b[5].buffer=&itu;
  b[6].buffer_type=MYSQL_TYPE_DOUBLE; b[6].buffer=&lat;
  b[7].buffer_type=MYSQL_TYPE_DOUBLE; b[7].buffer=&lon;
  b[8].buffer_type=MYSQL_TYPE_DOUBLE; b[8].buffer=&gmt;
  bind_text(&b[9],prefix,&len[3]);
  if(mysql_stmt_bind_param(stmt,b)!=0)return 0;
  return mysql_stmt_execute(stmt)==0;
}

static int import_line(MYSQL_STMT *stmt,char *line,unsigned long *count) {
  char *f[FIELD_COUNT],*p,*end,*next;
  char base[16],name[64],cont[8],tmp[64],prefix[64];
  long dxcc,cq,itu;
  double lat,lon,gmt;
  int rc;

  if(!csv_split(line,f))return 0;
  if(strlen(f[0])>=sizeof(base) || strlen(f[1])>=sizeof(name) || strlen(f[3])>=sizeof(cont))return 0;
  strcpy(base,f[0]); strcpy(name,f[1]); strcpy(cont,f[3]);
  if(!parse_long_value(f[2],&dxcc) || !parse_long_value(f[4],&cq) || !parse_long_value(f[5],&itu) || !parse_double_value(f[6],&lat) || !parse_double_value(f[7],&lon) || !parse_double_value(f[8],&gmt))return 0;
  p=f[9];
  end=p+strlen(p);
  if(end>p && end[-1]==';')end[-1]='\0';
  for(;*p!='\0';){
    for(;*p==' ' || *p=='\t';p++){}
    if(*p=='\0')break;
    next=p;
    for(;*next!='\0' && *next!=' ' && *next!='\t';next++){}
    if(*next!='\0')*next++='\0';
    if(strlen(p)>=sizeof(prefix))return 0;
    strcpy(prefix,p);
    strcpy(tmp,cont); rc=cut_override(prefix,'{','}',tmp,sizeof(tmp)); if(rc<0)return 0; if(rc>0){if(strlen(tmp)>=sizeof(cont))return 0; strcpy(cont,tmp);} else strcpy(cont,f[3]);
    rc=cut_override(prefix,'(',')',tmp,sizeof(tmp)); if(rc<0)return 0; if(rc>0){if(!parse_long_value(tmp,&cq))return 0;} else if(!parse_long_value(f[4],&cq))return 0;
    rc=cut_override(prefix,'[',']',tmp,sizeof(tmp)); if(rc<0)return 0; if(rc>0){if(!parse_long_value(tmp,&itu))return 0;} else if(!parse_long_value(f[5],&itu))return 0;
    rc=cut_override(prefix,'<','>',tmp,sizeof(tmp));
    if(rc<0)return 0;
    if(rc>0){
      char *slash;
      slash=strchr(tmp,'/');
      if(slash==NULL)return 0;
      *slash='\0';
      if(!parse_double_value(tmp,&lat) || !parse_double_value(slash+1,&lon))return 0;
    } else {if(!parse_double_value(f[6],&lat) || !parse_double_value(f[7],&lon))return 0;}
    rc=cut_override(prefix,'~','~',tmp,sizeof(tmp)); if(rc<0)return 0; if(rc>0){if(!parse_double_value(tmp,&gmt))return 0;} else if(!parse_double_value(f[8],&gmt))return 0;
    if(prefix[0]=='=')memmove(prefix,prefix+1,strlen(prefix));
    if(prefix[0]=='\0' || strlen(prefix)>20 || strlen(base)>10 || strlen(name)>50 || strlen(cont)>2)return 0;
    if(!insert_prefix(stmt,base,name,dxcc,cont,cq,itu,lat,lon,gmt,prefix))return 0;
    (*count)++;
    p=next;
  }
  return 1;
}

static int import_csv(MYSQL *con,unsigned char *csv,unsigned long len,unsigned long *count,char *err,unsigned long errcap) {
  MYSQL_STMT *stmt;
  char *line,*p,*e;
  unsigned long lineno;

  if(mysql_query(con,"DROP TABLE IF EXISTS cty_new")!=0 || mysql_query(con,create_sql)!=0){snprintf(err,(size_t)errcap,"cannot create cty_new: %s",mysql_error(con)); return 0;}
  stmt=mysql_stmt_init(con);
  if(stmt==NULL || mysql_stmt_prepare(stmt,insert_sql,(unsigned long)strlen(insert_sql))!=0){if(stmt!=NULL)mysql_stmt_close(stmt); snprintf(err,(size_t)errcap,"cannot prepare CTY insert"); return 0;}
  *count=0;
  p=(char *)csv;
  lineno=0;
  for(;p<(char *)csv+len;){
    e=memchr(p,'\n',(size_t)(((char *)csv+len)-p));
    if(e==NULL)e=(char *)csv+len;
    if((unsigned long)(e-p)>=LINE_MAX_SIZE){snprintf(err,(size_t)errcap,"CTY line too long"); mysql_stmt_close(stmt); return 0;}
    if(e<(char *)csv+len)*e='\0';
    if(e>p && e[-1]=='\r')e[-1]='\0';
    lineno++;
    line=p;
    if(*line!='\0' && !import_line(stmt,line,count)){snprintf(err,(size_t)errcap,"invalid CTY data at line %lu",lineno); mysql_stmt_close(stmt); return 0;}
    p=e+1;
  }
  mysql_stmt_close(stmt);
  if(*count<20000UL){snprintf(err,(size_t)errcap,"CTY validation failed: only %lu entries",*count); return 0;}
  return 1;
}

static void release_text(const unsigned char *readme,unsigned long len,char *out,unsigned long cap) {
  const char *p,*e;
  unsigned long n;

  out[0]='\0';
  p=(const char *)readme;
  e=memchr(p,'\n',(size_t)len);
  if(e==NULL)e=p+len;
  for(;e>p && (e[-1]=='\r' || e[-1]=='\n');e--){}
  n=(unsigned long)(e-p);
  if(n>=cap)n=cap-1;
  memcpy(out,p,(size_t)n);
  out[n]='\0';
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
  MYSQL *con;
  Mem zipmem;
  zip_source_t *source;
  zip_t *za;
  unsigned char *csv,*readme;
  unsigned long csvlen,readmelen,count,n;
  char ota[64],err[256],release[256];
  int ok;

  con=NULL; source=NULL; za=NULL; csv=NULL; readme=NULL;
  memset(&zipmem,0,sizeof(zipmem));
  err[0]='\0'; release[0]='\0'; count=0;
  n=(unsigned long)fread(ota,1,sizeof(ota)-1,stdin);
  ota[n]='\0';
  for(;n>0 && (ota[n-1]=='\r' || ota[n-1]=='\n' || isspace((unsigned char)ota[n-1]));n--)ota[n-1]='\0';
  if(!qsoz_token_valid(ota)){
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--32--><pre><b>Login expired</b></pre>");
    return 0;
  }
  if(!qsoz_config_load(&cfg,QSOZ_CONFIG_FILE,err,sizeof(err)))goto fail;
  con=mysql_init(NULL);
  if(con==NULL){snprintf(err,sizeof(err),"database initialization failed"); goto fail;}
  if(mysql_real_connect(con,cfg.db_host,cfg.db_user,cfg.db_pass,cfg.db_name,cfg.db_port,NULL,0)==NULL){snprintf(err,sizeof(err),"database connection failed"); goto fail;}
  mysql_query(con,"SET time_zone='+00:00'");
  if(!auth_admin(con,ota)){snprintf(err,sizeof(err),"CTY update is restricted to IK4LZH"); goto fail;}
  if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){snprintf(err,sizeof(err),"curl global initialization failed"); goto fail;}
  ok=download_zip(&zipmem,err,sizeof(err));
  if(!ok)goto fail_curl;
  za=open_zip(&zipmem,&source,err,sizeof(err));
  if(za==NULL)goto fail_curl;
  if(!zip_read_file(za,"cty.csv",&csv,&csvlen,CSV_MAX,err,sizeof(err)))goto fail_zip;
  if(!zip_read_file(za,"README.TXT",&readme,&readmelen,1024UL*1024UL,err,sizeof(err)))goto fail_zip;
  release_text(readme,readmelen,release,sizeof(release));
  if(!import_csv(con,csv,csvlen,&count,err,sizeof(err)))goto fail_import;
  if(mysql_query(con,"DROP TABLE IF EXISTS cty_old")!=0){snprintf(err,sizeof(err),"cannot remove old CTY staging table"); goto fail_import;}
  if(mysql_query(con,"RENAME TABLE cty TO cty_old, cty_new TO cty")!=0){snprintf(err,sizeof(err),"CTY atomic swap failed: %s",mysql_error(con)); goto fail_import;}
  if(mysql_query(con,"DROP TABLE cty_old")!=0){snprintf(err,sizeof(err),"CTY updated but old table cleanup failed"); goto fail_import;}
  printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--32--><pre><b>CTY updated</b>\n%s\nEntries: %lu\n</pre>",release,count);
  free(readme); free(csv); zip_close(za); free(zipmem.ptr); curl_global_cleanup(); mysql_close(con);
  return 0;

fail_import:
  mysql_query(con,"DROP TABLE IF EXISTS cty_new");
fail_zip:
  free(readme); free(csv);
  if(za!=NULL)zip_close(za);
fail_curl:
  free(zipmem.ptr);
  curl_global_cleanup();
fail:
  printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--32--><pre><b>CTY update failed</b>\n%s\n</pre>",err[0]?err:"unknown error");
  if(con!=NULL)mysql_close(con);
  return 0;
}
