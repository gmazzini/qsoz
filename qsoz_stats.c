// Gianluca Mazzini @2022- Version 3.02
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qsoz_stats.h"

#define INITIAL_DATA_CAPACITY 8L
#define INITIAL_HASH_CAPACITY 16UL

typedef struct {
  uint32_t *slot;
  unsigned long cap;
} HashBucket;

Data3 ***data3;
long **ndata3;
static long **capacity3;
static HashBucket **hash3;

static int valid_bucket(int cha,int idx) {
  return cha>=0 && cha<QSOZ_STATS_CHANNELS && idx>=0 && idx<QSOZ_STATS_BUCKETS;
}

static unsigned long hash_key(const char *s) {
  uint32_t h;

  h=2166136261U;
  for (;*s!='\0';s++) {
    h^=(unsigned char)*s;
    h*=16777619U;
  }
  return (unsigned long)h;
}

static int grow_data(int cha,int idx,long needed) {
  Data3 *p;
  long cap;

  if (!valid_bucket(cha,idx) || needed>QSOZ_STATS_MAX_ITEMS) return 0;
  cap=capacity3[cha][idx];
  if (cap>=needed) return 1;
  if (cap==0) cap=INITIAL_DATA_CAPACITY;
  for (;cap<needed;) {
    if (cap>=QSOZ_STATS_MAX_ITEMS/2) {
      cap=QSOZ_STATS_MAX_ITEMS;
      break;
    }
    cap*=2;
  }
  p=(Data3 *)realloc(data3[cha][idx],(size_t)cap*sizeof(Data3));
  if (p==NULL) return 0;
  data3[cha][idx]=p;
  capacity3[cha][idx]=cap;
  return 1;
}

static int hash_rebuild(int cha,int idx,unsigned long newcap) {
  uint32_t *slot;
  long i;
  unsigned long pos,mask;

  slot=(uint32_t *)calloc(newcap,sizeof(uint32_t));
  if (slot==NULL) return 0;
  mask=newcap-1;
  for (i=0;i<ndata3[cha][idx];i++) {
    pos=hash_key(data3[cha][idx][i].lab)&mask;
    for (;slot[pos]!=0;pos=(pos+1)&mask) {
    }
    slot[pos]=(uint32_t)(i+1);
  }
  free(hash3[cha][idx].slot);
  hash3[cha][idx].slot=slot;
  hash3[cha][idx].cap=newcap;
  return 1;
}

static int hash_prepare(int cha,int idx,long needed) {
  unsigned long cap;

  cap=hash3[cha][idx].cap;
  if (cap==0) return hash_rebuild(cha,idx,INITIAL_HASH_CAPACITY);
  if ((unsigned long)needed*10UL<cap*7UL) return 1;
  if (cap>=(unsigned long)QSOZ_STATS_MAX_ITEMS*2UL) return 1;
  return hash_rebuild(cha,idx,cap*2UL);
}

static long hash_find(int cha,int idx,const char *key) {
  uint32_t value;
  unsigned long pos,mask,start;

  if (hash3[cha][idx].cap==0) return -1;
  mask=hash3[cha][idx].cap-1;
  pos=hash_key(key)&mask;
  start=pos;
  for (;;) {
    value=hash3[cha][idx].slot[pos];
    if (value==0) return -1;
    if (strcmp(data3[cha][idx][value-1].lab,key)==0) return value-1;
    pos=(pos+1)&mask;
    if (pos==start) return -1;
  }
}

static int hash_insert(int cha,int idx,long posdata) {
  unsigned long pos,mask;

  mask=hash3[cha][idx].cap-1;
  pos=hash_key(data3[cha][idx][posdata].lab)&mask;
  for (;hash3[cha][idx].slot[pos]!=0;pos=(pos+1)&mask) {
  }
  hash3[cha][idx].slot[pos]=(uint32_t)(posdata+1);
  return 1;
}

static int cmp_label(const void *a,const void *b) {
  const Data3 *x,*y;

  x=(const Data3 *)a;
  y=(const Data3 *)b;
  return strcmp(x->lab,y->lab);
}

