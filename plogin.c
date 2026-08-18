// Gianluca Mazzini @2022- Version 3.03
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mysql/mysql.h>
#include <sodium.h>
#include "qsoz_config.h"

#define INPUT_SIZE 256
#define QUERY_SIZE 2048
#define FILTER_SIZE 21
#define OTA_SIZE 17
#define CALL_SIZE 21
#define PASS_SIZE 129
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
  while(n>0 && (input[n-1]=='\n' || input[n-1]=='\r'))input[--n]='\0';
  got_call=0;
  got_pass=0;
  p=input;
  for(;;){
    amp=strchr(p,'&');
    if(amp!=NULL)*amp='\0';
    eq=strchr(p,'=');
    if(eq==NULL)return 0;
    *eq='\0';
    eq++;
    if(strcmp(p,"call")==0){
      if(got_call || !url_decode(call,callcap,eq,(unsigned long)strlen(eq)))return 0;
      got_call=1;
    } else if(strcmp(p,"password")==0){
      if(got_pass || !url_decode(passwd,passcap,eq,(unsigned long)strlen(eq)))return 0;
      got_pass=1;
    }
    if(amp==NULL)break;
    p=amp+1;
  }
  return got_call && got_pass && call[0]!='\0' && passwd[0]!='\0';
}

static int valid_call(const char *s) {
  int i,n;

  n=(int)strlen(s);
  if(n<2 || n>=CALL_SIZE)return 0;
  for(i=0;i<n;i++){
    if(!isalnum((unsigned char)s[i]) && s[i]!='/' && s[i]!='-')return 0;
  }
  return 1;
}

static int make_ota(char *out,unsigned long cap) {
  static const char charset[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  unsigned char b;
  int i;

  if(cap<OTA_SIZE)return 0;
  i=0;
  for(;i<16;){
    randombytes_buf(&b,1);
    if(b>=248)continue;
    out[i++]=charset[b%62];
  }
  out[16]='\0';
  return 1;
}

int main(void) {
  QsozConfig cfg;
  MYSQL *con;
  MYSQL_RES *res;
  MYSQL_ROW row;
  char call[CALL_SIZE],passwd[PASS_SIZE],ecall[CALL_SIZE*2+1];
  char query[QUERY_SIZE],ota[OTA_SIZE],filter[FILTER_SIZE],err[ERR_SIZE];
  char newhash[crypto_pwhash_STRBYTES],ehash[crypto_pwhash_STRBYTES*2+1];
  const char *stored_hash;
  unsigned long n;
  long mypage;
  time_t epoch;
  int ok,need_rehash;

  ota[0]='\0';
  filter[0]='\0';
  mypage=0;
  newhash[0]='\0';
  printf("Content-Type: text/plain\r\n\r\n");
  if(sodium_init()<0){printf(",0,\n"); return 0;}
  if(!read_request(call,sizeof(call),passwd,sizeof(passwd)) || !valid_call(call)){
    sodium_memzero(passwd,sizeof(passwd));
    printf(",0,\n");
    return 0;
  }
  if(!qsoz_config_load(&cfg,QSOZ_CONFIG_FILE,err,sizeof(err))){
    fprintf(stderr,"plogin: %s\n",err);
    sodium_memzero(passwd,sizeof(passwd));
    printf(",0,\n");
    return 0;
  }
  con=mysql_init(NULL);
  if(con==NULL){sodium_memzero(passwd,sizeof(passwd)); printf(",0,\n"); return 0;}
  if(mysql_real_connect(con,cfg.db_host,cfg.db_user,cfg.db_pass,cfg.db_name,cfg.db_port,NULL,0)==NULL){
    fprintf(stderr,"plogin: mysql connect error: %s\n",mysql_error(con));
    mysql_close(con);
    sodium_memzero(passwd,sizeof(passwd));
    printf(",0,\n");
    return 0;
  }
  mysql_real_escape_string(con,ecall,call,(unsigned long)strlen(call));
  n=(unsigned long)snprintf(query,sizeof(query),"select mypage,filter,passwd_hash from user where mycall='%s' limit 1",ecall);
  if(n>=sizeof(query) || mysql_query(con,query)!=0){
    fprintf(stderr,"plogin: mysql select error: %s\n",mysql_error(con));
    mysql_close(con);
    sodium_memzero(passwd,sizeof(passwd));
    printf(",0,\n");
    return 0;
  }
  res=mysql_store_result(con);
  if(res==NULL){mysql_close(con); sodium_memzero(passwd,sizeof(passwd)); printf(",0,\n"); return 0;}
  row=mysql_fetch_row(res);
  ok=0;
  need_rehash=0;
  stored_hash=NULL;
  if(row!=NULL){
    stored_hash=row[2];
    if(stored_hash!=NULL && stored_hash[0]!='\0' && crypto_pwhash_str_verify(stored_hash,passwd,strlen(passwd))==0){
      ok=1;
      need_rehash=crypto_pwhash_str_needs_rehash(stored_hash,crypto_pwhash_OPSLIMIT_INTERACTIVE,crypto_pwhash_MEMLIMIT_INTERACTIVE)!=0;
    }
    if(ok){
      mypage=strtol(row[0],NULL,10);
      if(row[1]!=NULL){
        strncpy(filter,row[1],sizeof(filter)-1);
        filter[sizeof(filter)-1]='\0';
      }
    }
  }
  mysql_free_result(res);

  if(ok && need_rehash){
    if(crypto_pwhash_str(newhash,passwd,strlen(passwd),crypto_pwhash_OPSLIMIT_INTERACTIVE,crypto_pwhash_MEMLIMIT_INTERACTIVE)!=0){
      ok=0;
    } else {
      mysql_real_escape_string(con,ehash,newhash,(unsigned long)strlen(newhash));
      n=(unsigned long)snprintf(query,sizeof(query),"update user set passwd_hash='%s' where mycall='%s'",ehash,ecall);
      if(n>=sizeof(query) || mysql_query(con,query)!=0){
        fprintf(stderr,"plogin: password migration update error: %s\n",mysql_error(con));
        ok=0;
      }
    }
  }
  if(ok && make_ota(ota,sizeof(ota))){
    epoch=time(NULL);
    n=(unsigned long)snprintf(query,sizeof(query),"update user set ota='%s',lastota=%lld where mycall='%s'",ota,(long long)epoch,ecall);
    if(n>=sizeof(query) || mysql_query(con,query)!=0){
      fprintf(stderr,"plogin: ota update error: %s\n",mysql_error(con));
      ok=0;
    }
  } else if(ok)ok=0;

  if(!ok){ota[0]='\0'; filter[0]='\0'; mypage=0;}
  printf("%s,%ld,%s\n",ota,mypage,filter);
  sodium_memzero(passwd,sizeof(passwd));
  sodium_memzero(newhash,sizeof(newhash));
  mysql_close(con);
  return 0;
}
