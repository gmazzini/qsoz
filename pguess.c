// Gianluca Mazzini @2022- Version 3.01
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include "qsoz_config.h"
#include "qsoz_html.h"

#define MAXINPUT 20
#define MAXCALL 6
#define MAXCAND 400
#define MAXOUT 50
#define QUERY_SIZE 4096


typedef struct {
  char callsign[MAXCALL+1];
  int common;
  int lev;
  double nd;
} Cand;

static int min3(int a,int b,int c) {
  int m;

  m=a;
  if(b<m)m=b;
  if(c<m)m=c;
  return m;
}

static int levenshtein(const char *s,const char *t) {
  int a[MAXINPUT+1],b[MAXINPUT+1],*prev,*curr,*tmp;
  int n,m,i,j,cost;

  n=(int)strlen(s);
  m=(int)strlen(t);
  if(n==0)return m;
  if(m==0)return n;
  prev=a;
  curr=b;
  for(j=0;j<=m;j++)prev[j]=j;
  for(i=1;i<=n;i++) {
    curr[0]=i;
    for(j=1;j<=m;j++) {
      cost=(s[i-1]==t[j-1])?0:1;
      curr[j]=min3(prev[j]+1,curr[j-1]+1,prev[j-1]+cost);
    }
    tmp=prev;
    prev=curr;
    curr=tmp;
  }
  return prev[m];
}

static int cmp_cand(const void *va,const void *vb) {
  const Cand *a,*b;

  a=(const Cand *)va;
  b=(const Cand *)vb;
  if(a->nd<b->nd)return -1;
  if(a->nd>b->nd)return 1;
  if(a->lev<b->lev)return -1;
  if(a->lev>b->lev)return 1;
  if(a->common>b->common)return -1;
  if(a->common<b->common)return 1;
  return strcmp(a->callsign,b->callsign);
}

static int read_input(char *buf,int cap) {
  int c,n,overflow;

  n=0;
  overflow=0;
  for(;;) {
    c=getchar();
    if(c==EOF)break;
    if(n<cap-1)buf[n++]=(char)c;
    else overflow=1;
  }
  buf[n]='\0';
  return overflow?-1:n;
}

int main(void) {
  QsozConfig cfg;
  MYSQL *con;
  MYSQL_RES *res;
  MYSQL_ROW row;
  Cand v[MAXCAND];
  char in[MAXINPUT+1],escaped[MAXINPUT*2+1],query[QUERY_SIZE],err[256],html[128],js[128];
  int len,i,len_call,mx,top;

  printf("Content-Type: text/html; charset=utf-8\r\n\r\n");
  len=read_input(in,sizeof(in));
  if(len<0) {
    fprintf(stderr,"pguess: input too long\n");
    printf("<pre>\n</pre>");
    return 0;
  }
  if(len==0) {
    printf("<pre>\n</pre>");
    return 0;
  }
  if(!qsoz_config_load(&cfg,QSOZ_CONFIG_FILE,err,sizeof(err))) {
    fprintf(stderr,"pguess: %s\n",err);
    printf("<pre>\n</pre>");
    return 1;
  }

  con=mysql_init(NULL);
  if(con==NULL) {
    fprintf(stderr,"pguess: mysql_init failed\n");
    printf("<pre>\n</pre>");
    return 1;
  }
  if(mysql_real_connect(con,cfg.db_host,cfg.db_user,cfg.db_pass,cfg.db_name,cfg.db_port,NULL,0)==NULL) {
    fprintf(stderr,"pguess: mysql connect: %s\n",mysql_error(con));
    mysql_close(con);
    printf("<pre>\n</pre>");
    return 1;
  }
  mysql_real_escape_string(con,escaped,in,(unsigned long)len);
  sprintf(query,"SELECT callsign, SUM(common) AS common FROM (SELECT a.callsign, COUNT(*) AS common FROM aux3 a WHERE %d >= 3 AND a.gram IN (SELECT DISTINCT SUBSTRING('%s', n.n, 3) FROM (SELECT 1 n UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4) n WHERE n.n <= %d - 2) GROUP BY a.callsign UNION ALL SELECT a.callsign, COUNT(*) AS common FROM aux2 a WHERE %d >= 3 AND a.gram IN (SELECT DISTINCT SUBSTRING('%s', n.n, 2) FROM (SELECT 1 n UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5) n WHERE n.n <= %d - 1) GROUP BY a.callsign UNION ALL SELECT a.callsign, COUNT(*) AS common FROM aux2 a WHERE %d = 2 AND a.gram = '%s' GROUP BY a.callsign) x GROUP BY callsign ORDER BY common DESC, callsign LIMIT %d",len,escaped,len,len,escaped,len,len,escaped,MAXCAND);
  if(mysql_query(con,query)!=0) {
    fprintf(stderr,"pguess: mysql query: %s\n",mysql_error(con));
    mysql_close(con);
    printf("<pre>\n</pre>");
    return 1;
  }
  res=mysql_store_result(con);
  if(res==NULL) {
    fprintf(stderr,"pguess: mysql result: %s\n",mysql_error(con));
    mysql_close(con);
    printf("<pre>\n</pre>");
    return 1;
  }

  for(i=0;i<MAXCAND;i++) {
    row=mysql_fetch_row(res);
    if(row==NULL)break;
    strncpy(v[i].callsign,row[0],MAXCALL);
    v[i].callsign[MAXCALL]='\0';
    v[i].common=row[1]?atoi(row[1]):0;
    v[i].lev=levenshtein(v[i].callsign,in);
    len_call=(int)strlen(v[i].callsign);
    mx=(len_call>len)?len_call:len;
    v[i].nd=(mx>0)?((double)v[i].lev/(double)mx):0.0;
  }
  mysql_free_result(res);
  mysql_close(con);

  qsort(v,(size_t)i,sizeof(Cand),cmp_cand);
  top=(i<MAXOUT)?i:MAXOUT;
  printf("<pre>");
  for(i=0;i<top;i++) {
    if(!qsoz_html_text(html,sizeof(html),v[i].callsign))html[0]='\0';
    if(!qsoz_html_js_sq_attr(js,sizeof(js),v[i].callsign))js[0]='\0';
    printf("<button type=\"button\" class=\"guess\" onclick=\"cmd4('%s')\">%6s</button>   ",js,html);
    if(i%5==4)printf("\n");
  }
  printf("\n</pre>");
  return 0;
}
