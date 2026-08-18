// Gianluca Mazzini @2022- Version 3.04
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <mysql/mysql.h>
#include "qsoz_util.h"
#include "qsoz_stats.h"
#include "qsoz_net.h"
#include "qsoz_request.h"
#include "qsoz_db.h"
#include "qsoz_html.h"
#include "qsoz_time.h"
#include "qsoz_config.h"
#include "/home/tools/mcp/work/data/radio_client.h"
#include "/home/tools/mcp/work/data/radio_data.h"
#define QSLWIN 240
#define TIMEOUT_AUX1 3600

#include "pscore.h"

static int next_token_copy(char *first,char **save,char *dst,unsigned long cap) {
  char *p;

  p=strtok_r(first," \t\r",save);
  if(p==NULL)return 0;
  return qsoz_copy(dst,cap,p);
}

static int parse_lzh_qso(char *line,char *clock,unsigned long clock_cap,char *call,unsigned long call_cap,
                         char *sigtx,unsigned long sigtx_cap,char *sigrx,unsigned long sigrx_cap) {
  char *save;

  save=NULL;
  if(!next_token_copy(line,&save,clock,clock_cap) || !next_token_copy(NULL,&save,call,call_cap))return 0;
  if(!next_token_copy(NULL,&save,sigtx,sigtx_cap)){
    if(!qsoz_copy(sigtx,sigtx_cap,"59"))return 0;
  }
  if(!next_token_copy(NULL,&save,sigrx,sigrx_cap)){
    if(!qsoz_copy(sigrx,sigrx_cap,"59"))return 0;
  }
  return 1;
}

static int parse_cbr_qso(char *line,char *freq,unsigned long freq_cap,char *mode,unsigned long mode_cap,
                         char *date,unsigned long date_cap,char *clock,unsigned long clock_cap,
                         char *sigtx,unsigned long sigtx_cap,char *contesttx,unsigned long contesttx_cap,
                         char *call,unsigned long call_cap,char *sigrx,unsigned long sigrx_cap,
                         char *contestrx,unsigned long contestrx_cap) {
  char dummy[300],*save;
  int fields;

  fields=qsoz_nfields(line);
  save=NULL;
  if(!next_token_copy(line,&save,dummy,sizeof(dummy)) ||
     !next_token_copy(NULL,&save,freq,freq_cap) ||
     !next_token_copy(NULL,&save,mode,mode_cap) ||
     !next_token_copy(NULL,&save,date,date_cap) ||
     !next_token_copy(NULL,&save,clock,clock_cap) ||
     !next_token_copy(NULL,&save,dummy,sizeof(dummy)))return 0;
  if(fields>10){
    if(!next_token_copy(NULL,&save,sigtx,sigtx_cap))return 0;
  } else if(!qsoz_copy(sigtx,sigtx_cap,""))return 0;
  if(!next_token_copy(NULL,&save,contesttx,contesttx_cap) || !next_token_copy(NULL,&save,call,call_cap))return 0;
  if(fields>10){
    if(!next_token_copy(NULL,&save,sigrx,sigrx_cap))return 0;
  } else if(!qsoz_copy(sigrx,sigrx_cap,""))return 0;
  if(!next_token_copy(NULL,&save,contestrx,contestrx_cap))return 0;
  return 1;
}

typedef struct {
  long long epoch;
  long freq;
  long dxcc_qso,dxcc_qsl,call_qso,call_qsl,last;
  int dxcc,dxcc_cache_fresh;
  char spotter[20],dx[20];
} QsozClusterSpot;

static void print_callbook_result(MYSQL *con,const char *esc_call,const char *response) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  char query[1024],h1[2048],h2[2048],h3[2048],h4[2048];

  printf("<pre>");
  if(qsoz_html_text(h1,sizeof(h1),response))printf("%s",h1);
  snprintf(query,sizeof(query),"select firstname,lastname,addr1,addr2,state,zip,country,grid,email,cqzone,ituzone,born,src,image from who where callsign='%s'",esc_call);
  if(mysql_query(con,query)!=0){printf("Callbook database error\n</pre>"); return;}
  res=mysql_store_result(con);
  if(res==NULL){printf("Callbook database error\n</pre>"); return;}
  row=mysql_fetch_row(res);
  if(row==NULL){
    printf("No callbook data stored\n</pre>");
    mysql_free_result(res);
    return;
  }
  qsoz_html_text(h1,sizeof(h1),row[0]); qsoz_html_text(h2,sizeof(h2),row[1]);
  printf("\n%s %s\n",h1,h2);
  qsoz_html_text(h1,sizeof(h1),row[2]); qsoz_html_text(h2,sizeof(h2),row[3]);
  printf("%s\n%s\n",h1,h2);
  qsoz_html_text(h1,sizeof(h1),row[4]); qsoz_html_text(h2,sizeof(h2),row[5]); qsoz_html_text(h3,sizeof(h3),row[6]);
  printf("%s %s %s\n",h1,h2,h3);
  qsoz_html_text(h1,sizeof(h1),row[7]); qsoz_html_text(h2,sizeof(h2),row[8]);
  printf("grid:%s email:%s\n",h1,h2);
  qsoz_html_text(h1,sizeof(h1),row[9]); qsoz_html_text(h2,sizeof(h2),row[10]); qsoz_html_text(h3,sizeof(h3),row[11]); qsoz_html_text(h4,sizeof(h4),row[12]);
  printf("cq:%s itu:%s born:%s source:%s\n",h1,h2,h3,h4);
  printf("</pre>");
  if(row[13]!=NULL && row[13][0]!='\0' && qsoz_html_attr(h1,sizeof(h1),row[13]))printf("<img src=\"%s\" width=\"200\" style=\"cursor:zoom-in\" onclick=\"openImgExact(this.src)\">",h1);
  mysql_free_result(res);
}

static int cluster_first_dxcc(QsozClusterSpot *spot,int index) {
  int i;

  for(i=0;i<index;i++)if(spot[i].dxcc==spot[index].dxcc)return 0;
  return 1;
}

static void cluster_dxcc_stats(MYSQL *con,const char *esc_mycall,QsozClusterSpot *spot,int count) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  char *query,*p;
  unsigned long cap,left;
  long long now;
  int i,dxcc,n,stale,first;

  if(count<=0)return;
  now=(long long)time(NULL);
  cap=1024UL+(unsigned long)count*32UL;
  query=(char *)malloc((size_t)cap);
  if(query==NULL)return;

  n=snprintf(query,(size_t)cap,"select dxcc,qso,qsl,time from aux1 where mycall='%s' and dxcc in (",esc_mycall);
  if(n<0 || (unsigned long)n>=cap){free(query); return;}
  p=query+n;
  left=cap-(unsigned long)n;
  first=1;
  for(i=0;i<count;i++){
    if(!cluster_first_dxcc(spot,i))continue;
    n=sprintf(p,"%s%d",first?"":",",spot[i].dxcc);
    if(n<0 || (unsigned long)n>=left){free(query); return;}
    p+=n; left-=(unsigned long)n; first=0;
  }
  if(first || left<=2UL){free(query); return;}
  strcpy(p,")");
  if(mysql_query(con,query)!=0){free(query); return;}
  res=mysql_store_result(con);
  if(res==NULL){free(query); return;}
  for(;;){
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    if(now-atoll(row[3])>=TIMEOUT_AUX1)continue;
    dxcc=atoi(row[0]);
    for(i=0;i<count;i++)if(spot[i].dxcc==dxcc){
      spot[i].dxcc_qso=atol(row[1]);
      spot[i].dxcc_qsl=atol(row[2]);
      spot[i].dxcc_cache_fresh=1;
    }
  }
  mysql_free_result(res);

  stale=0;
  for(i=0;i<count;i++)if(!spot[i].dxcc_cache_fresh){stale=1; break;}
  if(!stale){free(query); return;}

  n=snprintf(query,(size_t)cap,"select dxcc,count(*),coalesce(sum(lotw)+sum(eqsl)+sum(qrz),0) from log where mycall='%s' and dxcc in (",esc_mycall);
  if(n<0 || (unsigned long)n>=cap){free(query); return;}
  p=query+n;
  left=cap-(unsigned long)n;
  first=1;
  for(i=0;i<count;i++){
    if(spot[i].dxcc_cache_fresh || !cluster_first_dxcc(spot,i))continue;
    n=sprintf(p,"%s%d",first?"":",",spot[i].dxcc);
    if(n<0 || (unsigned long)n>=left){free(query); return;}
    p+=n; left-=(unsigned long)n; first=0;
  }
  if(first || left<=strlen(") group by dxcc")){free(query); return;}
  strcpy(p,") group by dxcc");
  if(mysql_query(con,query)!=0){free(query); return;}
  res=mysql_store_result(con);
  if(res==NULL){free(query); return;}
  for(;;){
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    dxcc=atoi(row[0]);
    for(i=0;i<count;i++)if(spot[i].dxcc==dxcc && !spot[i].dxcc_cache_fresh){
      spot[i].dxcc_qso=atol(row[1]);
      spot[i].dxcc_qsl=atol(row[2]);
    }
  }
  mysql_free_result(res);

  n=snprintf(query,(size_t)cap,"replace into aux1 (mycall,dxcc,qso,qsl,time) values ");
  if(n<0 || (unsigned long)n>=cap){free(query); return;}
  p=query+n;
  left=cap-(unsigned long)n;
  first=1;
  for(i=0;i<count;i++){
    if(spot[i].dxcc_cache_fresh || !cluster_first_dxcc(spot,i))continue;
    n=snprintf(p,(size_t)left,"%s('%s',%d,%ld,%ld,%lld)",first?"":",",esc_mycall,spot[i].dxcc,spot[i].dxcc_qso,spot[i].dxcc_qsl,now);
    if(n<0 || (unsigned long)n>=left){free(query); return;}
    p+=n; left-=(unsigned long)n; first=0;
  }
  if(!first)mysql_query(con,query);
  free(query);
}

