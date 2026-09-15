// Gianluca Mazzini @2022- Version 3.0
#ifndef QSOZ_NET_H
#define QSOZ_NET_H

#define QSOZ_NET_BUFFER 4096

typedef struct {
  int fd;
  char buffer[QSOZ_NET_BUFFER];
  unsigned long start;
  unsigned long end;
} QsozLineReader;

int qsoz_tcp_connect(const char *host,unsigned int port,unsigned int timeout);
int qsoz_send_all(int fd,const char *buf,unsigned long len);
void qsoz_line_reader_init(QsozLineReader *reader,int fd);
int qsoz_read_line(QsozLineReader *reader,char *out,unsigned long cap);

#endif
