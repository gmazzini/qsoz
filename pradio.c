// Gianluca Mazzini @2022- Version 3.01
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <mysql/mysql.h>
#include "qsoz_config.h"

#define INPUT_SIZE 256
#define QUERY_SIZE 512
#define RADIO_SIZE 64
#define UDEF_SIZE 21
#define CMD_SIZE 128
#define RECORD_SIZE 256
#define STREAM_SIZE 512
#define ERR_SIZE 256

typedef struct {
  int fd;
  char buf[STREAM_SIZE];
  unsigned long start;
  unsigned long end;
} RadioStream;

static const char *modets890s[16]={"Unused","LSB","USB","CW","FM","AM","FSK","CW-R","Unused","FSK-R","PSK","PSK-R","LSB-D","USB-D","FM-D","AM-D"};

static int read_request(char tok[][128],int count) {
  char input[INPUT_SIZE],*p,*comma;
  size_t n;
  int i;

  n=fread(input,1,sizeof(input)-1,stdin);
  if (ferror(stdin) || (!feof(stdin) && n==sizeof(input)-1)) return 0;
  input[n]='\0';
  while (n>0 && (input[n-1]=='\n' || input[n-1]=='\r')) input[--n]='\0';
  p=input;
  for (i=0;i<count;i++) {
    comma=(i<count-1)?strchr(p,','):NULL;
    if (i<count-1 && comma==NULL) return 0;
    if (comma!=NULL) *comma='\0';
    if (strlen(p)>=128) return 0;
    strcpy(tok[i],p);
    if (comma!=NULL) p=comma+1;
  }
  return tok[0][0]!='\0' && tok[1][0]!='\0';
}

static int connect_tcp(const char *host,unsigned int port,unsigned int timeout) {
  struct addrinfo hints,*res,*rp;
  struct timeval tv;
  fd_set wfds;
  char service[16];
  int fd,flags,rc,error;
  socklen_t error_len;

  memset(&hints,0,sizeof(hints));
  hints.ai_family=AF_UNSPEC;
  hints.ai_socktype=SOCK_STREAM;
  snprintf(service,sizeof(service),"%u",port);
  if (getaddrinfo(host,service,&hints,&res)!=0) return -1;
  fd=-1;
  for (rp=res;rp!=NULL;rp=rp->ai_next) {
    fd=socket(rp->ai_family,rp->ai_socktype,rp->ai_protocol);
    if (fd<0) continue;
    flags=fcntl(fd,F_GETFL,0);
    if (flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0) {
      close(fd);
      fd=-1;
      continue;
    }
    rc=connect(fd,rp->ai_addr,rp->ai_addrlen);
    if (rc<0 && errno==EINPROGRESS) {
      FD_ZERO(&wfds);
      FD_SET(fd,&wfds);
      tv.tv_sec=(time_t)timeout;
      tv.tv_usec=0;
      rc=select(fd+1,NULL,&wfds,NULL,&tv);
      if (rc>0) {
        error=0;
        error_len=sizeof(error);
        if (getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&error_len)<0 || error!=0) rc=-1;
        else rc=0;
      } else rc=-1;
    }
    if (rc==0) {
      fcntl(fd,F_SETFL,flags);
      tv.tv_sec=(time_t)timeout;
      tv.tv_usec=0;
      setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
      setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
      break;
    }
    close(fd);
    fd=-1;
  }
  freeaddrinfo(res);
  return fd;
}

static int send_all(int fd,const char *buf,unsigned long len) {
  ssize_t n;
  unsigned long sent;

  sent=0;
  for (;sent<len;) {
    n=send(fd,buf+sent,len-sent,MSG_NOSIGNAL);
    if (n<0) {
      if (errno==EINTR) continue;
      return 0;
    }
    if (n==0) return 0;
    sent+=(unsigned long)n;
  }
  return 1;
}

static int stream_record(RadioStream *st,char *out,unsigned long cap,char term) {
  ssize_t n;
  unsigned long i,len,remain;

  for (;;) {
    for (i=st->start;i<st->end;i++) {
      if (st->buf[i]==term) {
        len=i-st->start+1;
        if (len>=cap) return 0;
        memcpy(out,st->buf+st->start,len);
        out[len]='\0';
        st->start=i+1;
        if (st->start==st->end) st->start=st->end=0;
        return 1;
      }
    }
    if (st->start>0) {
      remain=st->end-st->start;
      memmove(st->buf,st->buf+st->start,remain);
      st->start=0;
      st->end=remain;
    }
    if (st->end==sizeof(st->buf)) return 0;
    n=recv(st->fd,st->buf+st->end,sizeof(st->buf)-st->end,0);
    if (n<0) {
      if (errno==EINTR) continue;
      return 0;
    }
    if (n==0) return 0;
    st->end+=(unsigned long)n;
  }
}

