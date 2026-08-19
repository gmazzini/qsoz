// Gianluca Mazzini @2022- Version 3.01
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <mysql/mysql.h>
#include "qsoz_config.h"

#define DXCC_MAX 1000
#define BAND_COUNT 11
#define DELTA_MIN (-70)
#define DELTA_MAX 70
#define DELTA_SHOW_MIN (-35)
#define DELTA_SHOW_MAX 35
#define DELTA_COUNT (DELTA_MAX-DELTA_MIN+1)
#define CQ_MIN 1
#define CQ_MAX 40
#define MONTH_FIRST 201901
#define CACHE_FILE "/home/www/ft8/.pft8.cache"
#define CACHE_MAGIC "PFT8C01"
#define CACHE_VERSION 1UL
#define CACHE_MONTH_MAX 10000UL

typedef struct {
  long key;
  unsigned long cq[CQ_MAX+1];
} MonthData;

typedef struct {
  MonthData *v;
  unsigned long count;
  unsigned long cap;
} MonthVec;

typedef struct {
  unsigned long long log_update;
  unsigned long long cty_update;
  unsigned long long max_open;
  unsigned long long log_rows;
} CacheKey;

typedef struct {
  char magic[8];
  unsigned long version;
  CacheKey key;
  unsigned long processed;
  unsigned long month_count;
  unsigned long acc[BAND_COUNT][DELTA_COUNT];
  unsigned long total[BAND_COUNT];
} CacheHeader;

static const int band_name[BAND_COUNT]={160,80,60,40,30,20,17,15,12,10,0};


static int band_index(long mhz) {
  int band,i;

  band=0;
  if(mhz==1)band=160;
  else if(mhz==3)band=80;
  else if(mhz==5)band=60;
  else if(mhz==7)band=40;
  else if(mhz==10)band=30;
  else if(mhz==14)band=20;
  else if(mhz==18)band=17;
  else if(mhz==21)band=15;
  else if(mhz==24)band=12;
  else if(mhz==28 || mhz==29)band=10;
  if(band==0)return -1;
  for(i=0;i<BAND_COUNT-1;i++)if(band_name[i]==band)return i;
  return -1;
}

static int numeric_int(const char *s,int *value) {
  char *end;
  double d;

  if(s==NULL || *s=='\0')return 0;
  errno=0;
  d=strtod(s,&end);
  if(errno!=0 || end==s || *end!='\0')return 0;
  if(d<-2147483647.0 || d>2147483647.0)return 0;
  *value=(int)d;
  return 1;
}

static long month_key(time_t epoch) {
  struct tm *t;
  long year,part;

  t=localtime(&epoch);
  if(t==NULL)return 0;
  year=(long)t->tm_year+1900L;
  part=(long)t->tm_mon*100L/12L;
  return year*100L+part;
}

static int month_get(MonthVec *mv,long key,MonthData **out) {
  MonthData *p;
  unsigned long i,newcap;

  for(i=0;i<mv->count;i++)if(mv->v[i].key==key){*out=&mv->v[i]; return 1;}
  if(mv->count==mv->cap) {
    newcap=mv->cap?mv->cap*2UL:64UL;
    p=(MonthData *)realloc(mv->v,(size_t)newcap*sizeof(MonthData));
    if(p==NULL)return 0;
    mv->v=p;
    mv->cap=newcap;
  }
  memset(&mv->v[mv->count],0,sizeof(MonthData));
  mv->v[mv->count].key=key;
  *out=&mv->v[mv->count++];
  return 1;
}

static int cmp_month(const void *a,const void *b) {
  const MonthData *aa,*bb;

  aa=(const MonthData *)a;
  bb=(const MonthData *)b;
  if(aa->key<bb->key)return -1;
  if(aa->key>bb->key)return 1;
  return 0;
}

static int cache_key_equal(const CacheKey *a,const CacheKey *b) {
  return a->log_update==b->log_update && a->cty_update==b->cty_update &&
         a->max_open==b->max_open && a->log_rows==b->log_rows;
}

