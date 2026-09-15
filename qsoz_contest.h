// Gianluca Mazzini @2022- Version 4.3
#ifndef QSOZ_CONTEST_H
#define QSOZ_CONTEST_H

#include <mysql/mysql.h>

int qsoz_contest_details_print(MYSQL *con,const char *esc_mycall,const char *esc_contest);

#endif
