// Gianluca Mazzini @2022- Version 3.02
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "qsoz_util.h"

int qsoz_band(int mhz) {
  switch (mhz) {
    case 1: return 1600;
    case 3: return 800;
    case 5: return 600;
    case 7: return 400;
    case 10: return 300;
    case 14: return 200;
    case 18: return 170;
    case 21: return 150;
    case 24: return 120;
    case 28:
    case 29: return 100;
    case 50: return 60;
    case 144:
    case 145: return 20;
    case 430:
    case 431:
    case 432:
    case 433: return 7;
  }
  return 0;
}

const char *qsoz_mode(const char *mode) {
  if (mode==NULL) return "ND";
  if (strcmp(mode,"CW")==0) return "CW";
  if (strcmp(mode,"FT8")==0 || strcmp(mode,"RTTY")==0 || strcmp(mode,"MFSK")==0 || strcmp(mode,"FT4")==0 || strcmp(mode,"PKT")==0 || strcmp(mode,"TOR")==0 || strcmp(mode,"AMTOR")==0 || strcmp(mode,"PSK")==0) return "DG";
  if (strcmp(mode,"SSB")==0 || strcmp(mode,"USB")==0 || strcmp(mode,"LSB")==0 || strcmp(mode,"FM")==0 || strcmp(mode,"AM")==0) return "PH";
  return "ND";
}

static int wpx_ignored_designator(const char *s) {
  static const char *ignored[]={"A","E","J","P","M","MM","AM","QRP","QRPP"};
  int i,n;

  if(s==NULL || s[0]=='\0')return 1;
  n=(int)(sizeof(ignored)/sizeof(ignored[0]));
  for(i=0;i<n;i++)if(strcmp(s,ignored[i])==0)return 1;
  return 0;
}

static int wpx_digits_only(const char *s) {
  int i;

  if(s==NULL || s[0]=='\0')return 0;
  for(i=0;s[i]!='\0';i++)if(!isdigit((unsigned char)s[i]))return 0;
  return 1;
}

static void wpx_prefix_part(char *out,unsigned long cap,const char *part) {
  unsigned long i,j,n;
  int seen_letter;

  if(out==NULL || cap==0)return;
  out[0]='\0';
  if(part==NULL || part[0]=='\0')return;
  n=(unsigned long)strlen(part);
  seen_letter=0;
  for(i=0;i<n;i++){
    if(isalpha((unsigned char)part[i]))seen_letter=1;
    else if(isdigit((unsigned char)part[i]) && seen_letter){
      for(j=i+1;j<n && isdigit((unsigned char)part[j]);j++){
      }
      if(j>=cap)j=cap-1;
      memcpy(out,part,(size_t)j);
      out[j]='\0';
      return;
    }
  }
  if(n>=cap)n=cap-1;
  if(n>2)n=2;
  memcpy(out,part,(size_t)n);
  if(n+1<cap){
    out[n]='0';
    out[n+1]='\0';
  } else out[n]='\0';
}

const char *qsoz_wpx(const char *callsign) {
  static char out[20];
  char call[64],part[4][20],base[20],tmp[20];
  unsigned long n,pos,start,len,best_len;
  int count,i,best,base_idx;

  out[0]='\0';
  if(callsign==NULL || callsign[0]=='\0')return out;
  n=(unsigned long)strlen(callsign);
  if(n>=sizeof(call))n=sizeof(call)-1;
  for(i=0;i<(int)n;i++)call[i]=(char)toupper((unsigned char)callsign[i]);
  call[n]='\0';

  count=0;
  start=0;
  for(pos=0;pos<=n && count<4;pos++){
    if(call[pos]=='/' || call[pos]=='\0'){
      len=pos-start;
      if(len>=sizeof(part[0]))len=sizeof(part[0])-1;
      memcpy(part[count],call+start,(size_t)len);
      part[count][len]='\0';
      count++;
      start=pos+1;
    }
  }
  if(count<=1){
    wpx_prefix_part(out,sizeof(out),call);
    return out;
  }

  best=-1;
  best_len=1000UL;
  base_idx=-1;
  for(i=0;i<count;i++){
    len=(unsigned long)strlen(part[i]);
    if(len>best_len)base_idx=i;
    if(wpx_ignored_designator(part[i]))continue;
    if(len<best_len){
      best=i;
      best_len=len;
    }
  }
  if(best<0){
    wpx_prefix_part(out,sizeof(out),part[0]);
    return out;
  }

  if(wpx_digits_only(part[best])){
    base_idx=-1;
    best_len=0;
    for(i=0;i<count;i++){
      if(i==best || wpx_ignored_designator(part[i]))continue;
      len=(unsigned long)strlen(part[i]);
      if(len>best_len){base_idx=i; best_len=len;}
    }
    if(base_idx<0)return out;
    wpx_prefix_part(base,sizeof(base),part[base_idx]);
    len=(unsigned long)strlen(base);
    for(;len>0 && isdigit((unsigned char)base[len-1]);len--)base[len-1]='\0';
    if(strlen(base)+strlen(part[best])>=sizeof(tmp))return out;
    strcpy(tmp,base);
    strcat(tmp,part[best]);
    wpx_prefix_part(out,sizeof(out),tmp);
    return out;
  }

  wpx_prefix_part(out,sizeof(out),part[best]);
  return out;
}

static int pacc_digit(const char *s) {
  int i,d;

  d=-1;
  if(s==NULL)return -1;
  for(i=0;s[i]!='\0';i++)if(isdigit((unsigned char)s[i]))d=s[i]-'0';
  return d;
}