static void cluster_call_stats(MYSQL *con,const char *esc_mycall,QsozClusterSpot *spot,int count) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  char *query,*p,esc[64];
  unsigned long cap,left;
  int i,n;

  if(count<=0)return;
  cap=512UL+(unsigned long)count*64UL;
  query=(char *)malloc((size_t)cap);
  if(query==NULL)return;
  n=snprintf(query,(size_t)cap,"select callsign,count(*),coalesce(sum(lotw)+sum(eqsl)+sum(qrz),0),max(open) from log where mycall='%s' and callsign in (",esc_mycall);
  if(n<0 || (unsigned long)n>=cap){free(query); return;}
  p=query+n;
  left=cap-(unsigned long)n;
  for(i=0;i<count;i++){
    mysql_real_escape_string(con,esc,spot[i].dx,(unsigned long)strlen(spot[i].dx));
    n=sprintf(p,"%s'%s'",i==0?"":",",esc);
    if(n<0 || (unsigned long)n>=left){free(query); return;}
    p+=n; left-=(unsigned long)n;
  }
  if(left<=strlen(") group by callsign")){free(query); return;}
  strcpy(p,") group by callsign");
  if(mysql_query(con,query)!=0){free(query); return;}
  free(query);
  res=mysql_store_result(con);
  if(res==NULL)return;
  for(;;){
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    for(i=0;i<count;i++)if(strcmp(spot[i].dx,row[0])==0){
      spot[i].call_qso=atol(row[1]);
      spot[i].call_qsl=atol(row[2]);
      spot[i].last=row[3]==NULL?0:atol(row[3]);
    }
  }
  mysql_free_result(res);
}

static int parse_cluster_line(char *line,char **epoch,char **spotter,char **frequency,char **dx) {
  char *p1,*p2,*p3;

  if(line==NULL || epoch==NULL || spotter==NULL || frequency==NULL || dx==NULL)return 0;
  p1=strchr(line,',');
  if(p1==NULL)return 0;
  *p1='\0';
  p2=strchr(p1+1,',');
  if(p2==NULL)return 0;
  *p2='\0';
  p3=strchr(p2+1,',');
  if(p3==NULL || strchr(p3+1,',')!=NULL)return 0;
  *p3='\0';
  if(line[0]=='\0' || p1[1]=='\0' || p2[1]=='\0' || p3[1]=='\0')return 0;
  *epoch=line;
  *spotter=p1+1;
  *frequency=p2+1;
  *dx=p3+1;
  return 1;
}

