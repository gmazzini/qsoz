// Gianluca Mazzini @2022- Version 4.1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qsoz_app.h"
#include "qsoz_version.h"

#define OP_SIZE 32

static int query_op(char *out,unsigned long cap) {
  const char *query,*p,*end;
  unsigned long n;

  if(out==NULL || cap==0)return 0;
  out[0]='\0';
  query=getenv("QUERY_STRING");
  if(query==NULL || *query=='\0')return 1;
  p=query;
  for(;;){
    if(strncmp(p,"op=",3)==0){
      p+=3;
      end=strchr(p,'&');
      n=end!=NULL?(unsigned long)(end-p):(unsigned long)strlen(p);
      if(n==0 || n>=cap)return 0;
      memcpy(out,p,(size_t)n);
      out[n]='\0';
      return 1;
    }
    p=strchr(p,'&');
    if(p==NULL)break;
    p++;
  }
  return 1;
}

static int release_main(void) {
  printf("Content-Type: text/plain\r\n\r\n%s\n",QSOZ_RELEASE);
  return 0;
}

static int not_found(void) {
  printf("Status: 404 Not Found\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nNot found\n");
  return 0;
}

int main(void) {
  char op[OP_SIZE];

  if(!query_op(op,sizeof(op))){
    printf("Status: 400 Bad Request\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nBad request\n");
    return 0;
  }
  if(op[0]=='\0')return qsoz_frontend_main();
  if(strcmp(op,"cmd")==0)return qsoz_cmd_main();
  if(strcmp(op,"completion")==0)return qsoz_completion_main();
  if(strcmp(op,"cty")==0)return qsoz_cty_main();
  if(strcmp(op,"ft8")==0)return qsoz_ft8_main();
  if(strcmp(op,"guess")==0)return qsoz_guess_main();
  if(strcmp(op,"login")==0)return qsoz_login_main();
  if(strcmp(op,"proc")==0)return qsoz_proc_main();
  if(strcmp(op,"radio")==0)return qsoz_radio_main();
  if(strcmp(op,"release")==0)return release_main();
  if(strcmp(op,"time")==0)return qsoz_clock_main();
  return not_found();
}