static int get_cache_key(MYSQL *con,const char *dbname,CacheKey *key) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  char escaped[256],query[1024];
  unsigned long n;

  if(strlen(dbname)>120)return 0;
  n=mysql_real_escape_string(con,escaped,dbname,(unsigned long)strlen(dbname));
  if(n>=sizeof(escaped))return 0;
  snprintf(query,sizeof(query),
           "select coalesce(unix_timestamp(max(case when table_name='log' then update_time end)),0),"
           "coalesce(unix_timestamp(max(case when table_name='cty' then update_time end)),0),"
           "coalesce((select max(open) from log),0),"
           "coalesce(max(case when table_name='log' then table_rows end),0) "
           "from information_schema.tables where table_schema='%s' and table_name in ('log','cty')",escaped);
  if(mysql_query(con,query)!=0)return 0;
  res=mysql_store_result(con);
  if(res==NULL)return 0;
  row=mysql_fetch_row(res);
  if(row==NULL || row[0]==NULL || row[1]==NULL || row[2]==NULL || row[3]==NULL) {
    mysql_free_result(res);
    return 0;
  }
  key->log_update=strtoull(row[0],NULL,10);
  key->cty_update=strtoull(row[1],NULL,10);
  key->max_open=strtoull(row[2],NULL,10);
  key->log_rows=strtoull(row[3],NULL,10);
  mysql_free_result(res);
  return 1;
}

static int cache_load(const CacheKey *key,unsigned long acc[BAND_COUNT][DELTA_COUNT],
                      unsigned long total[BAND_COUNT],MonthVec *mv,unsigned long *processed) {
  CacheHeader h;
  MonthData *v;
  FILE *fp;
  size_t n;

  fp=fopen(CACHE_FILE,"rb");
  if(fp==NULL)return 0;
  n=fread(&h,1,sizeof(h),fp);
  if(n!=sizeof(h) || memcmp(h.magic,CACHE_MAGIC,8)!=0 || h.version!=CACHE_VERSION ||
     !cache_key_equal(&h.key,key) || h.month_count>CACHE_MONTH_MAX) {
    fclose(fp);
    return 0;
  }
  v=NULL;
  if(h.month_count>0) {
    v=(MonthData *)malloc((size_t)h.month_count*sizeof(MonthData));
    if(v==NULL) {fclose(fp); return 0;}
    n=fread(v,sizeof(MonthData),(size_t)h.month_count,fp);
    if(n!=(size_t)h.month_count) {free(v); fclose(fp); return 0;}
  }
  fclose(fp);
  memcpy(acc,h.acc,sizeof(h.acc));
  memcpy(total,h.total,sizeof(h.total));
  mv->v=v;
  mv->count=h.month_count;
  mv->cap=h.month_count;
  *processed=h.processed;
  return 1;
}

static void cache_save(const CacheKey *key,const unsigned long acc[BAND_COUNT][DELTA_COUNT],
                       const unsigned long total[BAND_COUNT],const MonthVec *mv,unsigned long processed) {
  CacheHeader h;
  char path[256];
  FILE *fp;
  int ok;

  if(mv->count>CACHE_MONTH_MAX)return;
  memset(&h,0,sizeof(h));
  memcpy(h.magic,CACHE_MAGIC,8);
  h.version=CACHE_VERSION;
  h.key=*key;
  h.processed=processed;
  h.month_count=mv->count;
  memcpy(h.acc,acc,sizeof(h.acc));
  memcpy(h.total,total,sizeof(h.total));
  snprintf(path,sizeof(path),"%s.%ld",CACHE_FILE,(long)getpid());
  fp=fopen(path,"wb");
  if(fp==NULL)return;
  ok=fwrite(&h,1,sizeof(h),fp)==sizeof(h);
  if(ok && mv->count>0)ok=fwrite(mv->v,sizeof(MonthData),(size_t)mv->count,fp)==(size_t)mv->count;
  if(fclose(fp)!=0)ok=0;
  if(ok) {
    if(rename(path,CACHE_FILE)!=0)unlink(path);
  } else unlink(path);
}

static int load_cq(MYSQL *con,int cq[DXCC_MAX]) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  int dx;

  memset(cq,0,sizeof(int)*DXCC_MAX);
  if(mysql_query(con,"select dxcc,cqzone from cty")!=0)return 0;
  res=mysql_use_result(con);
  if(res==NULL)return 0;
  for(;;) {
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    dx=atoi(row[0]);
    if(dx>=0 && dx<DXCC_MAX)cq[dx]=atoi(row[1]);
  }
  mysql_free_result(res);
  return mysql_errno(con)==0;
}

