// Gianluca Mazzini @2022- Version 4.3
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <mysql/mysql.h>
#include "qsoz_contest.h"
#include "qsoz_stats.h"
#include "qsoz_time.h"
#include "qsoz_util.h"
#include "/home/tools/mcp/work/data/radio_data.h"

#define CONTEST_BANDS 13
#define CONTEST_CONTINENTS 7
#define CONTEST_DXCC_MAX 1024
#define CONTEST_QSO_MAX 1000000UL
#define CONTEST_EXPORT_DIR "/home/www/log/files"

typedef struct {
  long long when;
  int band;
  int dxcc;
  char call[QSOZ_STATS_LABEL];
} ContestQso;

static const int band_code[CONTEST_BANDS]={7,20,60,100,120,150,170,200,300,400,600,800,1600};
static const char *band_name[CONTEST_BANDS]={"70cm","2","6","10","12","15","17","20","30","40","60","80","160"};
static const char *continent_name[CONTEST_CONTINENTS]={"AF","AN","AS","EU","NA","OC","SA"};

static int contest_band(long freq) {
  int code,i;

  code=qsoz_band((int)(freq/1000000L));
  for(i=0;i<CONTEST_BANDS;i++)if(code==band_code[i])return i;
  return -1;
}

static int contest_continent(const char *cont) {
  int i;

  if(cont==NULL)return -1;
  for(i=0;i<CONTEST_CONTINENTS;i++)if(strncmp(cont,continent_name[i],2)==0)return i;
  return -1;
}

static void contest_time_key(long long epoch,char *out,unsigned long cap) {
  struct tm *t;
  time_t tt;

  if(out==NULL || cap==0)return;
  out[0]='\0';
  tt=(time_t)epoch;
  t=gmtime(&tt);
  if(t==NULL)return;
  strftime(out,(size_t)cap,"%Y-%m-%d:%H%M",t);
}

static int contest_load_continents(MYSQL *con,char cont[][3],unsigned char ambiguous[]) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  int dxcc;

  memset(cont,0,CONTEST_DXCC_MAX*3UL);
  memset(ambiguous,0,CONTEST_DXCC_MAX*sizeof(unsigned char));
  if(mysql_query(con,"select dxcc,min(cont),count(distinct cont) from cty group by dxcc")!=0)return 0;
  res=mysql_store_result(con);
  if(res==NULL)return 0;
  for(;;){
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    dxcc=atoi(row[0]);
    if(dxcc<0 || dxcc>=CONTEST_DXCC_MAX || row[1]==NULL)continue;
    cont[dxcc][0]=row[1][0];
    cont[dxcc][1]=row[1][1];
    cont[dxcc][2]='\0';
    if(row[2]!=NULL && atoi(row[2])>1)ambiguous[dxcc]=1;
  }
  mysql_free_result(res);
  return 1;
}

static ContestQso *contest_load_qso(MYSQL *con,const char *esc_mycall,const char *esc_contest,unsigned long *count,int present[]) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  ContestQso *q,*newq;
  unsigned long n,cap,newcap;
  char query[1024];
  int b;

  *count=0;
  memset(present,0,CONTEST_BANDS*sizeof(int));
  snprintf(query,sizeof(query),"select open,freqtx,callsign,dxcc from log where mycall='%s' and contest='%s' order by open,callsign",esc_mycall,esc_contest);
  if(mysql_query(con,query)!=0)return NULL;
  res=mysql_use_result(con);
  if(res==NULL)return NULL;
  q=NULL;
  n=0;
  cap=0;
  for(;;){
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    if(n>=CONTEST_QSO_MAX){free(q); mysql_free_result(res); return NULL;}
    if(n==cap){
      newcap=cap==0?1024UL:cap*2UL;
      if(newcap>CONTEST_QSO_MAX)newcap=CONTEST_QSO_MAX;
      newq=(ContestQso *)realloc(q,(size_t)newcap*sizeof(ContestQso));
      if(newq==NULL){free(q); mysql_free_result(res); return NULL;}
      q=newq;
      cap=newcap;
    }
    q[n].when=atoll(row[0]);
    q[n].band=contest_band(atol(row[1]));
    q[n].dxcc=atoi(row[3]);
    if(!qsoz_copy(q[n].call,sizeof(q[n].call),row[2]))q[n].call[0]='\0';
    b=q[n].band;
    if(b>=0)present[b]=1;
    n++;
  }
  mysql_free_result(res);
  *count=n;
  return q;
}

