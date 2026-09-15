// Gianluca Mazzini @2022- Version 4.1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qsoz_version.h"

int qsoz_clock_main(void) {
  const char *query;
  time_t epoch;

  query=getenv("QUERY_STRING");
  printf("Content-Type: text/plain\r\n\r\n");
  if(query!=NULL && strcmp(query,"release")==0){
    printf("%s\n",QSOZ_RELEASE);
    return 0;
  }
  epoch=time(NULL);
  if(epoch==(time_t)-1)return 1;
  printf("%lld\n",(long long)epoch);
  return 0;
}
