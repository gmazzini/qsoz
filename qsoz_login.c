// Gianluca Mazzini @2022- Version 4.6
#include <stdio.h>
#include <string.h>
#include <sodium.h>
#include "qsoz_user.h"

#define INPUT_SIZE 256
#define CALL_SIZE 21
#define PASS_SIZE 129
#define FILTER_SIZE 21
#define OTA_SIZE 17
#define ERR_SIZE 256

static int hex_value(int c) {
  if(c>='0' && c<='9')return c-'0';
  if(c>='a' && c<='f')return c-'a'+10;
  if(c>='A' && c<='F')return c-'A'+10;
  return -1;
}

static int url_decode(char *dst,unsigned long cap,const char *src,unsigned long len) {
  unsigned long i,j;
  int a,b;

  if(dst==NULL || cap==0 || src==NULL)return 0;
  j=0;
  for(i=0;i<len;i++){
    if(j+1>=cap)return 0;
    if(src[i]=='+')dst[j++]=' ';
    else if(src[i]=='%'){
      if(i+2>=len)return 0;
      a=hex_value((unsigned char)src[i+1]);
      b=hex_value((unsigned char)src[i+2]);
      if(a<0 || b<0)return 0;
      dst[j++]=(char)((a<<4)|b);
      i+=2;
    } else dst[j++]=src[i];
  }
  dst[j]='\0';
  return 1;
}

static int read_request(char *call,unsigned long callcap,char *passwd,unsigned long passcap) {
  char input[INPUT_SIZE],*p,*amp,*eq;
  size_t n;
  int got_call,got_pass;

  n=fread(input,1,sizeof(input)-1,stdin);
  if(ferror(stdin) || (!feof(stdin) && n==sizeof(input)-1))return 0;
  input[n]='\0';
  for(;n>0 && (input[n-1]=='\n' || input[n-1]=='\r');n--)input[n-1]='\0';
  got_call=0; got_pass=0; p=input;
  for(;;){
    amp=strchr(p,'&');
    if(amp!=NULL)*amp='\0';
    eq=strchr(p,'=');
    if(eq==NULL)return 0;
    *eq='\0'; eq++;
    if(strcmp(p,"call")==0){if(got_call || !url_decode(call,callcap,eq,(unsigned long)strlen(eq)))return 0; got_call=1;}
    else if(strcmp(p,"password")==0){if(got_pass || !url_decode(passwd,passcap,eq,(unsigned long)strlen(eq)))return 0; got_pass=1;}
    if(amp==NULL)break;
    p=amp+1;
  }
  return got_call && got_pass && call[0]!='\0' && passwd[0]!='\0';
}

int qsoz_login_main(void) {
  char call[CALL_SIZE],passwd[PASS_SIZE],ota[OTA_SIZE],filter[FILTER_SIZE],err[ERR_SIZE];
  long mypage;
  int ok;

  ota[0]='\0'; filter[0]='\0'; err[0]='\0'; mypage=0;
  printf("Content-Type: text/plain\r\n\r\n");
  if(!read_request(call,sizeof(call),passwd,sizeof(passwd))){printf(",0,\n"); return 0;}
  ok=qsoz_user_login(call,passwd,ota,sizeof(ota),&mypage,filter,sizeof(filter),err,sizeof(err));
  sodium_memzero(passwd,sizeof(passwd));
  if(!ok){printf(",0,\n"); return 0;}
  printf("%s,%ld,%s\n",ota,mypage,filter);
  return 0;
}
