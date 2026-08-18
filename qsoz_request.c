// Gianluca Mazzini @2022- Version 3.0
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qsoz_request.h"

#define PAYLOAD_INITIAL 4096UL

static void set_error(char *err,unsigned long cap,const char *text) {
  unsigned long n;

  if(err==NULL || cap==0)return;
  n=(unsigned long)strlen(text);
  if(n>=cap)n=cap-1;
  memcpy(err,text,(size_t)n);
  err[n]='\0';
}

static int b64_value(int c) {
  if(c>='A' && c<='Z')return c-'A';
  if(c>='a' && c<='z')return c-'a'+26;
  if(c>='0' && c<='9')return c-'0'+52;
  if(c=='+' || c=='-')return 62;
  if(c=='/' || c=='_')return 63;
  if(c=='=')return -2;
  return -1;
}

static int reserve_payload(char **buf,unsigned long *cap,unsigned long need) {
  char *p;
  unsigned long ncap;

  if(need>QSOZ_REQUEST_MAX_PAYLOAD+1UL)return 0;
  if(*cap>=need)return 1;
  ncap=*cap;
  if(ncap==0)ncap=PAYLOAD_INITIAL;
  for(;ncap<need;){
    if(ncap>QSOZ_REQUEST_MAX_PAYLOAD/2UL){ncap=QSOZ_REQUEST_MAX_PAYLOAD+1UL; break;}
    ncap*=2UL;
  }
  p=(char *)realloc(*buf,(size_t)ncap);
  if(p==NULL)return 0;
  *buf=p;
  *cap=ncap;
  return 1;
}

static int append_byte(char **buf,unsigned long *len,unsigned long *cap,unsigned int value) {
  if(*len>=QSOZ_REQUEST_MAX_PAYLOAD)return 0;
  if(!reserve_payload(buf,cap,*len+2UL))return 0;
  (*buf)[*len]=(char)value;
  (*len)++;
  return 1;
}

int qsoz_request_read(char fields[][QSOZ_REQUEST_FIELD_SIZE],int field_count,char **payload,unsigned long *payload_len,char *err,unsigned long errcap) {
  char *buf;
  unsigned long len,cap,pos;
  int c,field,q[4],qn,v,padded;

  if(fields==NULL || payload==NULL || payload_len==NULL || field_count<1)return 0;
  *payload=NULL;
  *payload_len=0;
  for(field=0;field<field_count;field++)fields[field][0]='\0';

  for(field=0;field<field_count;field++){
    pos=0;
    for(;;){
      c=getchar();
      if(c==EOF){set_error(err,errcap,"incomplete request fields"); return 0;}
      if(c==',')break;
      if(pos+1UL>=QSOZ_REQUEST_FIELD_SIZE){set_error(err,errcap,"request field too long"); return 0;}
      fields[field][pos++]=(char)c;
    }
    fields[field][pos]='\0';
  }

  buf=NULL;
  len=0;
  cap=0;
  qn=0;
  padded=0;
  for(;;){
    c=getchar();
    if(c==EOF)break;
    if(isspace((unsigned char)c))continue;
    if(padded){free(buf); set_error(err,errcap,"data after base64 padding"); return 0;}
    v=b64_value(c);
    if(v==-1){free(buf); set_error(err,errcap,"invalid base64 payload"); return 0;}
    q[qn++]=v;
    if(qn==4){
      if(q[0]<0 || q[1]<0 || (q[2]==-2 && q[3]!=-2)){free(buf); set_error(err,errcap,"invalid base64 padding"); return 0;}
      if(!append_byte(&buf,&len,&cap,(unsigned int)((q[0]<<2)|(q[1]>>4)))){free(buf); set_error(err,errcap,"payload too large"); return 0;}
      if(q[2]>=0){
        if(!append_byte(&buf,&len,&cap,(unsigned int)(((q[1]&15)<<4)|(q[2]>>2)))){free(buf); set_error(err,errcap,"payload too large"); return 0;}
        if(q[3]>=0){
          if(!append_byte(&buf,&len,&cap,(unsigned int)(((q[2]&3)<<6)|q[3]))){free(buf); set_error(err,errcap,"payload too large"); return 0;}
        } else padded=1;
      } else padded=1;
      qn=0;
    }
  }
  if(qn!=0){free(buf); set_error(err,errcap,"truncated base64 payload"); return 0;}
  if(buf==NULL){
    buf=(char *)malloc(1);
    if(buf==NULL){set_error(err,errcap,"out of memory"); return 0;}
  } else if(!reserve_payload(&buf,&cap,len+1UL)){free(buf); set_error(err,errcap,"out of memory"); return 0;}
  buf[len]='\0';
  *payload=buf;
  *payload_len=len;
  return 1;
}
