# Makefile for Samsung Galaxy Book 12 Ambient Light Sensor Kernel Module
KERNEL_VER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_VER)/build

PWD := $(shell pwd)

KERNEL_CLANG := $(shell grep -q "^CONFIG_CC_IS_CLANG=y" $(KDIR)/.config 2>/dev/null && echo 1 || echo 0)
ifeq ($(KERNEL_CLANG),1)
    EXTRA_FLAGS := CC=clang LLVM=1
else
    EXTRA_FLAGS :=
endif

obj-m += bh1730_samsung.o

default: modules

all: modules daemon

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) $(EXTRA_FLAGS) modules

daemon:
	$(MAKE) -C autobrightness

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean 2>/dev/null || rm -f *.o *.ko *.mod* .*.cmd modules.order Module.symvers
	$(MAKE) -C autobrightness clean 2>/dev/null || true
	rm -f *.o

.PHONY: all default modules daemon clean