static void contest_print_ontime(const ContestQso *q,unsigned long count) {
  unsigned long i;
  long long presence,pause,on,delta;

  presence=0;
  pause=0;
  on=0;
  if(count>1){
    presence=q[count-1].when-q[0].when;
    for(i=1;i<count;i++){
      delta=q[i].when-q[i-1].when;
      if(delta>=3600)pause+=delta;
    }
    on=presence-pause;
  }
  printf("<p class=\"myh2\">Operating time</p><pre>");
  printf("Total presence [s h]: %lld %5.2f\n",presence,(double)presence/3600.0);
  printf("Total pause    [s h]: %lld %5.2f\n",pause,(double)pause/3600.0);
  printf("Total on times [s h]: %lld %5.2f\n",on,(double)on/3600.0);
  printf("</pre>");
}

static void contest_print_matrix(MYSQL *con,const ContestQso *q,unsigned long count,const int present[],char dxcc_cont[][3],const unsigned char ambiguous[]) {
  long matrix[CONTEST_BANDS][CONTEST_CONTINENTS],total,band_total,cached;
  unsigned long i;
  int b,c,j,rc;
  RadioCty cty;

  memset(matrix,0,sizeof(matrix));
  qsoz_stats_reset();
  for(i=0;i<count;i++){
    b=q[i].band;
    if(b<0 || q[i].call[0]=='\0')continue;
    if(numdata3(0,b,q[i].call)!=0)continue;
    if(incdata3(0,b,q[i].call,1,0)<0)continue;
    if(q[i].dxcc<0 || q[i].dxcc>=CONTEST_DXCC_MAX)continue;
    if(ambiguous[q[i].dxcc]){
      cached=numdata3(1,0,q[i].call);
      if(cached==0){
        rc=radio_cty_lookup(con,q[i].call,&cty);
        c=rc==1?contest_continent(cty.cont):-1;
        cached=c>=0?(long)c+1L:100L;
        incdata3(1,0,q[i].call,cached,0);
      }
      c=(cached>=1 && cached<=CONTEST_CONTINENTS)?(int)(cached-1):-1;
    }
    else c=contest_continent(dxcc_cont[q[i].dxcc]);
    if(c>=0)matrix[b][c]++;
  }
  printf("<p class=\"myh2\">Unique callsigns by band and continent</p><pre>");
  printf("BAND");
  for(c=0;c<CONTEST_CONTINENTS;c++)printf(" %6s",continent_name[c]);
  printf(" %6s\n","TOTAL");
  for(b=0;b<CONTEST_BANDS;b++){
    if(!present[b])continue;
    band_total=0;
    printf("%4s",band_name[b]);
    for(c=0;c<CONTEST_CONTINENTS;c++){printf(" %6ld",matrix[b][c]); band_total+=matrix[b][c];}
    printf(" %6ld\n",band_total);
  }
  printf(" TOT");
  total=0;
  for(c=0;c<CONTEST_CONTINENTS;c++){
    band_total=0;
    for(j=0;j<CONTEST_BANDS;j++)band_total+=matrix[j][c];
    printf(" %6ld",band_total);
    total+=band_total;
  }
  printf(" %6ld\n</pre>",total);
}

static FILE *contest_export_open(const char *kind,char *name,unsigned long namecap) {
  char path[512];
  long now,pid;

  now=(long)time(NULL);
  pid=(long)getpid();
  snprintf(name,(size_t)namecap,"%s_%ld_%ld.csv",kind,now,pid);
  snprintf(path,sizeof(path),"%s/%s",CONTEST_EXPORT_DIR,name);
  return fopen(path,"w");
}

