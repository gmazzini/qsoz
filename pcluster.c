// da cancellare
// pcluster.c cluster to memory by GM @2025 V 2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>

#define DXC_ADDR "ham.homelinux.org"
#define DXC_PORT "8000"
#define CALLSIGN "IK4LZH"
#define WHOIS_ADDR "127.0.0.1"
#define WHOIS_PORT "22222"
#define TIMEOUT_SEC 300
#define RECONNECT_WAIT 5
#define ELM 100000

struct data {
  char dx[20];
  char spotter[20];
  long freq;
  time_t time;
} *data;
long ndata=0;
struct bandplane {
  int band;
  int mode;
  long start;
  long end;
};
struct bandplane bands[] = {
  {8,1,1810000,1838000},
  {8,2,1838000,1843000},
  {8,0,1843000,2000000},
  {7,1,3500000,3570000},
  {7,2,3570000,3600000},
  {7,0,3600000,3800000},
  {6,1,7000000,7040000},
  {6,2,7040000,7080000},
  {6,0,7060000,7200000},
  {5,1,14000000,14070000},
  {5,2,14070000,14112000},
  {5,0,14112000,14350000},
  {4,1,21000000,21070000},
  {4,2,21070000,21151000},
  {4,0,21151000,21450000},
  {3,1,28000000,28070000},
  {3,2,28070000,28320000},
  {3,0,28320000,29700000},
  {11,1,10100000,10130000},
  {11,2,10130000,10150000},
  {10,1,18068000,18095000},
  {10,2,18095000,18111000},
  {10,0,18111000,18168000},
  {9,1,24890000,24915000},
  {9,2,24915000,24931000},
  {9,0,24931000,24990000},
  {12,1,5151500,5153000},
  {12,2,5154000,5366000},
  {12,0,5366000,5366500}
};
int nbands=sizeof(bands)/sizeof(bands[0]);
pthread_mutex_t data_mtx=PTHREAD_MUTEX_INITIALIZER;

static void *answer_thread(void *arg) {
  (void)arg;
  int ls,cs,one,r,c,mypage,xx;
  struct addrinfo hints,*res=NULL;
  char buf[100],out[100],filter[50];
  long i,idx;

  memset(&hints,0,sizeof(hints));
  hints.ai_family=AF_UNSPEC;
  hints.ai_socktype=SOCK_STREAM;
  hints.ai_flags=AI_PASSIVE;
  if(getaddrinfo(WHOIS_ADDR,WHOIS_PORT,&hints,&res)!=0)return NULL;
  ls=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
  if(ls<0){freeaddrinfo(res); return NULL;}
  one=1;
  setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
  if(bind(ls,res->ai_addr,res->ai_addrlen)!=0){freeaddrinfo(res); close(ls); return NULL;}
  freeaddrinfo(res);
  if(listen(ls,16)!=0){close(ls); return NULL;}
  for(;;){
    cs=accept(ls,NULL,NULL);
    if(cs<0)continue;
    r=recv(cs,buf,99,0);
    if(r<0){close(cs); continue;}
    buf[r]='\0';
    sscanf(buf,"%d,%s",&mypage,filter);
    pthread_mutex_lock(&data_mtx);
    xx=0;
    for(i=0;i<ELM;i++){
      idx=(ndata-1-i+ELM)%ELM;
      if(data[idx].freq>0){
        for(c=0;c<nbands;c++)if(data[idx].freq>=bands[c].start && data[idx].freq<=bands[c].end)break;
        if(c==nbands)continue;
        if(filter[bands[c].band]=='0' || filter[bands[c].mode]=='0')continue;
        sprintf(out,"%ld,%s,%ld,%s\n",data[idx].time,data[idx].spotter,data[idx].freq,data[idx].dx);
        send(cs,out,strlen(out),0);
        xx++;
        if(xx>=mypage)break;
      }
    }
    pthread_mutex_unlock(&data_mtx);
    close(cs);
  }
}

int main(void){
  struct addrinfo hints,*res,*rp;
  int sock,rc,one=1; 
  char buf[4096],aux1[300],*p,*q1,*q2,*q3;
  struct timeval to; 
  pthread_t th;
  long i;

  data=(struct data *)malloc(ELM*sizeof(struct data));
  if(data==NULL)return 0;
  for(i=0;i<ELM;i++)data[i].freq=0;
  pthread_create(&th,NULL,answer_thread,NULL);
  pthread_detach(th);

reconnect:
  memset(&hints,0,sizeof(hints));
  hints.ai_family=AF_UNSPEC;
  hints.ai_socktype=SOCK_STREAM;
  if(getaddrinfo(DXC_ADDR,DXC_PORT,&hints,&res)!=0){sleep(RECONNECT_WAIT); goto reconnect;}
  sock=-1;
  for(rp=res;rp;rp=rp->ai_next){
    sock=socket(rp->ai_family,rp->ai_socktype,rp->ai_protocol);
    if(sock<0)continue;
    if(connect(sock,rp->ai_addr,rp->ai_addrlen)==0)break;
    close(sock);
    sock=-1; 
  }
  freeaddrinfo(res);
  if(sock<0){sleep(RECONNECT_WAIT); goto reconnect;}
  to.tv_sec=TIMEOUT_SEC; to.tv_usec=0;
  setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&to,sizeof(to));
  setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,&one,sizeof(one));

  for(;;){
    rc=recv(sock,buf,sizeof(buf)-1,0);
    if(rc<=0){close(sock); sleep(RECONNECT_WAIT); goto reconnect;}
    buf[rc]='\0';
    if(strstr(buf,"login:")){
      sprintf(aux1,"%s\n",CALLSIGN);
      send(sock,aux1,strlen(aux1),0);
      continue;
    }
    p=strstr(buf,"DX de ");
    if(p==NULL)continue;
    q1=strtok(p+6," ");
    q1[strlen(q1)-1]='\0';
    q2=strtok(NULL," ");
    q3=strtok(NULL," ");
    pthread_mutex_lock(&data_mtx);
    strcpy(data[ndata].dx,q3);
    strcpy(data[ndata].spotter,q1);
    data[ndata].freq=atof(q2)*1000;
    data[ndata].time=time(NULL);
    ndata=(ndata+1)%ELM;
    pthread_mutex_unlock(&data_mtx);
  }
}