static int load_qso(MYSQL *con,const int cq[DXCC_MAX],unsigned long acc[BAND_COUNT][DELTA_COUNT],
                    unsigned long total[BAND_COUNT],MonthVec *mv,unsigned long *processed) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  MonthData *md;
  long mhz,key;
  int bi,tx,rx,delta,dx,zone;
  time_t epoch;

  if(mysql_query(con,"select freqtx,signaltx,signalrx,dxcc,open from log where mode='FT8' or mode='MFSK'")!=0)return 0;
  res=mysql_use_result(con);
  if(res==NULL)return 0;
  for(;;) {
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    mhz=atol(row[0])/1000000L;
    if(mhz==0 || mhz>29)continue;
    if(!numeric_int(row[1],&tx) || tx<DELTA_SHOW_MIN || tx>DELTA_SHOW_MAX)continue;
    if(!numeric_int(row[2],&rx) || rx<DELTA_SHOW_MIN || rx>DELTA_SHOW_MAX)continue;
    bi=band_index(mhz);
    if(bi<0)continue;
    delta=tx-rx;
    if(delta<DELTA_SHOW_MIN || delta>DELTA_SHOW_MAX)continue;
    acc[bi][delta-DELTA_MIN]++;
    total[bi]++;
    acc[BAND_COUNT-1][delta-DELTA_MIN]++;
    total[BAND_COUNT-1]++;
    dx=atoi(row[3]);
    epoch=(time_t)atoll(row[4]);
    key=month_key(epoch);
    zone=(dx>=0 && dx<DXCC_MAX)?cq[dx]:0;
    if(key>=MONTH_FIRST && zone>=CQ_MIN && zone<=CQ_MAX) {
      if(!month_get(mv,key,&md)) {mysql_free_result(res); return 0;}
      md->cq[zone]++;
    }
    (*processed)++;
  }
  mysql_free_result(res);
  return mysql_errno(con)==0;
}

static void print_css(void) {
  printf("<style>");
  printf("html,body{margin:0;padding:0;font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#f5f6fa;color:#20242a}");
  printf("h1{font-size:1.25rem;margin:16px}.section{min-height:100vh;padding:12px 16px;box-sizing:border-box;display:flex}.card{flex:1;background:#fff;border-radius:14px;box-shadow:0 4px 12px rgba(0,0,0,.06);padding:16px;overflow:auto}");
  printf("h2{font-size:1rem;margin:0 0 12px}svg{width:100%%;height:auto;min-height:520px;display:block}table{width:100%%;border-collapse:collapse;font-size:.9rem}th,td{padding:6px 10px;text-align:right;border-bottom:1px solid #e0e3ea;white-space:nowrap}th:first-child,td:first-child{text-align:left}thead{background:#f0f2f7}tbody tr:hover{background:#f9fafc}.muted{color:#666;font-size:.85rem}</style>");
}