int main(void){
  int c,act,vv,gg,s,mypage,f1,line_rc;
  QsozConfig cfg;
  char buf[8192],aux1[512],aux2[300],aux3[4096],aux4[512],aux5[300],aux6[300],aux7[300],aux8[300],aux9[4096],aux0[300],tok[13][100],mycall[16],cfgerr[256],callbook_response[256],cluster_line[1024],request_err[256],esc_mycall[64],esc_call[256],esc_contest[256],esc_cluster[256],esc_tx[256],esc_rx[256],html1[2048],html2[2048],html3[2048],html4[2048],js1[2048],*ff,*pp,*qq,*save1,*p1,*p2,*p3,*p4;
  struct tm ts,*tm_now;
  time_t epoch,td,open_epoch,close_epoch;
  long l1,l2,l3,l4,idx,suml[10],nnn,ppp,qqq;
  unsigned long lff;
  long long ll1,ll2,ll3;
  MYSQL *con;
  MYSQL_RES *res;
  MYSQL_ROW row;
  FILE *fp;
  double fx,f6,f7,f8,dist,bear;
  RadioCty cty,cty2;
  RadioAdif adif_rec;
  const char *adif_names[RADIO_ADIF_MAX_FIELDS],*adif_cursor;
  QsozLineReader cluster_reader;
  const char *l11[]={"call","band","mode","lotw","eqsl","qrz"};
 
  if(!qsoz_stats_init())exit(1);
  ff=NULL;
  if(!qsoz_request_read(tok,QSOZ_REQUEST_FIELDS,&ff,&lff,request_err,sizeof(request_err))){
    fprintf(stderr,"pproc: %s\n",request_err);
    printf("Status: 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nERROR\n");
    goto end_no_db;
  }
  mypage=atoi(tok[3]);
  if(!qsoz_token_valid(tok[0])){
    printf("Status: 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n<pre><b>Login expired</b>\nPlease login again\n</pre>");
    goto end_no_db;
  }

  if(!qsoz_config_load(&cfg,QSOZ_CONFIG_FILE,cfgerr,sizeof(cfgerr))){fprintf(stderr,"pproc: %s\n",cfgerr); goto end_no_db;}
  con=mysql_init(NULL);
  if(con==NULL)goto end_no_db;
  if(mysql_real_connect(con,cfg.db_host,cfg.db_user,cfg.db_pass,cfg.db_name,cfg.db_port,NULL,0)==NULL){fprintf(stderr,"pproc: mysql connect error: %s\n",mysql_error(con)); mysql_close(con); goto end_no_db;}
  mysql_query(con,"SET time_zone='+00:00'");
  sprintf(buf,"select mycall from user where ota='%s' and lastota+durationota>%ld limit 1",tok[0],time(NULL));
  mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res);
  if(row==NULL){
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n");
    printf("<pre><b>Login expired</b>\nPlease login again\n</pre>");
    mysql_free_result(res);
    goto end;
  }
  else strcpy(mycall,row[0]);
  mysql_free_result(res);
  if(!qsoz_db_escape(con,esc_mycall,sizeof(esc_mycall),mycall) ||
     !qsoz_db_escape(con,esc_call,sizeof(esc_call),tok[4]) ||
     !qsoz_db_escape(con,esc_contest,sizeof(esc_contest),tok[9]))goto end;
  act=0; if(tok[1][0]=='a')act=atoi(tok[1]+1);

  if(act==5){ // Go button with date in call input and format YYYYMMDD
    printf("Content-Type: text/plain\r\n\r\n");
    sprintf(buf,"select count(*) from log where mycall='%s' and open>=%lld order by open",esc_mycall,(long long)qsoz_date_clock_epoch(tok[4],"00:00:00"));
    mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res);
    l1=atol(row[0]);
    mysql_free_result(res);
    printf("%ld\n",l1);
    goto end;
  }

  if((act>=1 && act<=8) || (act>=28 && act<=30)){ // List buttons(4: 1 2 3 4) and List Find buttons(3: 5 6 7) and List Contest buttons(3:28 29 30) 
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    printf("<pre>");
    printf("<p class=\"myh1\">%22s%5s %16s %10s %4s %5s %5s</p>","DATETIME","LEN","CALLSIGN","FREQ","MODE","SIGTX","SIGRX");
    if(act<=5)sprintf(buf,"select open,close,callsign,freqtx,freqrx,mode,signaltx,signalrx,lotw,eqsl,qrz,contesttx,contestrx,contest from log where mycall='%s' order by open desc, callsign desc limit %d offset %ld",esc_mycall,mypage,atol(tok[2]));
    else if(act<=8)sprintf(buf,"select open,close,callsign,freqtx,freqrx,mode,signaltx,signalrx,lotw,eqsl,qrz,contesttx,contestrx,contest from log where callsign like '%s' and mycall='%s' order by open desc, callsign desc limit %d offset %ld",esc_call,esc_mycall,mypage,atol(tok[2]));
    else sprintf(buf,"select open,close,callsign,freqtx,freqrx,mode,signaltx,signalrx,lotw,eqsl,qrz,contesttx,contestrx,contest from log where contest='%s' and mycall='%s' order by open desc, callsign desc limit %d offset %ld",esc_contest,esc_mycall,mypage,atol(tok[2]));
    mysql_query(con,buf);
    res=mysql_store_result(con);
    for(;;){
      row=mysql_fetch_row(res);
      if(row==NULL)break;
      aux1[0]='\0';
      if(atoi(row[8])==1)strcat(aux1,"L");
      if(atoi(row[9])==1)strcat(aux1,"E");
      if(atoi(row[10])==1)strcat(aux1,"Q");
      td=atoll(row[1])-atoll(row[0]);
      if(td==0)strcpy(aux2,"(0s)");
      else if(td<60)sprintf(aux2,"(%lds)",td);
      else if(td<3600)sprintf(aux2,"(%ldm)",td/60);
      else sprintf(aux2,"(%ldh)",td/3600);
      if(!qsoz_html_js_sq_attr(js1,sizeof(js1),row[2]))js1[0]='\0';
      if(!qsoz_html_text(html1,sizeof(html1),row[2]))html1[0]='\0';
      if(!qsoz_html_text(html2,sizeof(html2),row[5]))html2[0]='\0';
      if(!qsoz_html_text(html3,sizeof(html3),row[6]))html3[0]='\0';
      if(!qsoz_html_text(html4,sizeof(html4),row[7]))html4[0]='\0';
      printf("<button type=\"button\" class=\"myb2\" onclick=\"cmd1(%lld,'%s')\"> </button> ",atoll(row[0]),js1);
      printf("%s%5s <b>%16s</b> %10.1f %4s %5s %5s %-3s ",qsoz_epoch_text(atoll(row[0])),aux2,html1,atol(row[3])/1000.0,html2,html3,html4,aux1);
      if(row[13][0]!='\0'){
        if(!qsoz_html_text(html1,sizeof(html1),row[13]))html1[0]='\0';
        if(!qsoz_html_text(html2,sizeof(html2),row[11]))html2[0]='\0';
        if(!qsoz_html_text(html3,sizeof(html3),row[12]))html3[0]='\0';
        printf(" (%s,%s,%s)",html1,html2,html3);
      }
      if(atol(row[4])>0&&atol(row[4])!=atol(row[3]))printf(" [%+.1f]",(atol(row[4])-atol(row[3]))/1000.0);
      printf("\n");
    }
    mysql_free_result(res);
    printf("</pre>");
    goto end;
  }
 
  if(act==9){ // dxcc and qrz solve unset button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    printf("<pre>");
    l1=l2=0;
    sprintf(buf,"select open,callsign from log where mycall='%s' and dxcc=0",esc_mycall);
    mysql_query(con,buf);
    res=mysql_store_result(con);
    for(;;){
      row=mysql_fetch_row(res);
      if(row==NULL)break;
      if(radio_cty_lookup(con,row[1],&cty)==1 && qsoz_db_escape(con,esc_cluster,sizeof(esc_cluster),row[1])){
        sprintf(aux1,"Update log set dxcc=%d where mycall='%s' and open=%lld and callsign='%s' and dxcc=0",atoi(cty.dxcc),esc_mycall,atoll(row[0]),esc_cluster);
        mysql_query(con,aux1);
        l1++;
      }
      else l2++;
    }
    mysql_free_result(res);
    printf("Set dxcc: %ld\nNot found dxcc: %ld\n",l1,l2);
    printf("</pre>");
    goto end;
  }

  if(act==10){ // Report button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    printf("<pre>");
    qsoz_stats_reset();
    sprintf(buf,"select callsign,freqtx,mode,lotw,eqsl,qrz,dxcc from log where mycall='%s'",esc_mycall);
    mysql_query(con,buf);
    res=mysql_use_result(con);
    for(;;){
      row=mysql_fetch_row(res);
      if(row==NULL)break;
      c=(int)(atol(row[1])/1000000.0);
      if(c>433)continue;
      sprintf(aux1,"%04d%s",qsoz_band(c),qsoz_mode(row[2]));
      strcpy(aux2,qsoz_wpx(row[0]));
      idx=incdata3(0,0,aux1,1,1);
      incdata3(1,idx,row[0],1,1);
      incdata3(1,TOTL2-1,row[0],1,1);
      incdata3(3,idx,aux2,1,1);
      incdata3(3,TOTL2-1,aux2,1,1);
      if(atoi(row[3])==1)incdata3(0,1,aux1,1,1);
      if(atoi(row[4])==1)incdata3(0,2,aux1,1,1);
      if(atoi(row[5])==1)incdata3(0,3,aux1,1,1);
      sprintf(aux1,"%03d",atoi(row[6]));
      idx=incdata3(0,4,aux1,1,1);
      incdata3(2,idx,row[0],1,1);
      incdata3(4,idx,aux2,1,1);
      if(atoi(row[3])==1)incdata3(0,5,aux1,1,1);
      if(atoi(row[4])==1)incdata3(0,6,aux1,1,1);
      if(atoi(row[5])==1)incdata3(0,7,aux1,1,1);
    }
    mysql_free_result(res);
    qsoz_stats_sort_bucket(0,0);
    qsoz_stats_sort_bucket(0,4);

    printf("<p class=\"myh1\">%6s %7s %8s %8s %8s %8s %8s</p>","B/Mode","QSO","QSO.uniq","QSO.wpx","QSL.LOTW","QSL.EQSL","QSL.QRZ");
    for(c=0;c<4;c++)for(suml[c]=0,l1=0;l1<ndata3[0][c];l1++)suml[c]+=data3[0][c][l1].num;
    printf("<p class=\"myh2\">%6s %7ld %8ld %8ld %8ld %8ld %8ld</p>","Tot",suml[0],ndata3[1][TOTL2-1],ndata3[3][TOTL2-1],suml[1],suml[2],suml[3]);
    for(l1=0;l1<ndata3[0][0];l1++)printf("%6s %7ld %8ld %8ld %8ld %8ld %8ld\n",data3[0][0][l1].lab,data3[0][0][l1].num,ndata3[1][data3[0][0][l1].idx],ndata3[2][data3[0][0][l1].idx],numdata3(0,1,data3[0][0][l1].lab),numdata3(0,2,data3[0][0][l1].lab),numdata3(0,3,data3[0][0][l1].lab));
    printf("\n");
    qsort(data3[0][4],ndata3[0][4],sizeof(struct data3),cmp3);
    printf("<p class=\"myh1\">%6s %7s %8s %8s %8s %8s %8s %s</p>","dxcc","QSO","QSO.uniq","QSO.wpx","QSL.LOTW","QSL.EQSL","QSL.QRZ","Country");
    printf("<p class=\"myh2\">%6s %7ld %8s %8s %8ld %8ld %8ld</p>","Tot",ndata3[0][4],"","",ndata3[0][5],ndata3[0][6],ndata3[0][7]);
    for(l1=0;l1<ndata3[0][4];l1++){
      printf("%6s %7ld %8ld %8ld %8ld %8ld %8ld",data3[0][4][l1].lab,data3[0][4][l1].num,ndata3[2][data3[0][4][l1].idx],ndata3[4][data3[0][4][l1].idx],numdata3(0,5,data3[0][4][l1].lab),numdata3(0,6,data3[0][4][l1].lab),numdata3(0,7,data3[0][4][l1].lab));
      sprintf(buf,"select name from cty where dxcc='%d' limit 1",atoi(data3[0][4][l1].lab));
      mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res);
      if(row!=NULL){if(!qsoz_html_text(html1,sizeof(html1),row[0]))html1[0]='\0'; printf(" %s",html1);}
      mysql_free_result(res);
      printf("\n");
    }
    printf("</pre>");
    goto end;
  }

  if(act==11){ // Curio button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    qsoz_stats_reset();
    sprintf(buf,"select callsign,freqtx,mode,lotw,eqsl,qrz,dxcc from log where mycall='%s'",esc_mycall);
    mysql_query(con,buf);
    res=mysql_use_result(con);
    for(;;){
      row=mysql_fetch_row(res);
      if(row==NULL)break;
      c=(int)(atol(row[1])/1000000.0);
      if(c>433)continue;
      incdata3(0,0,row[0],1,1);
      sprintf(aux1,"%04d",qsoz_band(c));
      incdata3(0,1,aux1,1,1);
      incdata3(0,2,row[2],1,1);
      if(atoi(row[3])==1)incdata3(0,3,row[0],1,1); 
      if(atoi(row[4])==1)incdata3(0,4,row[0],1,1);
      if(atoi(row[5])==1)incdata3(0,5,row[0],1,1);
    }
    mysql_free_result(res);
    printf("<table>");
    for(c=0;c<6;c++){
      qsoz_stats_sort_bucket(0,c);
      qsort(data3[0][c],ndata3[0][c],sizeof(struct data3),cmp3);
      printf("<td><pre><b>%7s     #</b>\n",l11[c]);
      for(l1=0,l2=qsoz_min_long(ndata3[0][c],mypage);l1<l2;l1++)printf("%7.7s %6ld\n",data3[0][c][l1].lab,data3[0][c][l1].num);
      printf("</pre></td>");
    }
    printf("</table>");
    goto end;
  }

  if(act==12){ // Activity button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    printf("<pre>");
    qsoz_stats_reset();
    epoch=time(NULL); tm_now=gmtime(&epoch); ts=*tm_now;
    ts.tm_year-=2; timegm(&ts);
    strftime(aux3,sizeof(aux3),"%Y-%m",&ts);
    strftime(aux4,sizeof(aux4),"%Y-%m",tm_now);
    ts.tm_year+=2; ts.tm_mon-=1; timegm(&ts);
    strftime(aux5,sizeof(aux5),"%Y-%m-%d",&ts);
    strftime(aux6,sizeof(aux6),"%Y-%m-%d",tm_now);
    sprintf(buf,"select callsign,open,mode,lotw,eqsl,qrz,dxcc from log where mycall='%s'",esc_mycall);
    mysql_query(con,buf);
    res=mysql_use_result(con);
    for(;;){
      row=mysql_fetch_row(res);
      if(row==NULL)break;
      strcpy(aux2,qsoz_mode(row[2]));
      sprintf(aux1,"%.4s",qsoz_epoch_text(atoll(row[1])));
      idx=incdata3(0,0,aux1,1,1);
      incdata3(1,idx,row[0],1,1);
      incdata3(2,idx,qsoz_wpx(row[0]),1,1);
      incdata3(3,idx,row[6],1,1);
      if(atoi(row[3])==1)incdata3(0,1,aux1,1,1);
      if(atoi(row[4])==1)incdata3(0,2,aux1,1,1);
      if(atoi(row[5])==1)incdata3(0,3,aux1,1,1);
      if(strcmp(aux2,"CW")==0)incdata3(0,4,aux1,1,1);
      if(strcmp(aux2,"DG")==0)incdata3(0,5,aux1,1,1);
      if(strcmp(aux2,"PH")==0)incdata3(0,6,aux1,1,1);
      sprintf(aux1,"%.7s",qsoz_epoch_text(atoll(row[1])));
      if(strcmp(aux1,aux3)>=0 && strcmp(aux1,aux4)<=0){
        idx=incdata3(0,0,aux1,1,1);
        incdata3(1,idx,row[0],1,1);
        incdata3(2,idx,qsoz_wpx(row[0]),1,1);
        incdata3(3,idx,row[6],1,1);
        if(atoi(row[3])==1)incdata3(0,1,aux1,1,1);
        if(atoi(row[4])==1)incdata3(0,2,aux1,1,1);
        if(atoi(row[5])==1)incdata3(0,3,aux1,1,1);
        if(strcmp(aux2,"CW")==0)incdata3(0,4,aux1,1,1);
        if(strcmp(aux2,"DG")==0)incdata3(0,5,aux1,1,1);
        if(strcmp(aux2,"PH")==0)incdata3(0,6,aux1,1,1);
      }
      sprintf(aux1,"%.10s",qsoz_epoch_text(atoll(row[1])));
      if(strcmp(aux1,aux5)>=0 && strcmp(aux1,aux6)<=0){
        idx=incdata3(0,0,aux1,1,1);
        incdata3(1,idx,row[0],1,1);
        incdata3(2,idx,qsoz_wpx(row[0]),1,1);
        incdata3(3,idx,row[6],1,1);
        if(atoi(row[3])==1)incdata3(0,1,aux1,1,1);
        if(atoi(row[4])==1)incdata3(0,2,aux1,1,1);
        if(atoi(row[5])==1)incdata3(0,3,aux1,1,1);
        if(strcmp(aux2,"CW")==0)incdata3(0,4,aux1,1,1);
        if(strcmp(aux2,"DG")==0)incdata3(0,5,aux1,1,1);
        if(strcmp(aux2,"PH")==0)incdata3(0,6,aux1,1,1);
      } 
    }
    mysql_free_result(res);
    qsoz_stats_sort_bucket(0,0);

    suml[0]=4; suml[1]=7; suml[2]=10;
    strcpy(aux1,"YYYY-MM-DD");
    for(c=0;c<3;c++){
      printf("<p class=\"myh1\">%10.*s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s</p>",(int)suml[c],aux1,"QSO","QSO.cw","QSO.dg","QSO.ph","QSO.uniq","QSO.wpx","DXCC","QSL.LOTW","QSL.EQSL","QSL.QRZ");
      for(l1=ndata3[0][0]-1;l1>0;l1--){
        if((long)strlen(data3[0][0][l1].lab)==suml[c]){
          printf("%10s %8ld %8ld %8ld %8ld",data3[0][0][l1].lab,data3[0][0][l1].num,numdata3(0,4,data3[0][0][l1].lab),numdata3(0,5,data3[0][0][l1].lab),numdata3(0,6,data3[0][0][l1].lab));
          printf(" %8ld %8ld %8ld",ndata3[1][data3[0][0][l1].idx],ndata3[2][data3[0][0][l1].idx],ndata3[3][data3[0][0][l1].idx]);
          printf(" %8ld %8ld %8ld\n",numdata3(0,1,data3[0][0][l1].lab),numdata3(0,2,data3[0][0][l1].lab),numdata3(0,3,data3[0][0][l1].lab));
        }
      }
    }
    printf("</pre>");
    goto end;
  }

  if(act>=17 && act<=19){ // QSL.lotw QSL.eqsl QSL.qrz buttons
    adif_names[0]="CALL"; adif_names[1]="TIME_ON"; adif_names[2]="QSO_DATE";
    if(act==17){adif_names[3]="APP_LoTW_RXQSL"; strcpy(aux4,"lotw");}
    else if(act==18){adif_names[3]="EQSL_QSLRDATE"; strcpy(aux4,"eqsl");}
    else if(act==19){adif_names[3]="app_qrzlog_status"; strcpy(aux4,"qrz");}
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    printf("<pre>");
    vv=4; adif_cursor=ff; gg=radio_adif_extract(&adif_cursor,adif_names,vv,&adif_rec);
    for(ppp=nnn=qqq=0;gg>0;){
      epoch=radio_adif_time(adif_rec.value[2],adif_rec.value[1]);
      if(adif_rec.value[3][0]!='\0'){
        if(epoch==(time_t)-1 || !qsoz_db_escape(con,esc_call,sizeof(esc_call),adif_rec.value[0])){ppp++; qqq++; gg=radio_adif_extract(&adif_cursor,adif_names,vv,&adif_rec); continue;}
        snprintf(buf,sizeof(buf),"select %s from log where mycall='%s' and callsign='%s' and open>=%lld and open<=%lld",aux4,esc_mycall,esc_call,(long long)(epoch-QSLWIN),(long long)(epoch+QSLWIN));
        mysql_query(con,buf); 
        res=mysql_store_result(con); 
        row=mysql_fetch_row(res); 
        if(row==NULL)c=-1; else c=atoi(row[0]); 
        mysql_free_result(res);
        ppp++;
        if(c==-1)qqq++;
        if(c==0){
          snprintf(buf,sizeof(buf),"update log set %s=1 where mycall='%s' and callsign='%s' and open>=%lld and open<=%lld",aux4,esc_mycall,esc_call,(long long)(epoch-QSLWIN),(long long)(epoch+QSLWIN));
          mysql_query(con,buf);
          nnn++;
        }
      }
      gg=radio_adif_extract(&adif_cursor,adif_names,vv,&adif_rec);
    }
    printf("QSL %s Processed: %ld\nNew QSL %s Inserted: %ld\nQSO %s Missed: %ld\n",aux4,ppp,aux4,nnn,aux4,qqq);
    printf("</pre>");
    goto end;
  }

  if(act==15){ // adi in button
    adif_names[0]="call"; adif_names[1]="freq"; adif_names[2]="freq_rx"; adif_names[3]="rst_sent"; adif_names[4]="rst_rcvd"; adif_names[5]="mode";
    adif_names[6]="time_on"; adif_names[7]="time_off"; adif_names[8]="stx_string"; adif_names[9]="stx"; adif_names[10]="srx_string"; adif_names[11]="srx";
    adif_names[12]="contest_id"; adif_names[13]="qso_date"; adif_names[14]="qso_date_off";
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    printf("<pre>");
    vv=15; adif_cursor=ff; gg=radio_adif_extract(&adif_cursor,adif_names,vv,&adif_rec);
    for(ppp=nnn=0;gg>0;){
      if(adif_rec.value[6][4]=='\0'){adif_rec.value[6][4]='0'; adif_rec.value[6][5]='0'; adif_rec.value[6][6]='\0';}
      if(adif_rec.value[14][0]=='\0')strcpy(adif_rec.value[14],adif_rec.value[13]);
      if(adif_rec.value[7][0]=='\0')strcpy(adif_rec.value[7],adif_rec.value[6]);
      if(adif_rec.value[7][4]=='\0'){adif_rec.value[7][4]='0'; adif_rec.value[7][5]='0'; adif_rec.value[7][6]='\0';}
      if(radio_cty_lookup(con,adif_rec.value[0],&cty)!=1)cty.dxcc[0]='\0';
      open_epoch=radio_adif_time(adif_rec.value[13],adif_rec.value[6]);
      close_epoch=radio_adif_time(adif_rec.value[14],adif_rec.value[7]);
      if(open_epoch==(time_t)-1 || close_epoch==(time_t)-1){ppp++; gg=radio_adif_extract(&adif_cursor,adif_names,vv,&adif_rec); continue;}
      if(!qsoz_db_log_values(con,aux3,sizeof(aux3),mycall,adif_rec.value[0],adif_rec.value[5],
                             (long)(atof(adif_rec.value[1])*1000000.0),(long)(atof(adif_rec.value[2])*1000000.0),
                             adif_rec.value[3],adif_rec.value[4],
                             (adif_rec.value[8][0]=='\0')?adif_rec.value[9]:adif_rec.value[8],
                             (adif_rec.value[10][0]=='\0')?adif_rec.value[11]:adif_rec.value[10],adif_rec.value[12],
                             atoi(cty.dxcc),(long long)open_epoch,(long long)close_epoch)){
        ppp++; gg=radio_adif_extract(&adif_cursor,adif_names,vv,&adif_rec); continue;
      }
      snprintf(buf,sizeof(buf),"insert ignore into log (mycall,callsign,mode,freqtx,freqrx,signaltx,signalrx,contesttx,contestrx,contest,dxcc,open,close) value %s",aux3);
      mysql_query(con,buf);
      l1=mysql_affected_rows(con);
      if(l1>0){nnn+=l1; printf("%s\n",aux3);}
      ppp++;
      gg=radio_adif_extract(&adif_cursor,adif_names,vv,&adif_rec);
    }
    printf("QSO Processed: %ld\nNew QSO Inserted: %ld\n",ppp,nnn);
    printf("</pre>");
    goto end;
  }

   if(act==20){ // adi out button
     adif_names[0]="export_from"; adif_names[1]="export_to"; adif_names[2]="export_contest";
     printf("Status: 200 OK\r\n");
     printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
     vv=3; adif_cursor=ff; gg=radio_adif_extract(&adif_cursor,adif_names,vv,&adif_rec);
     if(gg==0)goto end;
     srand((unsigned)time(NULL));
     sprintf(aux1,"%d%d%d%d.adi",rand(),rand(),rand(),rand());
     sprintf(aux2,"/home/www/log/files/%s",aux1);
     fp=fopen(aux2,"w");
     strcpy(aux3,"PROGRAMID"); fprintf(fp,"<LZHlogger:%lu>%s\n",(unsigned long)strlen(aux3),aux3);
     fprintf(fp,"<EOH>\n\n");
     if(adif_rec.value[2][0]=='\0')snprintf(buf,sizeof(buf),"select open,callsign,freqtx,mode,signaltx,signalrx,close,freqrx,contesttx,contestrx,contest from log where mycall='%s' and open>=%lld and open<=%lld order by open",esc_mycall,(long long)qsoz_datetime_epoch(adif_rec.value[0]),(long long)qsoz_datetime_epoch(adif_rec.value[1]));
     else {
       if(!qsoz_db_escape(con,esc_contest,sizeof(esc_contest),adif_rec.value[2]))goto end;
       snprintf(buf,sizeof(buf),"select open,callsign,freqtx,mode,signaltx,signalrx,close,freqrx,contesttx,contestrx,contest from log where mycall='%s' and contest='%s' order by open",esc_mycall,esc_contest);
     }
     mysql_query(con,buf);
     res=mysql_store_result(con);
     for(l1=0;;l1++){
       row=mysql_fetch_row(res);
       if(row==NULL)break;
       fprintf(fp,"<CALL:%lu>%s\n",(unsigned long)strlen(row[1]),row[1]);
       p1=qsoz_epoch_text(atoll(row[0]));
       fprintf(fp,"<QSO_DATE:8>%.4s%.2s%.2s\n",p1,p1+5,p1+8);
       fprintf(fp,"<TIME_ON:6>%.2s%.2s%.2s\n",p1+11,p1+14,p1+17);
       p1=qsoz_epoch_text(atoll(row[6]));
       fprintf(fp,"<QSO_DATE_OFF:8>%.4s%.2s%.2s\n",p1,p1+5,p1+8);
       fprintf(fp,"<TIME_OFF:6>%.2s%.2s%.2s\n",p1+11,p1+14,p1+17);
       sprintf(aux4,"%7.5f",atol(row[2])/1000000.0); fprintf(fp,"<FREQ:%lu>%s\n",(unsigned long)strlen(aux4),aux4);
       sprintf(aux4,"%7.5f",atol(row[7])/1000000.0); fprintf(fp,"<FREQ_RX:%lu>%s\n",(unsigned long)strlen(aux4),aux4);
       fprintf(fp,"<MODE:%lu>%s\n",(unsigned long)strlen(row[3]),row[3]);
       fprintf(fp,"<RST_SENT:%lu>%s\n",(unsigned long)strlen(row[4]),row[4]);
       fprintf(fp,"<RST_RCVD:%lu>%s\n",(unsigned long)strlen(row[5]),row[5]);
       fprintf(fp,"<STX_STRING:%lu>%s\n",(unsigned long)strlen(row[8]),row[8]);
       fprintf(fp,"<SRX_STRING:%lu>%s\n",(unsigned long)strlen(row[9]),row[9]);
       fprintf(fp,"<CONTEST_ID:%lu>%s\n",(unsigned long)strlen(row[10]),row[10]);
       fprintf(fp,"<EOR>\n\n");
     }
     res=mysql_store_result(con);
     fclose(fp);
     printf("<pre>");
     printf("<pre><a href='https://log.mazzini.org/files/%s' download>Download ADIF</a>\n",aux1);
     if(adif_rec.value[2][0]=='\0')printf("from:%s to:%s\n",adif_rec.value[0],adif_rec.value[1]);
     else printf("contest:%s\n",adif_rec.value[2]);
     printf("</pre>");
     goto end;
  }

  if(act==21){ // cbr out button
    adif_names[0]="export_from"; adif_names[1]="export_to"; adif_names[2]="export_contest";
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    vv=3; adif_cursor=ff; gg=radio_adif_extract(&adif_cursor,adif_names,vv,&adif_rec);
    if(gg==0)goto end;
    srand((unsigned)time(NULL));
    sprintf(aux1,"%d%d%d%d.cbr",rand(),rand(),rand(),rand());
    sprintf(aux2,"/home/www/log/files/%s",aux1);
    fp=fopen(aux2,"w");
    fprintf(fp,"-OF-LOG: 3.0\nCREATED-BY: IK4LZH logger\n");
    fprintf(fp,"CONTEST: xxxxxx\nCALLSIGN: %s\nOPERATORS: %s\n",mycall,mycall);
    fprintf(fp,"CATEGORY-OPERATOR: SINGLE-OP\nCATEGORY-ASSISTED: ASSISTED\nCATEGORY-BAND: ALL\nCATEGORY-POWER: LOW\nCATEGORY-TRANSMITTER: ONE\n");    
    sprintf(buf,"select firstname,lastname,addr1,addr2,state,zip,country,email from who where callsign='%s'",esc_mycall);
    mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res);
    fprintf(fp,"NAME: %s %s\n",row[0],row[1]);
    if(row[7][0]!='\0')fprintf(fp,"EMAIL: %s\n",row[7]);
    if(row[2][0]!='\0')fprintf(fp,"ADDRESS: %s\n",row[2]);
    if(row[3][0]!='\0')fprintf(fp,"ADDRESS-CITY: %s\n",row[3]);
    if(row[4][0]!='\0')fprintf(fp,"ADDRESS-STATE-PROVINCE: %s\n",row[4]);
    if(row[5][0]!='\0')fprintf(fp,"ADDRESS-POSTALCODE: %s\n",row[5]);
    if(row[6][0]!='\0')fprintf(fp,"ADDRESS-COUNTRY: %s\n",row[6]);
    fprintf(fp,"CLUB: Italian Contest Club\n");
    mysql_free_result(res);
    if(adif_rec.value[2][0]=='\0')snprintf(buf,sizeof(buf),"select open,callsign,freqtx,mode,signaltx,signalrx,contesttx,contestrx from log where mycall='%s' and open>=%lld and open<=%lld order by open",esc_mycall,(long long)qsoz_datetime_epoch(adif_rec.value[0]),(long long)qsoz_datetime_epoch(adif_rec.value[1]));
    else {
      if(!qsoz_db_escape(con,esc_contest,sizeof(esc_contest),adif_rec.value[2]))goto end;
      snprintf(buf,sizeof(buf),"select open,callsign,freqtx,mode,signaltx,signalrx,contesttx,contestrx from log where mycall='%s' and contest='%s' order by open",esc_mycall,esc_contest);
    }
    mysql_query(con,buf);
    res=mysql_store_result(con);
    for(l1=0;;l1++){
      row=mysql_fetch_row(res);
      if(row==NULL)break;
      p1=qsoz_epoch_text(atoll(row[0]));
      fprintf(fp,"QSO: %5ld %2s %.4s-%.2s-%.2s %.2s%.2s",atol(row[2])/1000L,qsoz_mode(row[3]),p1,p1+5,p1+8,p1+11,p1+14);
      fprintf(fp," %-13s %3s %-6s %-13s %3s %-6s 0\n",mycall,row[4],row[6],row[1],row[5],row[7]);
    }
    res=mysql_store_result(con);
    fprintf(fp,"END-OF-LOG:\n");
    fclose(fp);
    printf("<pre>");
    printf("<pre><a href='https://log.mazzini.org/files/%s' download>Download Cabrillo</a>\n",aux1);
    if(adif_rec.value[2][0]=='\0')printf("from:%s to:%s\n",adif_rec.value[0],adif_rec.value[1]);
    else printf("contest:%s\n",adif_rec.value[2]);
    printf("</pre>");
    goto end;
  }
  
  if(act==16){ // lzh in button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    printf("<pre>");
    aux1[0]=aux2[0]=aux3[0]='\0';
    pp=strtok(ff,"\n");
    for(ppp=nnn=0;;){
      if(pp==NULL)break;
      if(pp[0]=='D'){if(!qsoz_copy(aux1,sizeof(aux1),pp+1))aux1[0]='\0';}
      else if(pp[0]=='F'){if(!qsoz_copy(aux2,sizeof(aux2),pp+1))aux2[0]='\0';}
      else if(pp[0]=='M'){if(!qsoz_copy(aux3,sizeof(aux3),pp+1))aux3[0]='\0';}
      else if(pp[0]!='\0' && pp[0]!=' ' && aux1[0]!='\0' && aux2[0]!='\0' && aux3[0]!='\0'){
        if(!parse_lzh_qso(pp,aux5,sizeof(aux5),aux6,sizeof(aux6),aux7,sizeof(aux7),aux8,sizeof(aux8))){pp=strtok(NULL,"\n"); continue;}
        for(qq=aux6;*qq!='\0';qq++)*qq=(char)toupper((unsigned char)*qq);
        if(radio_cty_lookup(con,aux6,&cty)!=1)cty.dxcc[0]='\0';
        strcat(aux5,":00");
        epoch=qsoz_date_clock_epoch(aux1,aux5);
        if(epoch==(time_t)-1){ppp++; pp=strtok(NULL,"\n"); continue;}
        if(!qsoz_db_log_values(con,aux9,sizeof(aux9),mycall,aux6,aux3,atol(aux2)*1000L,atol(aux2)*1000L,
                               aux7,aux8,"","","",atoi(cty.dxcc),(long long)epoch,(long long)epoch)){
          ppp++; pp=strtok(NULL,"\n"); continue;
        }
        snprintf(buf,sizeof(buf),"insert ignore into log (mycall,callsign,mode,freqtx,freqrx,signaltx,signalrx,contesttx,contestrx,contest,dxcc,open,close) value %s",aux9);
        mysql_query(con,buf);
        l1=mysql_affected_rows(con);
        if(l1>0){nnn+=l1; printf("%s\n",aux9);}
        ppp++;
      }
      pp=strtok(NULL,"\n");
    }
    printf("QSO Processed: %ld\nNew QSO Inserted: %ld\n",ppp,nnn);
    printf("</pre>");
    goto end;
  }
  
  if(act==22){ // cbr in button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    printf("<pre>");
    pp=strtok_r(ff,"\n",&save1);
    f1=1;
    for(ppp=nnn=0;;){
      if(pp==NULL)break;
      if(f1 && strncmp(pp,"CONTEST:",8)==0){if(qsoz_copy(aux0,sizeof(aux0),pp+9))f1=0; else aux0[0]='\0';}
      if(strncmp(pp,"QSO:",4)==0){
        if(!parse_cbr_qso(pp,aux1,sizeof(aux1),aux2,sizeof(aux2),aux3,sizeof(aux3),aux4,sizeof(aux4),
                          aux5,sizeof(aux5),aux6,sizeof(aux6),aux7,sizeof(aux7),aux8,sizeof(aux8),aux9,sizeof(aux9))){
          pp=strtok_r(NULL,"\n",&save1); continue;
        }
        if(strlen(aux4)+3>=sizeof(aux4)){pp=strtok_r(NULL,"\n",&save1); continue;}
        strcat(aux4,":00");
        if(radio_cty_lookup(con,aux7,&cty)!=1)cty.dxcc[0]='\0';
        epoch=qsoz_date_clock_epoch(aux3,aux4);
        if(epoch==(time_t)-1){ppp++; pp=strtok_r(NULL,"\n",&save1); continue;}
        l1=atol(aux1)*1000L;
        if(!qsoz_db_escape(con,esc_call,sizeof(esc_call),aux7)) {ppp++; pp=strtok_r(NULL,"\n",&save1); continue;}
        snprintf(buf,sizeof(buf),"select count(*),open from log where mycall='%s' and callsign='%s' and open>=%lld and open<=%lld and freqtx>=%ld and freqtx<=%ld limit 1",esc_mycall,esc_call,(long long)(epoch-180),(long long)(epoch+180),l1-1700000,l1+1700000);
        mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res); gg=atoi(row[0]); if(gg>0)epoch=atoll(row[1]);
        mysql_free_result(res);
        if(gg==0){
          if(!qsoz_db_log_values(con,aux3,sizeof(aux3),mycall,aux7,aux2,l1,l1,aux5,aux8,aux6,aux9,aux0,atoi(cty.dxcc),(long long)epoch,(long long)epoch)){
            ppp++; pp=strtok_r(NULL,"\n",&save1); continue;
          }
          snprintf(buf,sizeof(buf),"insert into log (mycall,callsign,mode,freqtx,freqrx,signaltx,signalrx,contesttx,contestrx,contest,dxcc,open,close) value %s",aux3);
          nnn++;
        }
        else {
          if(!qsoz_db_escape(con,esc_tx,sizeof(esc_tx),aux6) || !qsoz_db_escape(con,esc_rx,sizeof(esc_rx),aux9) || !qsoz_db_escape(con,esc_contest,sizeof(esc_contest),aux0)){
            ppp++; pp=strtok_r(NULL,"\n",&save1); continue;
          }
          snprintf(buf,sizeof(buf),"update log set contesttx='%s',contestrx='%s',contest='%s' where mycall='%s' and callsign='%s' and open=%lld",esc_tx,esc_rx,esc_contest,esc_mycall,esc_call,(long long)epoch);
        }
        mysql_query(con,buf);
        ppp++;
      }
      pp=strtok_r(NULL,"\n",&save1);
    }
    printf("QSO Processed: %ld\nNew QSO Inserted: %ld\n",ppp,nnn);
    printf("</pre>");
    goto end;
  }

  if(act==23){ // start button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    l1=(long)(atof(tok[5])*1000);
    if(strlen(tok[4])<3 || strlen(tok[6])<2 || strlen(tok[7])<2 || strlen(tok[8])<2 || l1==0)goto end;
    epoch=time(NULL); 
    printf("Start: %s\n",qsoz_epoch_text(epoch));
    printf("<table><td>");
    if(radio_cty_lookup(con,tok[4],&cty)==1){
      vv=atoi(cty.dxcc); f6=atof(cty.latitude); f7=atof(cty.longitude); f8=atof(cty.gmtshift);
      qsoz_html_text(html1,sizeof(html1),cty.base); qsoz_html_text(html2,sizeof(html2),cty.name);
      printf("<pre>base:%s\nname:%s\ndxcc:%s\ncont:%s\ncqzone:%s\nituzone:%s\nlatitude:%s\nlongitude:%s\ngmtshift:%s\n</pre>",html1,html2,cty.dxcc,cty.cont,cty.cqzone,cty.ituzone,cty.latitude,cty.longitude,cty.gmtshift);
    } else {vv=0; f6=f7=f8=0.0;}
    printf("</td><td>");
    if(radio_cty_lookup(con,mycall,&cty2)==1){
      dist=radio_distance_km(f6,f7,atof(cty2.latitude),atof(cty2.longitude));
      bear=radio_bearing_deg(f6,f7,atof(cty2.latitude),atof(cty2.longitude));
      printf("<pre>distance:%5.0f\nbearing:%5.0f\ndeltatime:%.0f\n</pre>",dist,bear,atof(cty2.gmtshift)-f8);
    }
    printf("</td><td>");    
    sprintf(buf,"select grid from who where callsign='%s'",esc_call);
    mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res); if(row!=NULL)strcpy(aux1,row[0]); else aux1[0]='\0';
    mysql_free_result(res);
    sprintf(buf,"select grid from who where callsign='%s'",esc_mycall);
    mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res); if(row!=NULL)strcpy(aux2,row[0]); else aux2[0]='\0';
    mysql_free_result(res);
    if(aux1[0]!='\0' && aux2[0]!='\0' && radio_locator_distance_bearing(aux1,aux2,&dist,&bear)){
      if(!qsoz_html_text(html1,sizeof(html1),aux1))html1[0]='\0';
      if(!qsoz_html_text(html2,sizeof(html2),aux2))html2[0]='\0';
      printf("<pre>gridyou:%s\ngridme:%s\ndistance:%5.0f\nbearing:%5.0f\n</pre>",html1,html2,dist,bear);
    }
    printf("</td></table>");
    sprintf(buf,"select count(*) from log where mycall='%s' and dxcc=%d",esc_mycall,vv);
    mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res); l1=atol(row[0]);
    mysql_free_result(res);
    printf("<pre>Records with same dxcc[%d]: %ld\n</pre>",vv,l1);
    qsoz_stats_reset();
    sprintf(buf,"select count(*) from who where callsign='%s'",esc_call);
    mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res); c=atoi(row[0]);
    mysql_free_result(res);
    if(c==0)radio_callbook_lookup(cfg.callbook_host,cfg.callbook_port,RADIO_CALLBOOK_QRZCOM,tok[4],cfg.callbook_timeout,callbook_response,sizeof(callbook_response));
    sprintf(buf,"select firstname,lastname,addr1,addr2,state,zip,country,grid,email,cqzone,ituzone,born,src,image,time from who where callsign='%s'",esc_call);
    mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res);
    if(row!=NULL){
      printf("<table><td><pre>");
      qsoz_html_text(html1,sizeof(html1),row[0]); qsoz_html_text(html2,sizeof(html2),row[1]); printf("%s %s\n",html1,html2);
      qsoz_html_text(html1,sizeof(html1),row[2]); qsoz_html_text(html2,sizeof(html2),row[3]); printf("%s\n%s\n",html1,html2);
      qsoz_html_text(html1,sizeof(html1),row[4]); qsoz_html_text(html2,sizeof(html2),row[5]); qsoz_html_text(html3,sizeof(html3),row[6]); printf("%s %s %s\n",html1,html2,html3);
      qsoz_html_text(html1,sizeof(html1),row[7]); qsoz_html_text(html2,sizeof(html2),row[8]); printf("%s\n%s\n",html1,html2);
      qsoz_html_text(html1,sizeof(html1),row[9]); qsoz_html_text(html2,sizeof(html2),row[10]); qsoz_html_text(html3,sizeof(html3),row[11]); qsoz_html_text(html4,sizeof(html4),row[12]); printf("%s %s %s %s\n",html1,html2,html3,html4);
    //  printf("%s\n",qsoz_epoch_text(atoll(row[14])));
      printf("</pre></td>");
      if(row[13][0]!='\0' && qsoz_html_attr(html1,sizeof(html1),row[13]))printf("<td><img src=\"%s\" width=\"200\" style=\"cursor:zoom-in\" onclick=\"openImgExact(this.src)\"></td>",html1);
      printf("</table>\n");
    }
    mysql_free_result(res);
    printf("<pre>");
    sprintf(buf,"select open,close,callsign,freqtx,freqrx,mode,signaltx,signalrx,lotw,eqsl,qrz,contesttx,contestrx,contest from log where callsign='%s' and mycall='%s' order by open desc",esc_call,esc_mycall);
    mysql_query(con,buf);
    res=mysql_store_result(con);
    vv=0;
    for(;;){
      row=mysql_fetch_row(res);
      if(row==NULL)break;
      c=(int)(atol(row[3])/1000000.0);
      if(c>433)continue;
      sprintf(aux3,"%04d%s",qsoz_band(c),qsoz_mode(row[5]));
      incdata3(0,0,aux3,1,1);
      aux1[0]='\0';
      if(atoi(row[8])==1){strcat(aux1,"L"); incdata3(0,1,aux3,1,1);}
      if(atoi(row[9])==1){strcat(aux1,"E"); incdata3(0,2,aux3,1,1);}
      if(atoi(row[10])==1){strcat(aux1,"Q"); incdata3(0,3,aux3,1,1);}
      if(++vv<=5){
        td=atoll(row[1])-atoll(row[0]);
        if(td==0)strcpy(aux2,"(0s)");
        else if(td<60)sprintf(aux2,"(%lds)",td);
        else if(td<3600)sprintf(aux2,"(%ldm)",td/60);
        else sprintf(aux2,"(%ldh)",td/3600);
        if(!qsoz_html_js_sq_attr(js1,sizeof(js1),row[2]))js1[0]='\0';
        if(!qsoz_html_text(html1,sizeof(html1),row[2]))html1[0]='\0';
        if(!qsoz_html_text(html2,sizeof(html2),row[5]))html2[0]='\0';
        if(!qsoz_html_text(html3,sizeof(html3),row[6]))html3[0]='\0';
        if(!qsoz_html_text(html4,sizeof(html4),row[7]))html4[0]='\0';
        printf("<button type=\"button\" class=\"myb2\" onclick=\"cmd1(%lld,'%s')\"> </button> ",atoll(row[0]),js1);
        printf("%s%5s %12s %7.1f %4s %5s %5s %-3s ",qsoz_epoch_text(atoll(row[0])),aux2,html1,atol(row[3])/1000.0,html2,html3,html4,aux1);
        if(row[13][0]!='\0'){
          if(!qsoz_html_text(html1,sizeof(html1),row[13]))html1[0]='\0';
          if(!qsoz_html_text(html2,sizeof(html2),row[11]))html2[0]='\0';
          if(!qsoz_html_text(html3,sizeof(html3),row[12]))html3[0]='\0';
          printf(" (%s,%s,%s)",html1,html2,html3);
        }
        if(atol(row[4])>0&&atol(row[4])!=atol(row[3]))printf(" [%+.1f]",(atol(row[4])-atol(row[3]))/1000.0);
        printf("\n");
      }
    }
    mysql_free_result(res);
    qsoz_stats_sort_bucket(0,0);

    printf("<p class=\"myh1\">%6s %8s %8s %8s %8s</p>","B/Mode","QSO","QSL.LOTW","QSL.EQSL","QSL.QRZ");
    suml[1]=suml[2]=suml[3]=suml[4]=0;
    for(idx=0;idx<ndata3[0][0];idx++){
      l1=data3[0][0][idx].num; suml[1]+=l1;
      l2=numdata3(0,1,data3[0][0][idx].lab); suml[2]+=l2;
      l3=numdata3(0,2,data3[0][0][idx].lab); suml[3]+=l3;
      l4=numdata3(0,3,data3[0][0][idx].lab); suml[4]+=l4;
      printf("%6s %8ld %8ld %8ld %8ld\n",data3[0][0][idx].lab,l1,l2,l3,l4);
    }
    printf("<p class=\"myh2\">%6s %8ld %8ld %8ld %8ld</p>","ALL",suml[1],suml[2],suml[3],suml[4]);
    printf("</pre>");
    goto end;
  }

  if(act==26){ // end button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    l1=(long)(atof(tok[5])*1000);
    if(strlen(tok[4])<3 || strlen(tok[6])<2 || strlen(tok[7])<2 || strlen(tok[8])<2 || l1==0)goto end;
    if(tok[12][0]=='\0')goto end;
    if(tok[9][0]=='-')tok[9][0]='\0';
    if(tok[10][0]=='-')tok[10][0]='\0';
    if(tok[11][0]=='-')tok[11][0]='\0';
    if(radio_cty_lookup(con,tok[4],&cty)!=1)cty.dxcc[0]='\0';
    open_epoch=qsoz_datetime_epoch(tok[12]);
    if(open_epoch==(time_t)-1)goto end;
    if(!qsoz_db_log_values(con,aux3,sizeof(aux3),mycall,tok[4],tok[6],l1,l1,tok[7],tok[8],tok[10],tok[11],tok[9],atoi(cty.dxcc),(long long)open_epoch,(long long)time(NULL)))goto end;
    snprintf(buf,sizeof(buf),"insert into log (mycall,callsign,mode,freqtx,freqrx,signaltx,signalrx,contesttx,contestrx,contest,dxcc,open,close) value %s",aux3);
    if(mysql_query(con,buf)==0)printf("%s inserted\n",tok[4]);
    goto end;
  }

  if(act==24){ // QRZ.com button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    if(tok[4][0]=='-' || tok[4][0]=='\0'){printf("<pre>Enter a callsign</pre>"); goto end;}
    callbook_response[0]='\0';
    radio_callbook_lookup(cfg.callbook_host,cfg.callbook_port,RADIO_CALLBOOK_QRZCOM,tok[4],cfg.callbook_timeout,callbook_response,sizeof(callbook_response));
    if(callbook_response[0]=='\0')strcpy(callbook_response,"ERROR callbook service unavailable\n");
    print_callbook_result(con,esc_call,callbook_response);
    goto end;
  }

  if(act==25){ // QRZ.ru button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    if(tok[4][0]=='-' || tok[4][0]=='\0'){printf("<pre>Enter a callsign</pre>"); goto end;}
    callbook_response[0]='\0';
    radio_callbook_lookup(cfg.callbook_host,cfg.callbook_port,RADIO_CALLBOOK_QRZRU,tok[4],cfg.callbook_timeout,callbook_response,sizeof(callbook_response));
    if(callbook_response[0]=='\0')strcpy(callbook_response,"ERROR callbook service unavailable\n");
    print_callbook_result(con,esc_call,callbook_response);
    goto end;
  }

  if(act==27){ // contest list button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    printf("<pre>");
    sprintf(buf,"select contest,min(open),max(open),count(callsign) from log where mycall='%s' and contest<>'' group by contest order by max(open) desc",esc_mycall);
    mysql_query(con,buf);
    res=mysql_store_result(con);
    for(;;){
      row=mysql_fetch_row(res);
      if(row==NULL)break;
      aux1[0]='\0';
      if(conscore_supported(row[0]))strcpy(aux1,"Scorable");
      if(!qsoz_html_js_sq_attr(js1,sizeof(js1),row[0]))js1[0]='\0';
      if(!qsoz_html_text(html1,sizeof(html1),row[0]))html1[0]='\0';
      printf("<button type=\"button\" class=\"myb2\" onclick=\"cmd2('%s')\">%20s</button>: [%4d] ",js1,html1,atoi(row[3]));
      printf("%s -> ",qsoz_epoch_text(atoll(row[1])));
      printf("%s %s\n",qsoz_epoch_text(atoll(row[2])),aux1);
    }
    mysql_free_result(res);
    printf("</pre>");
    goto end;
  }

  if(act==31){ // contest score button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    conscore_setup(con,tok,mycall);
    sprintf(buf,"select min(open),max(open) from log where mycall='%s' and contest='%s'",esc_mycall,esc_contest);
    mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res);
    if(row==NULL || row[0]==NULL || row[1]==NULL){
      if(res!=NULL)mysql_free_result(res);
      printf("<pre>No QSO for selected contest</pre>");
      goto end;
    }
    ll1=atoll(row[0]); ll2=atoll(row[1]);
    mysql_free_result(res);
    conscore(con,tok,mycall,ll1,ll2);
    qsoz_stats_sort_bucket(0,0);
    qsoz_stats_sort_bucket(0,1);
    qsoz_stats_sort_bucket(0,2);
    qsoz_stats_sort_bucket(0,3);
    qsoz_stats_sort_bucket(0,4);
    printf("<pre>");
    if(!qsoz_html_text(html1,sizeof(html1),tok[9]))html1[0]='\0';
    printf("<p class=\"myh1\">%s</p>\n",html1);
    gg=strlen(data3[0][4][0].lab);
    for(c=0;c<ndata3[0][4];c++){
      for(l1=0,idx=0;idx<ndata3[0][0];idx++)if(strncmp(data3[0][0][idx].lab,data3[0][4][c].lab,gg)==0)l1+=data3[0][0][idx].num;
      for(l2=0,idx=0;idx<ndata3[0][1];idx++)if(strncmp(data3[0][1][idx].lab,data3[0][4][c].lab,gg)==0)l2+=data3[0][1][idx].num;
      for(l3=0,idx=0;idx<ndata3[0][2];idx++)if(strncmp(data3[0][2][idx].lab,data3[0][4][c].lab,gg)==0)l3+=data3[0][2][idx].num;
      if(!qsoz_html_text(html1,sizeof(html1),data3[0][4][c].lab))html1[0]='\0';
      printf("%*s %5ld %8ld %4ld\n",gg,html1,l1,l2,l3);
    }
    for(l1=0,idx=0;idx<ndata3[0][0];idx++)l1+=data3[0][0][idx].num;
    for(l2=0,idx=0;idx<ndata3[0][1];idx++)l2+=data3[0][1][idx].num;
    for(l3=0,idx=0;idx<ndata3[0][3];idx++)l3+=data3[0][3][idx].num;
    if(strncmp(tok[9],"RAC",3)==0 && l3==0)l3=1;
    printf("<p class=\"myh1\">%*s %5ld %8ld %4ld</p>\n",gg,"ALL",l1,l2,l3);
    printf("<p class=\"myh2\">Score %9ld</p>\n",l2*l3);
    for(idx=0;idx<ndata3[0][3];idx++){
      if(!qsoz_html_text(html1,sizeof(html1),data3[0][3][idx].lab))html1[0]='\0';
      printf("%s ",html1);
      if(idx>0 && idx%9==0)printf("\n");
    }
    printf("\n");
    printf("</pre>");
    goto end;
  }

  if(act==14){ // contest graph button
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    conscore_setup(con,tok,mycall);
    sprintf(buf,"select min(open),max(open) from log where mycall='%s' and contest='%s'",esc_mycall,esc_contest);
    mysql_query(con,buf); res=mysql_store_result(con); row=mysql_fetch_row(res);
    if(row==NULL || row[0]==NULL || row[1]==NULL){
      if(res!=NULL)mysql_free_result(res);
      printf("<pre>No QSO for selected contest</pre>");
      goto end;
    }
    ll1=atoll(row[0]); ll2=atoll(row[1]);
    mysql_free_result(res);
    printf("<div class=\"gchart\" data-rows='[ ");
    for(ll3=ll1;ll3<=ll2;ll3+=900){
      conscore(con,tok,mycall,ll3,ll3+899);
      for(l1=0,idx=0;idx<ndata3[0][0];idx++)l1+=data3[0][0][idx].num;
      for(l2=0,idx=0;idx<ndata3[0][1];idx++)l2+=data3[0][1][idx].num;
      for(l3=0,idx=0;idx<ndata3[0][3];idx++)l3+=data3[0][3][idx].num;
      if(strncmp(tok[9],"RAC",3)==0 && l3==0)l3=1;
      printf("%c[ %lld,%ld,%ld,%ld,%ld ]\n",(ll3-ll1>0)?',':' ',ll3,l1,l2,l3,l2*l3);
    }
    printf("]' style=\"width:100%%;height:520px\"></div>");
    goto end;
  }

  if(act==13){ // cluster button
    QsozClusterSpot *spot;
    int count,i,cap;

    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/html; charset=utf-8\r\n\r\n<!--%d-->",act);
    printf("<pre>");
    printf("<p class=\"myh1\">%22s <b>%16s</b> %10s %7s %7s %4s %4s %3s %s</p>","DATETIME","CALLSIGN","FREQ","QSODXCC","QSLDXCC","QSO","QSL","LAST","SPOTTER");
    cap=mypage;
    if(cap<1)cap=1;
    if(cap>1000)cap=1000;
    spot=(QsozClusterSpot *)calloc((size_t)cap,sizeof(QsozClusterSpot));
    if(spot==NULL){printf("Cluster memory error\n</pre>"); goto end;}
    s=qsoz_tcp_connect(cfg.cluster_host,cfg.cluster_port,cfg.cluster_timeout);
    if(s<0){fprintf(stderr,"pproc: cluster connect failed\n"); free(spot); printf("Cluster unavailable\n</pre>"); goto end;}
    snprintf(aux1,sizeof(aux1),"%d,%s\n",cap,tok[12]);
    if(!qsoz_send_all(s,aux1,(unsigned long)strlen(aux1))){fprintf(stderr,"pproc: cluster send failed\n"); close(s); free(spot); printf("Cluster unavailable\n</pre>"); goto end;}
    qsoz_line_reader_init(&cluster_reader,s);
    count=0;
    line_rc=0;
    for(;count<cap;){
      line_rc=qsoz_read_line(&cluster_reader,cluster_line,sizeof(cluster_line));
      if(line_rc<=0)break;
      if(!parse_cluster_line(cluster_line,&p1,&p2,&p3,&p4))continue;
      spot[count].epoch=atoll(p1);
      spot[count].freq=atol(p3);
      if(!qsoz_copy(spot[count].spotter,sizeof(spot[count].spotter),p2) || !qsoz_copy(spot[count].dx,sizeof(spot[count].dx),p4))continue;
      if(radio_cty_lookup(con,spot[count].dx,&cty)==1)spot[count].dxcc=atoi(cty.dxcc);
      count++;
    }
    if(line_rc<0)fprintf(stderr,"pproc: cluster stream error\n");
    close(s);

    cluster_dxcc_stats(con,esc_mycall,spot,count);
    cluster_call_stats(con,esc_mycall,spot,count);

    for(i=0;i<count;i++){
      fx=spot[i].freq/1000.0;
      if(!qsoz_html_js_sq_attr(js1,sizeof(js1),spot[i].dx))js1[0]='\0';
      if(!qsoz_html_text(html1,sizeof(html1),spot[i].dx))html1[0]='\0';
      if(!qsoz_html_text(html2,sizeof(html2),spot[i].spotter))html2[0]='\0';
      if(spot[i].last==0)strcpy(aux1,"   "); else strcpy(aux1,qsoz_elapsed(time(NULL)-spot[i].last));
      printf("<button type=\"button\" class=\"myb2\" onclick=\"cmd3('%s','%.1f')\"> </button> %s <b>%16s</b> %10.1f ",js1,fx,qsoz_epoch_text(spot[i].epoch),html1,fx);
      if(spot[i].dxcc_cache_fresh)printf("<span style=\"color: red;\">%7ld %7ld</span> ",spot[i].dxcc_qso,spot[i].dxcc_qsl);
      else printf("%7ld %7ld ",spot[i].dxcc_qso,spot[i].dxcc_qsl);
      printf("%4ld %4ld %3s %s\n",spot[i].call_qso,spot[i].call_qsl,aux1,html2);
    }
    free(spot);
    printf("</pre>");
    goto end;
  }

  
  end:
  free(ff);
  qsoz_stats_free();
  mysql_close(con);
  return 0;

  end_no_db:
  free(ff);
  qsoz_stats_free();
  return 0;
}
