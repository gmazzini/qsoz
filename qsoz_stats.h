// Gianluca Mazzini @2022- Version 3.02
#ifndef QSOZ_STATS_H
#define QSOZ_STATS_H

#define QSOZ_STATS_CHANNELS 5
#define QSOZ_STATS_BUCKETS 400
#define QSOZ_STATS_LABEL 32
#define QSOZ_STATS_MAX_ITEMS 200000L
#define TOT3 QSOZ_STATS_CHANNELS
#define TOTL2 QSOZ_STATS_BUCKETS

typedef struct data3 {
  char lab[QSOZ_STATS_LABEL];
  long num;
  long idx;
} Data3;

extern Data3 ***data3;
extern long **ndata3;

int qsoz_stats_init(void);
void qsoz_stats_reset(void);
void qsoz_stats_free(void);
int qsoz_stats_sort_bucket(int cha,int idx);
long incdata3(int cha,int idx,const char *key,long ss,long dd);
long numdata3(int cha,int idx,const char *key);
int cmp3(const void *a,const void *b);

#endif
