// Gianluca Mazzini @2022- Version 4.12
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qsoz_db.h"
#include "qsoz_time.h"
#include "qsoz_user.h"

#define INPUT_SIZE 512
#define QUERY_SIZE 1024
#define OTA_SIZE 32
#define CALL_SIZE 20
#define COMMAND_SIZE 128

static int read_request(char *buf,int cap) {
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
  return overflow?0:1;
}

static int split_request(char *buf,char **tok) {
  int i;
  char *p;

  tok[0]=buf;
  for(i=1;i<4;i++) {
    p=strchr(tok[i-1],',');
    if(p==NULL)return 0;
    *p='\0';
    tok[i]=p+1;
  }
  return strchr(tok[3],',')==NULL;
}

static int parse_long(const char *s,long *value) {
  char *end;
  long n;

  if(s==NULL || *s=='\0')return 0;
  errno=0;
  n=strtol(s,&end,10);
  if(errno!=0 || end==s || *end!='\0')return 0;
  *value=n;
  return 1;
}

static int valid_len(const char *s,unsigned long max) {
  unsigned long n;

  n=(unsigned long)strlen(s);
  return n>0 && n<=max;
}

static int escape_value(char *dst,unsigned long cap,const char *src) {
  unsigned long n;

  if(cap<3)return 0;
  n=qsoz_db_escape_raw(dst,src,(unsigned long)strlen(src));
  return n<cap;
}

int qsoz_cmd_main(void) {
  QsozUser user;
  QsozDb *con;
  char input[INPUT_SIZE],*tok[4],mycall[CALL_SIZE+1],command[COMMAND_SIZE+1];
  char esc_mycall[CALL_SIZE*2+1],esc_call[CALL_SIZE*2+1],esc_value[COMMAND_SIZE*2+1];
  char query[QUERY_SIZE],*eq,*key,*value,*column;
  long open,n;
  time_t epoch;
  unsigned long maxlen;
  int is_string,is_freq,is_delete;

  printf("Content-Type: text/plain\r\n\r\n");
  if(!read_request(input,sizeof(input)) || !split_request(input,tok)) {
    fprintf(stderr,"pcmd: invalid request\n");
    printf("ERROR\n");
    return 0;
  }
  if(!valid_len(tok[0],OTA_SIZE) || !parse_long(tok[1],&open) || open<0 || !valid_len(tok[2],CALL_SIZE) || !valid_len(tok[3],COMMAND_SIZE)) {
    fprintf(stderr,"pcmd: invalid request fields\n");
    printf("ERROR\n");
    return 0;
  }
  strcpy(command,tok[3]);
  if(!qsoz_user_session(tok[0],&user)) {
    fprintf(stderr,"pcmd: invalid or expired session\n");
    printf("ERROR\n");
    return 0;
  }
  strcpy(mycall,user.mycall);
  con=qsoz_db_open();
  if(con==NULL) {
    fprintf(stderr,"pcmd: cannot open log database\n");
    printf("ERROR\n");
    return 1;
  }
  if(!escape_value(esc_mycall,sizeof(esc_mycall),mycall) || !escape_value(esc_call,sizeof(esc_call),tok[2])) {
    qsoz_db_close(con);
    printf("ERROR\n");
    return 1;
  }

  eq=strchr(command,'=');
  key=command;
  value=NULL;
  if(eq!=NULL) {
    *eq='\0';
    value=eq+1;
  }
  column=NULL;
  maxlen=0;
  is_string=0;
  is_freq=0;
  is_delete=0;

  if(strcmp(key,"DEL")==0 || strcmp(key,"DELETE")==0)is_delete=1;
  else if(strcmp(key,"FT")==0 || strcmp(key,"FREQTX")==0) {column="freqtx"; is_freq=1;}
  else if(strcmp(key,"FR")==0 || strcmp(key,"FREQRX")==0) {column="freqrx"; is_freq=1;}
  else if(strcmp(key,"M")==0 || strcmp(key,"MODE")==0) {column="mode"; maxlen=8; is_string=1;}
  else if(strcmp(key,"ST")==0 || strcmp(key,"SIGNALTX")==0) {column="signaltx"; maxlen=8; is_string=1;}
  else if(strcmp(key,"SR")==0 || strcmp(key,"SIGNALRX")==0) {column="signalrx"; maxlen=8; is_string=1;}
  else if(strcmp(key,"C")==0 || strcmp(key,"CALL")==0) {column="callsign"; maxlen=20; is_string=1;}
  else if(strcmp(key,"DTS")==0 || strcmp(key,"DATETIMESTART")==0)column="open";
  else if(strcmp(key,"DTE")==0 || strcmp(key,"DATETIMEEND")==0)column="close";
  else if(strcmp(key,"CO")==0 || strcmp(key,"CONTEST")==0) {column="contest"; maxlen=20; is_string=1;}
  else if(strcmp(key,"COT")==0 || strcmp(key,"CONTESTTX")==0) {column="contesttx"; maxlen=10; is_string=1;}
  else if(strcmp(key,"COR")==0 || strcmp(key,"CONTESTRX")==0) {column="contestrx"; maxlen=10; is_string=1;}
  else {
    fprintf(stderr,"pcmd: unknown command\n");
    qsoz_db_close(con);
    printf("ERROR\n");
    return 0;
  }

  if(is_delete) {
    if(value!=NULL) {
      qsoz_db_close(con);
      printf("ERROR\n");
      return 0;
    }
    sprintf(query,"delete from log where mycall='%s' and callsign='%s' and open=%ld",esc_mycall,esc_call,open);
  } else {
    if(value==NULL || *value=='\0') {
      qsoz_db_close(con);
      printf("ERROR\n");
      return 0;
    }
    if(is_string) {
      if(!valid_len(value,maxlen) || !escape_value(esc_value,sizeof(esc_value),value)) {
        qsoz_db_close(con);
        printf("ERROR\n");
        return 0;
      }
      sprintf(query,"update log set %s='%s' where mycall='%s' and callsign='%s' and open=%ld",column,esc_value,esc_mycall,esc_call,open);
    } else if(is_freq) {
      if(!parse_long(value,&n) || n<0 || n>LONG_MAX/1000L) {
        qsoz_db_close(con);
        printf("ERROR\n");
        return 0;
      }
      sprintf(query,"update log set %s=%ld where mycall='%s' and callsign='%s' and open=%ld",column,n*1000L,esc_mycall,esc_call,open);
    } else {
      if(!qsoz_datetime_to_epoch(value,&epoch)) {
        qsoz_db_close(con);
        printf("ERROR\n");
        return 0;
      }
      sprintf(query,"update log set %s=%lld where mycall='%s' and callsign='%s' and open=%ld",column,(long long)epoch,esc_mycall,esc_call,open);
    }
  }

  if(qsoz_db_query(con,query)!=0) {
    fprintf(stderr,"pcmd: database query: %s\n",qsoz_db_error(con));
    qsoz_db_close(con);
    printf("ERROR\n");
    return 1;
  }
  qsoz_db_close(con);
  printf("OK\n");
  return 0;
}
