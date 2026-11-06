# Makefile for Samsung Galaxy Book 12 Ambient Light Sensor Kernel Module
KDIR ?= /lib/modules/$(KERNEL_VER)/build
ifeq ($(KERNEL_VER),)
KDIR := /lib/modules/$(shell uname -r)/build
endif

PWD := $(shell pwd)

obj-m += bh1730_samsung.o

default: modules

all: modules daemon

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) LLVM=1 modules || $(MAKE) -C $(KDIR) M=$(PWD) modules

daemon:
	$(MAKE) -C autobrightness

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean 2>/dev/null || rm -f *.o *.ko *.mod* .*.cmd modules.order Module.symvers
	$(MAKE) -C autobrightness clean 2>/dev/null || true
	rm -f *.o

.PHONY: all default modules daemon clean
