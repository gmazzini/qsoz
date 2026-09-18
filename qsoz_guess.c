// Gianluca Mazzini @2022- Version 4.5
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "qsoz_completion.h"
#include "qsoz_html.h"

#define MAXINPUT 20
#define MAXCALL 6
#define MAXCAND 400
#define MAXOUT 50
#define MAXGRAM 19
#define QUERY_SIZE 2048

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

static void upper_ascii(char *s) {
  unsigned char c;

  for(;*s!='\0';s++) {
    c=(unsigned char)*s;
    if(c>='a' && c<='z')*s=(char)(c-'a'+'A');
  }
}

static int gram_seen(char gram[][4],int count,const char *value) {
  int i;

  for(i=0;i<count;i++)if(strcmp(gram[i],value)==0)return 1;
  return 0;
}

static int make_grams(const char *s,int n,char gram[][4]) {
  int len,i,count;
  char value[4];

  len=(int)strlen(s);
  count=0;
  if(len<n)return 0;
  for(i=0;i<=len-n && count<MAXGRAM;i++) {
    memcpy(value,s+i,(size_t)n);
    value[n]='\0';
    if(gram_seen(gram,count,value))continue;
    memcpy(gram[count],value,(size_t)n+1U);
    count++;
  }
  return count;
}

static int append_placeholders(char *query,unsigned long cap,unsigned long *len,int count) {
  int i,n;

  for(i=0;i<count;i++) {
    n=snprintf(query+*len,(size_t)(cap-*len),"%s?",i==0?"":",");
    if(n<0 || (unsigned long)n>=cap-*len)return 0;
    *len+=(unsigned long)n;
  }
  return 1;
}

static int prepare_query(sqlite3 *db,const char *in,int len,sqlite3_stmt **stmt) {
  char bigram[MAXGRAM][4],trigram[MAXGRAM][4];
  char query[QUERY_SIZE];
  unsigned long used;
  int bigrams,trigrams,i,param,n,rc;

  *stmt=NULL;
  if(len<2)return 1;
  bigrams=make_grams(in,2,bigram);
  trigrams=make_grams(in,3,trigram);
  used=0;
  if(len==2) {
    n=snprintf(query,sizeof(query),"select callsign,count(*) common from bigram where gram in (");
    if(n<0 || (unsigned long)n>=sizeof(query))return 0;
    used=(unsigned long)n;
    if(!append_placeholders(query,sizeof(query),&used,bigrams))return 0;
    n=snprintf(query+used,sizeof(query)-used,") group by callsign order by common desc,callsign limit %d",MAXCAND);
    if(n<0 || (unsigned long)n>=sizeof(query)-used)return 0;
  } else {
    n=snprintf(query,sizeof(query),"select callsign,sum(common) common from (select callsign,count(*) common from trigram where gram in (");
    if(n<0 || (unsigned long)n>=sizeof(query))return 0;
    used=(unsigned long)n;
    if(!append_placeholders(query,sizeof(query),&used,trigrams))return 0;
    n=snprintf(query+used,sizeof(query)-used,") group by callsign union all select callsign,count(*) common from bigram where gram in (");
    if(n<0 || (unsigned long)n>=sizeof(query)-used)return 0;
    used+=(unsigned long)n;
    if(!append_placeholders(query,sizeof(query),&used,bigrams))return 0;
    n=snprintf(query+used,sizeof(query)-used,") group by callsign) group by callsign order by common desc,callsign limit %d",MAXCAND);
    if(n<0 || (unsigned long)n>=sizeof(query)-used)return 0;
  }
  rc=sqlite3_prepare_v2(db,query,-1,stmt,NULL);
  if(rc!=SQLITE_OK)return 0;
  param=1;
  if(len>=3)for(i=0;i<trigrams;i++)sqlite3_bind_text(*stmt,param++,trigram[i],-1,SQLITE_TRANSIENT);
  for(i=0;i<bigrams;i++)sqlite3_bind_text(*stmt,param++,bigram[i],-1,SQLITE_TRANSIENT);
  return 1;
}

int qsoz_guess_main(void) {
  sqlite3 *db;
  sqlite3_stmt *stmt;
  Cand v[MAXCAND];
  char in[MAXINPUT+1],html[128],js[128];
  const unsigned char *call;
  int len,i,len_call,mx,top,rc;

  printf("Content-Type: text/html; charset=utf-8\r\n\r\n");
  len=read_input(in,sizeof(in));
  if(len<0) {
    fprintf(stderr,"qsoz_guess: input too long\n");
    printf("<pre>\n</pre>");
    return 0;
  }
  if(len==0) {
    printf("<pre>\n</pre>");
    return 0;
  }
  upper_ascii(in);
  db=NULL;
  rc=sqlite3_open_v2(QSOZ_COMPLETION_DB,&db,SQLITE_OPEN_READONLY,NULL);
  if(rc!=SQLITE_OK) {
    fprintf(stderr,"qsoz_guess: cannot open %s: %s\n",QSOZ_COMPLETION_DB,db==NULL?"unknown error":sqlite3_errmsg(db));
    if(db!=NULL)sqlite3_close(db);
    printf("<pre>\n</pre>");
    return 1;
  }
  stmt=NULL;
  if(!prepare_query(db,in,len,&stmt)) {
    fprintf(stderr,"qsoz_guess: query prepare failed: %s\n",sqlite3_errmsg(db));
    sqlite3_close(db);
    printf("<pre>\n</pre>");
    return 1;
  }
  if(stmt==NULL) {
    sqlite3_close(db);
    printf("<pre>\n</pre>");
    return 0;
  }
  i=0;
  for(;i<MAXCAND;) {
    rc=sqlite3_step(stmt);
    if(rc==SQLITE_DONE)break;
    if(rc!=SQLITE_ROW) {
      fprintf(stderr,"qsoz_guess: query failed: %s\n",sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      printf("<pre>\n</pre>");
      return 1;
    }
    call=sqlite3_column_text(stmt,0);
    if(call==NULL)continue;
    strncpy(v[i].callsign,(const char *)call,MAXCALL);
    v[i].callsign[MAXCALL]='\0';
    v[i].common=sqlite3_column_int(stmt,1);
    v[i].lev=levenshtein(v[i].callsign,in);
    len_call=(int)strlen(v[i].callsign);
    mx=(len_call>len)?len_call:len;
    v[i].nd=(mx>0)?((double)v[i].lev/(double)mx):0.0;
    i++;
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);

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
