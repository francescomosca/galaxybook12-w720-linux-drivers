# Makefile for Samsung Galaxy Book 12 Ambient Light Sensor
KDIR ?= /lib/modules/$(KERNEL_VER)/build
ifeq ($(KERNEL_VER),)
KDIR := /lib/modules/$(shell uname -r)/build
endif

PWD := $(shell pwd)
CC ?= gcc
PKG_CFLAGS := $(shell pkg-config --cflags gio-2.0 gio-unix-2.0 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs gio-2.0 gio-unix-2.0 2>/dev/null) -lm

obj-m += bh1730_samsung.o

default: modules

all: modules daemon

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) LLVM=1 modules || $(MAKE) -C $(KDIR) M=$(PWD) modules

daemon: galaxybook-autobrightness.c
	$(CC) -O2 -Wall -Wextra galaxybook-autobrightness.c $(PKG_CFLAGS) $(PKG_LIBS) -o galaxybook-autobrightness

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean 2>/dev/null || rm -f *.o *.ko *.mod* .*.cmd modules.order Module.symvers
	rm -f galaxybook-autobrightness *.o

.PHONY: all default modules daemon clean
