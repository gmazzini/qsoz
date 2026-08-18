// Gianluca Mazzini @2022- Version 3.0
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qsoz_config.h"

#define LINE_SIZE 2048

static char *trim(char *s) {
  char *e;

  for (;*s!='\0' && isspace((unsigned char)*s);s++) {
  }
  e=s+strlen(s);
  for (;e>s && isspace((unsigned char)e[-1]);e--) {
  }
  *e='\0';
  return s;
}

static int copy_value(char *dst,unsigned long cap,const char *src) {
  unsigned long n;

  n=(unsigned long)strlen(src);
  if (cap==0 || n>=cap) return 0;
  memcpy(dst,src,n+1);
  return 1;
}

static int parse_uint(const char *s,unsigned int min,unsigned int max,unsigned int *value) {
  char *end;
  unsigned long n;

  errno=0;
  n=strtoul(s,&end,10);
  if (errno!=0 || end==s || *end!='\0' || n<min || n>max) return 0;
  *value=(unsigned int)n;
  return 1;
}

static void set_error(char *err,unsigned long cap,const char *text,int line) {
  char tmp[256];
  unsigned long n;

  if(err==NULL || cap==0)return;
  if(line>0)snprintf(tmp,sizeof(tmp),"%s at line %d",text,line);
  else snprintf(tmp,sizeof(tmp),"%s",text);
  n=(unsigned long)strlen(tmp);
  if(n>=cap)n=cap-1;
  memcpy(err,tmp,(size_t)n);
  err[n]='\0';
}

int qsoz_config_load(QsozConfig *cfg,const char *path,char *err,unsigned long errcap) {
  FILE *fp;
  char line[LINE_SIZE],*key,*value,*eq;
  int lineno;

  if (cfg==NULL || path==NULL) {
    set_error(err,errcap,"invalid configuration arguments",0);
    return 0;
  }

  memset(cfg,0,sizeof(*cfg));
  copy_value(cfg->db_host,sizeof(cfg->db_host),"127.0.0.1");
  cfg->db_port=3306;
  copy_value(cfg->callbook_host,sizeof(cfg->callbook_host),"127.0.0.1");
  cfg->callbook_port=22223;
  cfg->callbook_timeout=5;
  copy_value(cfg->cluster_host,sizeof(cfg->cluster_host),"127.0.0.1");
  cfg->cluster_port=22222;
  cfg->cluster_timeout=5;

  fp=fopen(path,"r");
  if (fp==NULL) {
    set_error(err,errcap,"cannot open qsoz configuration",0);
    return 0;
  }

  lineno=0;
  for (;fgets(line,sizeof(line),fp)!=NULL;) {
    lineno++;
    key=trim(line);
    if (*key=='\0' || *key=='#') continue;
    eq=strchr(key,'=');
    if (eq==NULL) {
      fclose(fp);
      set_error(err,errcap,"invalid configuration line",lineno);
      return 0;
    }
    *eq='\0';
    value=trim(eq+1);
    key=trim(key);

    if (strcmp(key,"db_host")==0) {
      if (!copy_value(cfg->db_host,sizeof(cfg->db_host),value)) goto value_error;
    } else if (strcmp(key,"db_user")==0) {
      if (!copy_value(cfg->db_user,sizeof(cfg->db_user),value)) goto value_error;
    } else if (strcmp(key,"db_pass")==0) {
      if (!copy_value(cfg->db_pass,sizeof(cfg->db_pass),value)) goto value_error;
    } else if (strcmp(key,"db_name")==0) {
      if (!copy_value(cfg->db_name,sizeof(cfg->db_name),value)) goto value_error;
    } else if (strcmp(key,"db_port")==0) {
      if (!parse_uint(value,1,65535,&cfg->db_port)) goto value_error;
    } else if (strcmp(key,"callbook_host")==0) {
      if (!copy_value(cfg->callbook_host,sizeof(cfg->callbook_host),value)) goto value_error;
    } else if (strcmp(key,"callbook_port")==0) {
      if (!parse_uint(value,1,65535,&cfg->callbook_port)) goto value_error;
    } else if (strcmp(key,"callbook_timeout")==0) {
      if (!parse_uint(value,1,60,&cfg->callbook_timeout)) goto value_error;
    } else if (strcmp(key,"cluster_host")==0) {
      if (!copy_value(cfg->cluster_host,sizeof(cfg->cluster_host),value)) goto value_error;
    } else if (strcmp(key,"cluster_port")==0) {
      if (!parse_uint(value,1,65535,&cfg->cluster_port)) goto value_error;
    } else if (strcmp(key,"cluster_timeout")==0) {
      if (!parse_uint(value,1,60,&cfg->cluster_timeout)) goto value_error;
    } else {
      fclose(fp);
      set_error(err,errcap,"unknown configuration key",lineno);
      return 0;
    }
    continue;

value_error:
    fclose(fp);
    set_error(err,errcap,"invalid configuration value",lineno);
    return 0;
  }
  fclose(fp);

  if (cfg->db_user[0]=='\0' || cfg->db_name[0]=='\0') {
    set_error(err,errcap,"missing database configuration",0);
    return 0;
  }
  return 1;
}
