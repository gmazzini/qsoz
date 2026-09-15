// Gianluca Mazzini @2022- Version 3.0
#ifndef QSOZ_CONFIG_H
#define QSOZ_CONFIG_H

#define QSOZ_CONFIG_FILE "/home/tools/mcp/work/qsoz/qsoz.conf"
#define QSOZ_CFG_VALUE 256

typedef struct {
  char db_host[QSOZ_CFG_VALUE];
  char db_user[QSOZ_CFG_VALUE];
  char db_pass[QSOZ_CFG_VALUE];
  char db_name[QSOZ_CFG_VALUE];
  unsigned int db_port;
  char callbook_host[QSOZ_CFG_VALUE];
  unsigned int callbook_port;
  unsigned int callbook_timeout;
  char cluster_host[QSOZ_CFG_VALUE];
  unsigned int cluster_port;
  unsigned int cluster_timeout;
} QsozConfig;

int qsoz_config_load(QsozConfig *cfg,const char *path,char *err,unsigned long errcap);

#endif
