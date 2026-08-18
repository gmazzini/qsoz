// Gianluca Mazzini @2022- Version 3.0
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include "qsoz_net.h"

int qsoz_tcp_connect(const char *host,unsigned int port,unsigned int timeout) {
  struct addrinfo hints,*res,*rp;
  struct timeval tv;
  fd_set wfds;
  char service[16];
  int fd,flags,rc,error;
  socklen_t error_len;

  if (host==NULL || *host=='\0' || port==0 || timeout==0) return -1;
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
      setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
      setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
      break;
    }
    close(fd);
    fd=-1;
  }
  freeaddrinfo(res);
  return fd;
}

int qsoz_send_all(int fd,const char *buf,unsigned long len) {
  ssize_t n;
  unsigned long sent;

  if (fd<0 || buf==NULL) return 0;
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

void qsoz_line_reader_init(QsozLineReader *reader,int fd) {
  if (reader==NULL) return;
  reader->fd=fd;
  reader->start=0;
  reader->end=0;
}

int qsoz_read_line(QsozLineReader *reader,char *out,unsigned long cap) {
  ssize_t n;
  unsigned long i,len,remain;

  if (reader==NULL || out==NULL || cap<2 || reader->fd<0) return -1;
  for (;;) {
    for (i=reader->start;i<reader->end;i++) {
      if (reader->buffer[i]=='\n') {
        len=i-reader->start;
        if (len>0 && reader->buffer[reader->start+len-1]=='\r') len--;
        if (len>=cap) return -1;
        memcpy(out,reader->buffer+reader->start,len);
        out[len]='\0';
        reader->start=i+1;
        if (reader->start==reader->end) reader->start=reader->end=0;
        return 1;
      }
    }
    if (reader->start>0) {
      remain=reader->end-reader->start;
      memmove(reader->buffer,reader->buffer+reader->start,remain);
      reader->start=0;
      reader->end=remain;
    }
    if (reader->end==sizeof(reader->buffer)) return -1;
    n=recv(reader->fd,reader->buffer+reader->end,sizeof(reader->buffer)-reader->end,0);
    if (n<0) {
      if (errno==EINTR) continue;
      return -1;
    }
    if (n==0) {
      len=reader->end-reader->start;
      if (len==0) return 0;
      if (len>0 && reader->buffer[reader->start+len-1]=='\r') len--;
      if (len>=cap) return -1;
      memcpy(out,reader->buffer+reader->start,len);
      out[len]='\0';
      reader->start=reader->end=0;
      return 1;
    }
    reader->end+=(unsigned long)n;
  }
}