int qsoz_stats_init(void) {
  int cha;

  data3=(Data3 ***)calloc(QSOZ_STATS_CHANNELS,sizeof(Data3 **));
  ndata3=(long **)calloc(QSOZ_STATS_CHANNELS,sizeof(long *));
  capacity3=(long **)calloc(QSOZ_STATS_CHANNELS,sizeof(long *));
  hash3=(HashBucket **)calloc(QSOZ_STATS_CHANNELS,sizeof(HashBucket *));
  if (data3==NULL || ndata3==NULL || capacity3==NULL || hash3==NULL) goto fail;
  for (cha=0;cha<QSOZ_STATS_CHANNELS;cha++) {
    data3[cha]=(Data3 **)calloc(QSOZ_STATS_BUCKETS,sizeof(Data3 *));
    ndata3[cha]=(long *)calloc(QSOZ_STATS_BUCKETS,sizeof(long));
    capacity3[cha]=(long *)calloc(QSOZ_STATS_BUCKETS,sizeof(long));
    hash3[cha]=(HashBucket *)calloc(QSOZ_STATS_BUCKETS,sizeof(HashBucket));
    if (data3[cha]==NULL || ndata3[cha]==NULL || capacity3[cha]==NULL || hash3[cha]==NULL) goto fail;
  }
  return 1;

fail:
  qsoz_stats_free();
  return 0;
}

void qsoz_stats_reset(void) {
  int cha,idx;

  if (ndata3==NULL) return;
  for (cha=0;cha<QSOZ_STATS_CHANNELS;cha++) {
    for (idx=0;idx<QSOZ_STATS_BUCKETS;idx++) {
      ndata3[cha][idx]=0;
      if (hash3[cha][idx].slot!=NULL) memset(hash3[cha][idx].slot,0,hash3[cha][idx].cap*sizeof(uint32_t));
    }
  }
}

void qsoz_stats_free(void) {
  int cha,idx;

  if (data3!=NULL) {
    for (cha=0;cha<QSOZ_STATS_CHANNELS;cha++) {
      if (data3[cha]!=NULL) {
        for (idx=0;idx<QSOZ_STATS_BUCKETS;idx++) free(data3[cha][idx]);
        free(data3[cha]);
      }
    }
    free(data3);
  }
  if (ndata3!=NULL) {
    for (cha=0;cha<QSOZ_STATS_CHANNELS;cha++) free(ndata3[cha]);
    free(ndata3);
  }
  if (capacity3!=NULL) {
    for (cha=0;cha<QSOZ_STATS_CHANNELS;cha++) free(capacity3[cha]);
    free(capacity3);
  }
  if (hash3!=NULL) {
    for (cha=0;cha<QSOZ_STATS_CHANNELS;cha++) {
      if (hash3[cha]!=NULL) {
        for (idx=0;idx<QSOZ_STATS_BUCKETS;idx++) free(hash3[cha][idx].slot);
        free(hash3[cha]);
      }
    }
    free(hash3);
  }
  data3=NULL;
  ndata3=NULL;
  capacity3=NULL;
  hash3=NULL;
}

int qsoz_stats_sort_bucket(int cha,int idx) {
  unsigned long cap;

  if (!valid_bucket(cha,idx)) return 0;
  if (ndata3[cha][idx]>1) qsort(data3[cha][idx],(size_t)ndata3[cha][idx],sizeof(Data3),cmp_label);
  cap=hash3[cha][idx].cap;
  if (cap==0 || ndata3[cha][idx]==0) return 1;
  return hash_rebuild(cha,idx,cap);
}

long incdata3(int cha,int idx,const char *key,long ss,long dd) {
  long n,pos;
  size_t len;

  if (!valid_bucket(cha,idx) || key==NULL) return -1;
  len=strlen(key);
  if (len>=QSOZ_STATS_LABEL) return -1;
  pos=hash_find(cha,idx,key);
  if (pos>=0) {
    data3[cha][idx][pos].num+=dd;
    return data3[cha][idx][pos].idx;
  }
  n=ndata3[cha][idx];
  if (n>=QSOZ_STATS_MAX_ITEMS) return -1;
  if (!grow_data(cha,idx,n+1) || !hash_prepare(cha,idx,n+1)) return -1;
  memcpy(data3[cha][idx][n].lab,key,len+1);
  data3[cha][idx][n].idx=n;
  data3[cha][idx][n].num=ss;
  ndata3[cha][idx]=n+1;
  hash_insert(cha,idx,n);
  return n;
}

long numdata3(int cha,int idx,const char *key) {
  long pos;

  if (!valid_bucket(cha,idx) || key==NULL) return 0;
  pos=hash_find(cha,idx,key);
  if (pos<0) return 0;
  return data3[cha][idx][pos].num;
}

int cmp3(const void *a,const void *b) {
  const Data3 *x,*y;

  x=(const Data3 *)a;
  y=(const Data3 *)b;
  if (x->num<y->num) return 1;
  if (x->num>y->num) return -1;
  return 0;
}
