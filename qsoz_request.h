// Gianluca Mazzini @2022- Version 3.0
#ifndef QSOZ_REQUEST_H
#define QSOZ_REQUEST_H

#define QSOZ_REQUEST_FIELDS 13
#define QSOZ_REQUEST_FIELD_SIZE 100
#define QSOZ_REQUEST_MAX_PAYLOAD 20000000UL

int qsoz_request_read(char fields[][QSOZ_REQUEST_FIELD_SIZE],int field_count,char **payload,unsigned long *payload_len,char *err,unsigned long errcap);

#endif
