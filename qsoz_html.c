// Gianluca Mazzini @2022- Version 3.0
#include <string.h>
#include "qsoz_html.h"

static int append_text(char *dst,unsigned long cap,unsigned long *len,const char *text) {
  unsigned long n;

  n=(unsigned long)strlen(text);
  if(*len+n>=cap)return 0;
  memcpy(dst+*len,text,n);
  *len+=n;
  dst[*len]='\0';
  return 1;
}

static int append_char(char *dst,unsigned long cap,unsigned long *len,char c) {
  if(*len+1UL>=cap)return 0;
  dst[*len]=c;
  (*len)++;
  dst[*len]='\0';
  return 1;
}

int qsoz_html_text(char *dst,unsigned long cap,const char *src) {
  unsigned long i,len;

  if(dst==NULL || cap==0 || src==NULL)return 0;
  len=0;
  dst[0]='\0';
  for(i=0;src[i]!='\0';i++){
    if(src[i]=='&'){
      if(!append_text(dst,cap,&len,"&amp;"))return 0;
    } else if(src[i]=='<'){
      if(!append_text(dst,cap,&len,"&lt;"))return 0;
    } else if(src[i]=='>'){
      if(!append_text(dst,cap,&len,"&gt;"))return 0;
    } else if(!append_char(dst,cap,&len,src[i]))return 0;
  }
  return 1;
}

int qsoz_html_attr(char *dst,unsigned long cap,const char *src) {
  unsigned long i,len;

  if(dst==NULL || cap==0 || src==NULL)return 0;
  len=0;
  dst[0]='\0';
  for(i=0;src[i]!='\0';i++){
    if(src[i]=='&'){
      if(!append_text(dst,cap,&len,"&amp;"))return 0;
    } else if(src[i]=='<'){
      if(!append_text(dst,cap,&len,"&lt;"))return 0;
    } else if(src[i]=='>'){
      if(!append_text(dst,cap,&len,"&gt;"))return 0;
    } else if(src[i]=='\"'){
      if(!append_text(dst,cap,&len,"&quot;"))return 0;
    } else if(src[i]=='\''){
      if(!append_text(dst,cap,&len,"&#39;"))return 0;
    } else if(!append_char(dst,cap,&len,src[i]))return 0;
  }
  return 1;
}

int qsoz_html_js_sq_attr(char *dst,unsigned long cap,const char *src) {
  unsigned long i,len;

  if(dst==NULL || cap==0 || src==NULL)return 0;
  len=0;
  dst[0]='\0';
  for(i=0;src[i]!='\0';i++){
    if(src[i]=='\\'){
      if(!append_text(dst,cap,&len,"\\\\"))return 0;
    } else if(src[i]=='\''){
      if(!append_text(dst,cap,&len,"\\&#39;"))return 0;
    } else if(src[i]=='\"'){
      if(!append_text(dst,cap,&len,"&quot;"))return 0;
    } else if(src[i]=='&'){
      if(!append_text(dst,cap,&len,"&amp;"))return 0;
    } else if(src[i]=='<'){
      if(!append_text(dst,cap,&len,"&lt;"))return 0;
    } else if(src[i]=='>'){
      if(!append_text(dst,cap,&len,"&gt;"))return 0;
    } else if(src[i]=='\n'){
      if(!append_text(dst,cap,&len,"\\n"))return 0;
    } else if(src[i]=='\r'){
      if(!append_text(dst,cap,&len,"\\r"))return 0;
    } else if(!append_char(dst,cap,&len,src[i]))return 0;
  }
  return 1;
}