static void print_line_chart(const unsigned long acc[BAND_COUNT][DELTA_COUNT],const unsigned long total[BAND_COUNT]) {
  double maxv,v,x,y;
  int d,b;

  maxv=0.0;
  for(d=DELTA_SHOW_MIN;d<=DELTA_SHOW_MAX;d++)for(b=0;b<BAND_COUNT;b++) {
    if(total[b]==0)continue;
    v=(double)acc[b][d-DELTA_MIN]/(double)total[b];
    if(v>maxv)maxv=v;
  }
  if(maxv<=0.0)maxv=1.0;
  printf("<svg viewBox='0 0 1200 560' role='img' aria-label='TX-RX probability distributions'>");
  printf("<rect x='0' y='0' width='1200' height='560' fill='white'/>");
  for(d=0;d<=5;d++){y=30.0+450.0*(double)d/5.0; printf("<line x1='70' y1='%.1f' x2='1140' y2='%.1f' stroke='#ddd'/>",y,y); printf("<text x='62' y='%.1f' text-anchor='end' font-size='11'>%.3f</text>",y+4,maxv*(5-d)/5.0);}
  printf("<line x1='70' y1='480' x2='1140' y2='480' stroke='#333'/><line x1='70' y1='30' x2='70' y2='480' stroke='#333'/>");
  for(d=DELTA_SHOW_MIN;d<=DELTA_SHOW_MAX;d+=5){x=70.0+1070.0*(double)(d-DELTA_SHOW_MIN)/(double)(DELTA_SHOW_MAX-DELTA_SHOW_MIN); printf("<line x1='%.1f' y1='480' x2='%.1f' y2='486' stroke='#333'/><text x='%.1f' y='502' text-anchor='middle' font-size='11'>%d</text>",x,x,x,d);}
  for(b=0;b<BAND_COUNT;b++) {
    if(total[b]==0)continue;
    printf("<polyline fill='none' stroke='hsl(%d,65%%,42%%)' stroke-width='1.8' points='",(b*31)%360);
    for(d=DELTA_SHOW_MIN;d<=DELTA_SHOW_MAX;d++) {
      x=70.0+1070.0*(double)(d-DELTA_SHOW_MIN)/(double)(DELTA_SHOW_MAX-DELTA_SHOW_MIN);
      v=(double)acc[b][d-DELTA_MIN]/(double)total[b];
      y=480.0-450.0*v/maxv;
      printf("%.1f,%.1f ",x,y);
    }
    printf("'/>");
  }
  printf("<text x='605' y='535' text-anchor='middle' font-size='13'>TX-RX (dB)</text>");
  for(b=0;b<BAND_COUNT;b++){printf("<text x='%d' y='525' font-size='11' fill='hsl(%d,65%%,42%%)'>%s%d</text>",75+b*85,(b*31)%360,b==BAND_COUNT-1?"":"",band_name[b]);}
  printf("</svg>");
}

static void print_table(const unsigned long acc[BAND_COUNT][DELTA_COUNT],const unsigned long total[BAND_COUNT]) {
  double mean,sq,var,sd;
  unsigned long count;
  int b,d;

  printf("<table><thead><tr><th>Band</th><th>QSOs</th><th>Average</th><th>Stdev</th></tr></thead><tbody>");
  for(b=0;b<BAND_COUNT;b++) {
    if(total[b]==0)continue;
    mean=0.0;
    sq=0.0;
    count=0;
    for(d=DELTA_SHOW_MIN;d<=DELTA_SHOW_MAX;d++) {
      count=acc[b][d-DELTA_MIN];
      mean+=(double)d*(double)count;
      sq+=(double)d*(double)d*(double)count;
    }
    mean/=total[b];
    var=sq/total[b]-mean*mean;
    if(var<0.0 && var>-0.0000001)var=0.0;
    sd=var>0.0?sqrt(var):0.0;
    if(b==BAND_COUNT-1)printf("<tr><td>all</td>");
    else printf("<tr><td>%d</td>",band_name[b]);
    printf("<td>%lu</td><td>%+7.5f</td><td>%7.4f</td></tr>",total[b],mean,sd);
  }
  printf("</tbody></table>");
}

