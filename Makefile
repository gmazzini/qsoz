# Gianluca Mazzini @2022- Version 4.13
CC=cc
CFLAGS=-O3 -std=gnu89 -Wall -Wextra -Wdeclaration-after-statement -Wformat=2 -Wformat-overflow=2 -Wformat-truncation=2 -Wstringop-overflow=2
DATA_DIR=/home/tools/mcp/work/data

SOURCES=qsoz.c qsoz_frontend.c qsoz_cmd.c qsoz_completion.c qsoz_cty.c qsoz_ft8.c qsoz_guess.c qsoz_login.c qsoz_user.c qsoz_cache.c qsoz_proc.c qsoz_radio.c qsoz_clock.c qsoz_score.c qsoz_contest.c qsoz_config.c qsoz_db.c qsoz_html.c qsoz_net.c qsoz_request.c qsoz_stats.c qsoz_time.c qsoz_util.c
OBJECTS=$(SOURCES:.c=.o)

RADIO_DATA_H=$(DATA_DIR)/radio_data.h


all: qsoz

qsoz_proc.o qsoz_score.o qsoz_contest.o qsoz_cty.o qsoz_ft8.o: $(RADIO_DATA_H)

qsoz.o qsoz_clock.o: qsoz_version.h

qsoz: $(OBJECTS) $(DATA_DIR)/libradio_data.a $(DATA_DIR)/libradio_client.a
	$(CC) $(CFLAGS) -o qsoz.new $(OBJECTS) $(DATA_DIR)/libradio_data.a $(DATA_DIR)/libradio_client.a -lsqlite3 -lsodium -lcurl -lzip -lm
	mv -f qsoz.new qsoz

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o qsoz.new

distclean: clean
	rm -f qsoz

.PHONY: all clean distclean
