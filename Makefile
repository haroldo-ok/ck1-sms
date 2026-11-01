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

include gen/banks.mk

# release builds let SDCC search register allocations much harder (several
# minutes for main.c, ~10% faster code); `make OPT=` for quick dev builds
OPT      ?= --max-allocs-per-node 100000
CFLAGS   := -mz80 $(OPT) --peep-file $(SMSLIB)/peep-rules.txt -I$(SMSLIB) -Igen
LDFLAGS  := -mz80 -Wl-u --no-std-crt0 --data-loc 0xC000 $(BANKFLAGS)

all: $(PROG).sms

assets:
	python3 tools/convert_ck1.py

build/main.rel: src/main.c gen/game_data.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/game_data.rel: gen/game_data.c gen/game_data.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(addprefix build/bank,$(addsuffix .rel,$(DATABANKS))): build/bank%.rel: gen/bank%.c
	@mkdir -p build
	$(CC) -mz80 --constseg BANK$* -c $< -o $@

# code overlays: compiled into their own slot-2 bank (see src/engine.h)
build/ovl_%.rel: src/ovl_%.c src/engine.h gen/game_data.h
	@mkdir -p build
	$(CC) $(CFLAGS) --codeseg BANK$(OVL_$*) --constseg BANK$(OVL_$*) -c $< -o $@

build/ai.rel: src/ai.c src/shared.h gen/game_data.h
	@mkdir -p build
	$(CC) $(CFLAGS) --codeseg BANK25 -c $< -o $@

build/font_data.rel: gen/font_data.c
	@mkdir -p build
	$(CC) -mz80 --constseg BANK$(TXTBANK) -c $< -o $@

$(PROG).ihx: build/main.rel build/game_data.rel build/font_data.rel $(BANKRELS)
	@touch crt0_sms.lst   # -Wl-u (relocated listings for perf/) wants one per object
	$(CC) -o $@ $(LDFLAGS) $(CRT0) build/main.rel build/game_data.rel $(SMSLIB)/SMSlib.lib $(BANKRELS) build/font_data.rel

$(PROG).sms: $(PROG).ihx
	$(IHX2SMS) $< $@

clean:
	rm -rf build $(PROG).ihx $(PROG).sms $(PROG).map $(PROG).noi $(PROG).lk

.PHONY: all assets clean
