# PIX-DT195 research driver (macOS arm64 / libusb)
#
# Requires libusb (arm64 Homebrew):  brew install libusb

CC      ?= cc
PKGCONFIG ?= pkg-config

CFLAGS  += -std=c11 -Wall -Wextra -O2 -g
CFLAGS  += $(shell $(PKGCONFIG) --cflags libusb-1.0 2>/dev/null)
LDLIBS  += $(shell $(PKGCONFIG) --libs libusb-1.0 2>/dev/null)

# Fallback if pkg-config can't find libusb (adjust prefix as needed).
ifeq ($(strip $(LDLIBS)),)
BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
CFLAGS  += -I$(BREW_PREFIX)/include/libusb-1.0
LDLIBS  += -L$(BREW_PREFIX)/lib -lusb-1.0
endif

OBJS = dt195.o dt195_i2c.o dt195_tune.o main.o

dt195: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

dt195.o:      dt195.c dt195.h dt195_i2c.h
dt195_i2c.o:  dt195_i2c.c dt195_i2c.h dt195.h
dt195_tune.o: dt195_tune.c dt195_i2c.h dt195.h
main.o:       main.c dt195.h

clean:
	rm -f dt195 $(OBJS)

.PHONY: clean
