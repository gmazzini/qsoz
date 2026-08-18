// Gianluca Mazzini @2022- Version 3.01
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "qsoz_time.h"

static int valid_epoch_fields(int year,int month,int day,int hour,int min,int sec,time_t *epoch) {
  struct tm t,*check;
  time_t e;

  if (epoch==NULL) return 0;
  if (year<1970 || month<1 || month>12 || day<1 || day>31 || hour<0 || hour>23 || min<0 || min>59 || sec<0 || sec>60) return 0;
  memset(&t,0,sizeof(t));
  t.tm_year=year-1900;
  t.tm_mon=month-1;
  t.tm_mday=day;
  t.tm_hour=hour;
  t.tm_min=min;
  t.tm_sec=sec;
  t.tm_isdst=0;
  e=timegm(&t);
  check=gmtime(&e);
  if (check==NULL) return 0;
  if (check->tm_year!=year-1900 || check->tm_mon!=month-1 || check->tm_mday!=day || check->tm_hour!=hour || check->tm_min!=min || check->tm_sec!=sec) return 0;
  *epoch=e;
  return 1;
}

int qsoz_datetime_to_epoch(const char *datetime,time_t *epoch) {
  int year,month,day,hour,min,sec;
  char extra;

  if (datetime==NULL || epoch==NULL) return 0;
  if (sscanf(datetime,"%4d-%2d-%2d %2d:%2d:%2d%c",&year,&month,&day,&hour,&min,&sec,&extra)!=6) return 0;
  return valid_epoch_fields(year,month,day,hour,min,sec,epoch);
}

int qsoz_epoch_to_datetime(time_t epoch,char *out,unsigned long cap) {
  struct tm *t;

  if (out==NULL || cap<20) return 0;
  t=gmtime(&epoch);
  if (t==NULL) return 0;
  return strftime(out,(size_t)cap,"%Y-%m-%d %H:%M:%S",t)==19;
}

time_t qsoz_datetime_epoch(const char *datetime) {
  time_t epoch;

  if (!qsoz_datetime_to_epoch(datetime,&epoch)) return (time_t)-1;
  return epoch;
}

time_t qsoz_date_clock_epoch(const char *date,const char *clock) {
  int year,month,day,hour,min,sec,n;
  time_t epoch;

  if (date==NULL || clock==NULL) return (time_t)-1;
  year=month=day=hour=min=sec=0;
  if (strchr(date,'-')!=NULL) n=sscanf(date,"%4d-%2d-%2d",&year,&month,&day);
  else n=sscanf(date,"%4d%2d%2d",&year,&month,&day);
  if (n!=3) return (time_t)-1;
  n=sscanf(clock,"%2d:%2d:%2d",&hour,&min,&sec);
  if (n<2) {
    hour=min=sec=0;
    n=sscanf(clock,"%2d%2d%2d",&hour,&min,&sec);
    if (n<2) return (time_t)-1;
  }
  if (!valid_epoch_fields(year,month,day,hour,min,sec,&epoch)) return (time_t)-1;
  return epoch;
}

char *qsoz_epoch_text(time_t epoch) {
  static char out[20];

  if (!qsoz_epoch_to_datetime(epoch,out,sizeof(out))) out[0]='\0';
  return out;
}