static int recv_idle(int fd,char *buf,unsigned long cap,unsigned int idle_ms) {
  struct timeval tv;
  fd_set rfds;
  ssize_t n;
  unsigned long used;
  int rc,got;

  used=0;
  got=0;
  for (;;) {
    FD_ZERO(&rfds);
    FD_SET(fd,&rfds);
    tv.tv_sec=(time_t)(got?idle_ms/1000:2);
    tv.tv_usec=(suseconds_t)(got?(idle_ms%1000)*1000:0);
    rc=select(fd+1,&rfds,NULL,NULL,&tv);
    if (rc<0) {
      if (errno==EINTR) continue;
      return 0;
    }
    if (rc==0) break;
    if (used>=cap-1) return 0;
    n=recv(fd,buf+used,cap-1-used,0);
    if (n<0) {
      if (errno==EINTR) continue;
      return 0;
    }
    if (n==0) break;
    used+=(unsigned long)n;
    got=1;
  }
  buf[used]='\0';
  return got;
}

static int load_radio(MYSQL *con,const char *ota,char *radio,unsigned long rcap,char *udef1,unsigned long u1cap,char *udef2,unsigned long u2cap) {
  MYSQL_RES *res;
  MYSQL_ROW row;
  char escaped[257],query[QUERY_SIZE];
  unsigned long n;

  mysql_real_escape_string(con,escaped,ota,(unsigned long)strlen(ota));
  n=(unsigned long)snprintf(query,sizeof(query),"select radio,udef1,udef2 from user where ota='%s' and lastota+durationota>%lld limit 1",escaped,(long long)time(NULL));
  if (n>=sizeof(query) || mysql_query(con,query)!=0) return 0;
  res=mysql_store_result(con);
  if (res==NULL) return 0;
  row=mysql_fetch_row(res);
  if (row==NULL || row[0]==NULL || strlen(row[0])>=rcap) {
    mysql_free_result(res);
    return 0;
  }
  strcpy(radio,row[0]);
  udef1[0]=udef2[0]='\0';
  if (row[1]!=NULL) {
    strncpy(udef1,row[1],u1cap-1);
    udef1[u1cap-1]='\0';
  }
  if (row[2]!=NULL) {
    strncpy(udef2,row[2],u2cap-1);
    udef2[u2cap-1]='\0';
  }
  mysql_free_result(res);
  return 1;
}

static int mode_index(const char *mode) {
  int i;

  for (i=0;i<16;i++) if (strcmp(modets890s[i],mode)==0) return i;
  return -1;
}

static int ts890_query(RadioStream *st,const char *cmd,char *reply,unsigned long cap) {
  if (!send_all(st->fd,cmd,(unsigned long)strlen(cmd))) return 0;
  return stream_record(st,reply,cap,';');
}

static int handle_ts890(const char *host,unsigned int port,const char *user,const char *pass,char tok[][128],const char *udef1,const char *udef2) {
  RadioStream st;
  char cmd[CMD_SIZE],reply[RECORD_SIZE],*p;
  long freq;
  int fd,m,n;

  if (strlen(user)>99 || strlen(pass)>99) return 0;
  fd=connect_tcp(host,port,2);
  if (fd<0) return 0;
  memset(&st,0,sizeof(st));
  st.fd=fd;
  if (!ts890_query(&st,"##CN;",reply,sizeof(reply)) || strcmp(reply,"##CN1;")!=0) goto fail;
  n=snprintf(cmd,sizeof(cmd),"##ID0%02d%02d%s%s;",(int)strlen(user),(int)strlen(pass),user,pass);
  if (n<0 || (unsigned int)n>=sizeof(cmd)) goto fail;
  if (!ts890_query(&st,cmd,reply,sizeof(reply)) || strcmp(reply,"##ID1;")!=0) goto fail;

  if (tok[1][0]=='R') {
    if (!ts890_query(&st,"FA;",reply,sizeof(reply))) goto fail;
    p=strchr(reply,';');
    if (strncmp(reply,"FA",2)!=0 || p==NULL) goto fail;
    *p='\0';
    freq=strtol(reply+2,NULL,10);
    if (!ts890_query(&st,"OM0;",reply,sizeof(reply)) || strlen(reply)<5) goto fail;
    m=(reply[3]>='0' && reply[3]<='9')?reply[3]-'0':reply[3]-'A'+10;
    if (m<0 || m>=16) goto fail;
    printf("%ld,%s\n",freq,modets890s[m]);
  } else if (tok[1][0]=='S') {
    p=strchr(tok[2],':');
    if (p==NULL) goto fail;
    *p='\0';
    p++;
    freq=strtol(tok[2],NULL,10);
    if (freq<=0) goto fail;
    n=snprintf(cmd,sizeof(cmd),"FA%011ld;",freq);
    if (n<0 || (unsigned int)n>=sizeof(cmd) || !send_all(fd,cmd,(unsigned long)n)) goto fail;
    if (!ts890_query(&st,"FA;",reply,sizeof(reply))) goto fail;
    if (strncmp(reply,"FA",2)!=0) goto fail;
    reply[strcspn(reply,";")]='\0';
    freq=strtol(reply+2,NULL,10);
    if (*p!='\0') {
      m=mode_index(p);
      if (m>=0) {
        n=snprintf(cmd,sizeof(cmd),"OM0%c;",(m<10)?'0'+m:'A'+m-10);
        if (n<0 || (unsigned int)n>=sizeof(cmd) || !send_all(fd,cmd,(unsigned long)n)) goto fail;
      }
    }
    if (!ts890_query(&st,"OM0;",reply,sizeof(reply)) || strlen(reply)<5) goto fail;
    m=(reply[3]>='0' && reply[3]<='9')?reply[3]-'0':reply[3]-'A'+10;
    if (m<0 || m>=16) goto fail;
    printf("%ld,%s\n",freq,modets890s[m]);
  } else if (tok[1][0]=='U') {
    if (strcmp(tok[2],"1")==0) {
      if (!send_all(fd,udef1,(unsigned long)strlen(udef1))) goto fail;
    } else if (strcmp(tok[2],"2")==0) {
      if (!send_all(fd,udef2,(unsigned long)strlen(udef2))) goto fail;
    } else goto fail;
  } else goto fail;
  close(fd);
  return 1;

fail:
  close(fd);
  return 0;
}

