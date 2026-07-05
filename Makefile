# ─────────────────────────────────────────────────────────────────────────
# Commander Keen SMS port — devkitSMS / SDCC build
#
#   make assets   regenerate gen/ from the HTML5-Keen data (needs python3+PIL)
#   make          build keen.sms
#   make clean
#
# ROM layout: banks 0-1 = code + fixed data (32 KiB), banks 2-8 = assets
# ─────────────────────────────────────────────────────────────────────────
CC       := sdcc
IHX2SMS  := ./ihx2sms

SMSLIB   := SMSlib
CRT0     := crt0_sms.rel
PROG     := keen

BANKS    := 2 3 4 5 6 7 8
BANKRELS := $(addprefix build/bank,$(addsuffix .rel,$(BANKS)))

CFLAGS   := -mz80 --peep-file $(SMSLIB)/peep-rules.txt -I$(SMSLIB) -Igen
LDFLAGS  := -mz80 --no-std-crt0 --data-loc 0xC000 \
            -Wl-b_BANK2=0x28000 -Wl-b_BANK3=0x38000 -Wl-b_BANK4=0x48000 \
            -Wl-b_BANK5=0x58000 -Wl-b_BANK6=0x68000 -Wl-b_BANK7=0x78000 \
            -Wl-b_BANK8=0x88000

all: $(PROG).sms

assets:
	python3 tools/build_assets.py

build/main.rel: src/main.c gen/game_data.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/game_data.rel: gen/game_data.c gen/game_data.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/bank%.rel: gen/bank%.c
	@mkdir -p build
	$(CC) -mz80 --constseg BANK$* -c $< -o $@

$(PROG).ihx: build/main.rel build/game_data.rel $(BANKRELS)
	$(CC) -o $@ $(LDFLAGS) $(CRT0) build/main.rel build/game_data.rel $(SMSLIB)/SMSlib.lib $(BANKRELS)

$(PROG).sms: $(PROG).ihx
	$(IHX2SMS) $< $@

clean:
	rm -rf build $(PROG).ihx $(PROG).sms $(PROG).map $(PROG).noi $(PROG).lk

.PHONY: all assets clean