static void print_cq_chart(const MonthVec *mv) {
  unsigned long i,maxc,c;
  double x,y,r;
  int zone;
  long min_key,max_key;

  if(mv->count==0) {
    printf("<p>No CQ zone data</p>");
    return;
  }
  min_key=mv->v[0].key;
  max_key=mv->v[mv->count-1].key;
  maxc=1;
  for(i=0;i<mv->count;i++)for(zone=CQ_MIN;zone<=CQ_MAX;zone++)if(mv->v[i].cq[zone]>maxc)maxc=mv->v[i].cq[zone];
  printf("<svg viewBox='0 0 1200 560' role='img' aria-label='CQ zone activity over time'>");
  printf("<rect x='0' y='0' width='1200' height='560' fill='white'/><line x1='70' y1='480' x2='1140' y2='480' stroke='#333'/><line x1='70' y1='30' x2='70' y2='480' stroke='#333'/>");
  for(zone=5;zone<=40;zone+=5){y=480.0-450.0*(double)(zone-CQ_MIN)/(double)(CQ_MAX-CQ_MIN); printf("<line x1='70' y1='%.1f' x2='1140' y2='%.1f' stroke='#eee'/><text x='62' y='%.1f' text-anchor='end' font-size='11'>%d</text>",y,y,y+4.0,zone);}
  for(i=0;i<mv->count;i++) {
    if(i%12UL==0 || i==mv->count-1) {
      x=mv->count<=1?605.0:70.0+1070.0*(double)i/(double)(mv->count-1UL);
      printf("<text x='%.1f' y='502' text-anchor='middle' font-size='10'>%ld</text>",x,mv->v[i].key);
    }
    for(zone=CQ_MIN;zone<=CQ_MAX;zone++) {
      c=mv->v[i].cq[zone];
      if(c==0)continue;
      x=mv->count<=1?605.0:70.0+1070.0*(double)i/(double)(mv->count-1UL);
      y=480.0-450.0*(double)(zone-CQ_MIN)/(double)(CQ_MAX-CQ_MIN);
      r=2.0+7.0*sqrt((double)c/(double)maxc);
      printf("<circle cx='%.1f' cy='%.1f' r='%.2f' fill='hsl(%d,75%%,48%%)' fill-opacity='.55'><title>%ld CQ %d: %lu QSO</title></circle>",x,y,r,(zone*17)%360,mv->v[i].key,zone,c);
    }
  }
  printf("<text x='605' y='535' text-anchor='middle' font-size='13'>Time bucket</text><text x='18' y='255' transform='rotate(-90 18 255)' text-anchor='middle' font-size='13'>CQ zone</text>");
  printf("</svg><p class='muted'>Bubble size is proportional to QSO count. Time bucket format preserves the historical PHP calculation.</p>");
  (void)min_key;
  (void)max_key;
}

int main(void) {
  QsozConfig cfg;
  MYSQL *con;
  MonthVec mv;
  CacheKey key_before,key_after;
  unsigned long acc[BAND_COUNT][DELTA_COUNT],total[BAND_COUNT],processed;
  int cq[DXCC_MAX],cached,have_key;
  char err[256];

  memset(&mv,0,sizeof(mv));
  memset(acc,0,sizeof(acc));
  memset(total,0,sizeof(total));
  processed=0;
  printf("Content-Type: text/html; charset=utf-8\r\n\r\n");
  if(!qsoz_config_load(&cfg,QSOZ_CONFIG_FILE,err,sizeof(err))) {
    printf("<html><body><pre>Configuration error</pre></body></html>");
    return 0;
  }
  con=mysql_init(NULL);
  if(con==NULL) {
    printf("<html><body><pre>Database initialization error</pre></body></html>");
    return 0;
  }
  if(mysql_real_connect(con,cfg.db_host,cfg.db_user,cfg.db_pass,cfg.db_name,cfg.db_port,NULL,0)==NULL) {
    printf("<html><body><pre>Database connection error</pre></body></html>");
    mysql_close(con);
    return 0;
  }
  cached=0;
  have_key=get_cache_key(con,cfg.db_name,&key_before);
  if(have_key)cached=cache_load(&key_before,acc,total,&mv,&processed);
  if(!cached) {
    if(!load_cq(con,cq) || !load_qso(con,cq,acc,total,&mv,&processed)) {
      printf("<html><body><pre>Database query error</pre></body></html>");
      free(mv.v);
      mysql_close(con);
      return 0;
    }
    qsort(mv.v,(size_t)mv.count,sizeof(MonthData),cmp_month);
    if(have_key && get_cache_key(con,cfg.db_name,&key_after) && cache_key_equal(&key_before,&key_after))
      cache_save(&key_after,acc,total,&mv,processed);
  }
  mysql_close(con);

  printf("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>FT8 symmetricity</title>");
  print_css();
  printf("</head><body><h1>Real time channel symmetricity data analysis on QSO collection</h1>");
  printf("<div class='section'><div class='card'><h2>Analysis of channel differences</h2>");
  print_line_chart(acc,total);
  printf("</div></div>");
  printf("<div class='section'><div class='card'><h2>Characteristic parameter analysis</h2>");
  print_table(acc,total);
  printf("<p class='muted'>Processed valid FT8/MFSK QSO: %lu</p></div></div>",processed);
  printf("<div class='section'><div class='card'><h2>Analysis by CQ zones</h2>");
  print_cq_chart(&mv);
  printf("</div></div></body></html>");
  free(mv.v);
  return 0;
}