static int handle_rigctld(const char *host,unsigned int port,char tok[][128]) {
  char buf[512],cmd[CMD_SIZE],*line[8],*p;
  long freq;
  int fd,n,count;

  fd=connect_tcp(host,port,2);
  if (fd<0) return 0;
  if (tok[1][0]=='R') {
    if (!send_all(fd,"sfim\n",5) || !recv_idle(fd,buf,sizeof(buf),200)) goto fail;
    count=0;
    p=strtok(buf,"\r\n");
    for (;p!=NULL && count<8;p=strtok(NULL,"\r\n")) line[count++]=p;
    if (count<5) goto fail;
    freq=strtol(line[2],NULL,10);
    if (freq<=0 || line[4][0]=='\0') goto fail;
    printf("%ld,%s\n",freq,line[4]);
  } else if (tok[1][0]=='S') {
    p=strchr(tok[2],':');
    if (p==NULL) goto fail;
    *p='\0';
    p++;
    freq=strtol(tok[2],NULL,10);
    if (freq<=0 || *p=='\0') goto fail;
    n=snprintf(cmd,sizeof(cmd),"F %ld\nM %s 0\n",freq,p);
    if (n<0 || (unsigned int)n>=sizeof(cmd) || !send_all(fd,cmd,(unsigned long)n)) goto fail;
    printf("%ld,%s\n",freq,p);
  } else goto fail;
  close(fd);
  return 1;

fail:
  close(fd);
  return 0;
}

int main(void) {
  QsozConfig cfg;
  MYSQL *con;
  char tok[3][128],radio[RADIO_SIZE],copy[RADIO_SIZE],udef1[UDEF_SIZE],udef2[UDEF_SIZE];
  char *type,*host,*sport,*user,*pass,*end,err[ERR_SIZE];
  unsigned long port;
  int ok;

  printf("Content-Type: text/plain\r\n\r\n");
  if (!read_request(tok,3)) {
    printf("0,ND\n");
    return 0;
  }
  if (!qsoz_config_load(&cfg,QSOZ_CONFIG_FILE,err,sizeof(err))) {
    fprintf(stderr,"pradio: %s\n",err);
    printf("0,ND\n");
    return 0;
  }
  con=mysql_init(NULL);
  if (con==NULL) {
    printf("0,ND\n");
    return 0;
  }
  if (mysql_real_connect(con,cfg.db_host,cfg.db_user,cfg.db_pass,cfg.db_name,cfg.db_port,NULL,0)==NULL) {
    fprintf(stderr,"pradio: mysql connect error: %s\n",mysql_error(con));
    mysql_close(con);
    printf("0,ND\n");
    return 0;
  }
  ok=load_radio(con,tok[0],radio,sizeof(radio),udef1,sizeof(udef1),udef2,sizeof(udef2));
  mysql_close(con);
  if (!ok) {
    printf("0,ND\n");
    return 0;
  }

  strcpy(copy,radio);
  type=strtok(copy,",");
  host=strtok(NULL,",");
  sport=strtok(NULL,",");
  if (type==NULL || host==NULL || sport==NULL) {
    printf("0,ND\n");
    return 0;
  }
  errno=0;
  port=strtoul(sport,&end,10);
  if (errno!=0 || end==sport || *end!='\0' || port<1 || port>65535) {
    printf("0,ND\n");
    return 0;
  }
  if (strcmp(type,"TS890S")==0) {
    user=strtok(NULL,",");
    pass=strtok(NULL,",");
    if (user==NULL || pass==NULL) ok=0;
    else ok=handle_ts890(host,(unsigned int)port,user,pass,tok,udef1,udef2);
  } else if (strcmp(type,"RIGCTLD")==0) {
    ok=handle_rigctld(host,(unsigned int)port,tok);
  } else ok=0;
  if (!ok) printf("0,ND\n");
  return 0;
}
