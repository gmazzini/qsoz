# Gianluca Mazzini @2022- Version 4.2
CC=cc
CFLAGS=-O3 -std=gnu89 -Wall -Wextra -Wdeclaration-after-statement -Wformat=2 -Wformat-overflow=2 -Wformat-truncation=2 -Wstringop-overflow=2
MARIADB_CFLAGS=$(shell mariadb_config --cflags)
MARIADB_LIBS=$(shell mariadb_config --libs)
DATA_DIR=/home/tools/mcp/work/data

SOURCES=qsoz.c qsoz_frontend.c qsoz_cmd.c qsoz_completion.c qsoz_cty.c qsoz_ft8.c qsoz_guess.c qsoz_login.c qsoz_proc.c qsoz_radio.c qsoz_clock.c qsoz_score.c qsoz_contest.c qsoz_config.c qsoz_db.c qsoz_html.c qsoz_net.c qsoz_request.c qsoz_stats.c qsoz_time.c qsoz_util.c
OBJECTS=$(SOURCES:.c=.o)

all: qsoz

qsoz: $(OBJECTS) $(DATA_DIR)/libradio_data.a $(DATA_DIR)/libradio_client.a
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(DATA_DIR)/libradio_data.a $(DATA_DIR)/libradio_client.a $(MARIADB_LIBS) -lsodium -lcurl -lzip -lm

%.o: %.c
	$(CC) $(CFLAGS) $(MARIADB_CFLAGS) -c $< -o $@

clean:
	rm -f *.o qsoz

.PHONY: all clean