static void contest_print_band_growth(const ContestQso *q,unsigned long count,const int present[]) {
  FILE *fp;
  char name[128],key[32];
  unsigned long i,j;
  long long minute;
  int b;

  fp=contest_export_open("qsoband",name,sizeof(name));
  printf("<p class=\"myh2\">Cumulative unique callsigns by band</p>");
  if(fp!=NULL)printf("<a href='/files/%s' download>Download band breakdown CSV</a><br>",name);
  printf("<pre>DATA:TIME");
  if(fp!=NULL)fprintf(fp,"DATA:TIME");
  for(b=0;b<CONTEST_BANDS;b++)if(present[b]){printf(",%s",band_name[b]); if(fp!=NULL)fprintf(fp,",%s",band_name[b]);}
  printf("\n");
  if(fp!=NULL)fprintf(fp,"\n");
  qsoz_stats_reset();
  i=0;
  for(;i<count;){
    minute=(q[i].when/60LL)*60LL;
    j=i;
    for(;j<count && (q[j].when/60LL)*60LL==minute;j++){
      b=q[j].band;
      if(b>=0 && q[j].call[0]!='\0')incdata3(0,b,q[j].call,1,0);
    }
    contest_time_key(minute,key,sizeof(key));
    printf("%s",key);
    if(fp!=NULL)fprintf(fp,"%s",key);
    for(b=0;b<CONTEST_BANDS;b++)if(present[b]){
      printf(",%ld",ndata3[0][b]);
      if(fp!=NULL)fprintf(fp,",%ld",ndata3[0][b]);
    }
    printf("\n");
    if(fp!=NULL)fprintf(fp,"\n");
    i=j;
  }
  printf("</pre>");
  if(fp!=NULL)fclose(fp);
}

static void contest_print_rate(const ContestQso *q,unsigned long count,const int present[]) {
  FILE *fp;
  char name[128],key[32];
  unsigned long lo,hi;
  long band_count[CONTEST_BANDS];
  long long sample,last;
  int b;

  fp=contest_export_open("qsorate",name,sizeof(name));
  printf("<p class=\"myh2\">QSO rate / 1 hour, 5 minute steps</p>");
  if(fp!=NULL)printf("<a href='/files/%s' download>Download QSO rate CSV</a><br>",name);
  printf("<pre>DATA:TIME,*");
  if(fp!=NULL)fprintf(fp,"DATA:TIME,*");
  for(b=0;b<CONTEST_BANDS;b++)if(present[b]){printf(",%s",band_name[b]); if(fp!=NULL)fprintf(fp,",%s",band_name[b]);}
  printf("\n");
  if(fp!=NULL)fprintf(fp,"\n");
  if(count==0){printf("</pre>"); if(fp!=NULL)fclose(fp); return;}
  memset(band_count,0,sizeof(band_count));
  lo=0;
  hi=0;
  sample=(q[0].when/60LL)*60LL;
  last=q[count-1].when;
  for(;sample<=last;sample+=300LL){
    for(;lo<hi && q[lo].when<sample;lo++){
      b=q[lo].band;
      if(b>=0)band_count[b]--;
    }
    for(;hi<count && q[hi].when<sample+3600LL;hi++){
      b=q[hi].band;
      if(b>=0)band_count[b]++;
    }
    contest_time_key(sample,key,sizeof(key));
    printf("%s,%lu",key,hi-lo);
    if(fp!=NULL)fprintf(fp,"%s,%lu",key,hi-lo);
    for(b=0;b<CONTEST_BANDS;b++)if(present[b]){
      printf(",%ld",band_count[b]);
      if(fp!=NULL)fprintf(fp,",%ld",band_count[b]);
    }
    printf("\n");
    if(fp!=NULL)fprintf(fp,"\n");
  }
  printf("</pre>");
  if(fp!=NULL)fclose(fp);
}

int qsoz_contest_details_print(MYSQL *con,const char *esc_mycall,const char *esc_contest) {
  ContestQso *q;
  unsigned long count;
  int present[CONTEST_BANDS];
  char dxcc_cont[CONTEST_DXCC_MAX][3];
  unsigned char ambiguous[CONTEST_DXCC_MAX];

  if(con==NULL || esc_mycall==NULL || esc_contest==NULL)return 0;
  q=contest_load_qso(con,esc_mycall,esc_contest,&count,present);
  if(q==NULL || count==0){free(q); return 0;}
  if(!contest_load_continents(con,dxcc_cont,ambiguous)){free(q); return 0;}
  contest_print_ontime(q,count);
  contest_print_matrix(con,q,count,present,dxcc_cont,ambiguous);
  contest_print_band_growth(q,count,present);
  contest_print_rate(q,count,present);
  free(q);
  return 1;
}