static int pacc_prefix(const char *s,const char *prefix) {
  size_t n;

  if(s==NULL || prefix==NULL)return 0;
  n=strlen(prefix);
  return strncmp(s,prefix,n)==0;
}

const char *qsoz_pacc_area(const char *callsign,int dxcc) {
  static char out[8];
  char call[32],first[16],last[16],*p,*q;
  size_t n;
  int d,reciprocal;

  out[0]='\0';
  if(callsign==NULL)return out;
  n=strlen(callsign);
  if(n==0 || n>=sizeof(call))return out;
  for(d=0;d<(int)n;d++)call[d]=(char)toupper((unsigned char)callsign[d]);
  call[n]='\0';

  first[0]='\0';
  last[0]='\0';
  p=strchr(call,'/');
  reciprocal=0;
  if(p!=NULL){
    n=(size_t)(p-call);
    if(n>=sizeof(first))n=sizeof(first)-1;
    memcpy(first,call,n);
    first[n]='\0';
    q=strrchr(call,'/');
    if(q!=NULL){
      q++;
      n=strlen(q);
      if(n>=sizeof(last))n=sizeof(last)-1;
      memcpy(last,q,n);
      last[n]='\0';
    }
  }
  else {
    n=strlen(call);
    if(n>=sizeof(first))n=sizeof(first)-1;
    memcpy(first,call,n);
    first[n]='\0';
  }

  d=-1;
  if(p!=NULL){
    if((dxcc==15 && pacc_prefix(first,"UA")) ||
       (dxcc==112 && pacc_prefix(first,"CE")) ||
       (dxcc==339 && pacc_prefix(first,"JA")) ||
       (dxcc==100 && pacc_prefix(first,"LU")) ||
       (dxcc==108 && pacc_prefix(first,"PY")) ||
       (dxcc==1 && (pacc_prefix(first,"VE") || pacc_prefix(first,"VO") || pacc_prefix(first,"VY"))) ||
       (dxcc==291 && pacc_prefix(first,"W")) ||
       (dxcc==150 && pacc_prefix(first,"VK")) ||
       (dxcc==462 && pacc_prefix(first,"ZS")) ||
       (dxcc==170 && pacc_prefix(first,"ZL"))){
      reciprocal=1;
      d=pacc_digit(first);
    }
    else if(last[0]!='\0' && (strlen(last)==1 || isalpha((unsigned char)last[0])))d=pacc_digit(last);
  }
  if(d<0 && !reciprocal)d=pacc_digit(first);

  if(dxcc==15){
    if(d!=0 && d!=8 && d!=9)return out;
    sprintf(out,"UA%d",d);
  }
  else if(dxcc==112){
    if(d<0)d=0;
    sprintf(out,"CE%d",d);
  }
  else if(dxcc==339){
    if(d<0)return out;
    sprintf(out,"JA%d",d);
  }
  else if(dxcc==100){
    if(d<0)d=0;
    sprintf(out,"LU%d",d);
  }
  else if(dxcc==108){
    if(d<0)d=0;
    sprintf(out,"PY%d",d);
  }
  else if(dxcc==1){
    if(d<0)return out;
    if(pacc_prefix(first,"VO"))sprintf(out,"VO%d",d);
    else if(pacc_prefix(first,"VY"))sprintf(out,"VY%d",d);
    else sprintf(out,"VE%d",d);
  }
  else if(dxcc==291){
    if(d<0)return out;
    sprintf(out,"W%d",d);
  }
  else if(dxcc==150){
    if(d<0)d=0;
    sprintf(out,"VK%d",d);
  }
  else if(dxcc==462){
    if(d<0)d=0;
    sprintf(out,"ZS%d",d);
  }
  else if(dxcc==170){
    if(d<0)d=0;
    sprintf(out,"ZL%d",d);
  }
  return out;
}

long qsoz_min_long(long a,long b) {
  return a<b?a:b;
}

const char *qsoz_elapsed(long seconds) {
  static char out[32];

  if (seconds<3600) snprintf(out,sizeof(out),"%2ldm",seconds/60);
  else if (seconds<86400) snprintf(out,sizeof(out),"%2ldh",seconds/3600);
  else if (seconds<2592000) snprintf(out,sizeof(out),"%2ldD",seconds/86400);
  else if (seconds<31536000) snprintf(out,sizeof(out),"%2ldM",seconds/2592000);
  else snprintf(out,sizeof(out),"%2ldY",seconds/31536000);
  return out;
}

int qsoz_nfields(const char *s) {
  int count,in_token;

  if (s==NULL) return 0;
  count=0;
  in_token=0;
  for (;*s!='\0';s++) {
    if (isspace((unsigned char)*s)) in_token=0;
    else if (!in_token) {
      in_token=1;
      count++;
    }
  }
  return count;
}

int qsoz_token_valid(const char *token) {
  int i;

  if(token==NULL || strlen(token)!=16)return 0;
  for(i=0;i<16;i++){
    if(!((token[i]>='0' && token[i]<='9') || (token[i]>='A' && token[i]<='Z') || (token[i]>='a' && token[i]<='z')))return 0;
  }
  return 1;
}

int qsoz_copy(char *dst,unsigned long cap,const char *src) {
  unsigned long n;

  if(dst==NULL || cap==0 || src==NULL)return 0;
  n=(unsigned long)strlen(src);
  if(n>=cap)return 0;
  memcpy(dst,src,(size_t)n+1);
  return 1;
}
