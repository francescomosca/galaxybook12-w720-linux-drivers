obj-m += st_accel_samsung.o

KERNEL_VER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_VER)/build

MODULE_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

# Auto-detect if the target kernel was built with Clang/LLVM
KERNEL_CLANG := $(shell grep -q "^CONFIG_CC_IS_CLANG=y" $(KDIR)/.config 2>/dev/null && echo 1 || echo 0)
ifeq ($(KERNEL_CLANG),1)
    EXTRA_FLAGS := CC=clang LLVM=1
else
    EXTRA_FLAGS :=
endif

all:
	$(MAKE) -C $(KDIR) M=$(MODULE_DIR) $(EXTRA_FLAGS) modules

clean:
	$(MAKE) -C $(KDIR) M=$(MODULE_DIR) clean 2>/dev/null || rm -f *.o *.ko *.mod* .*.cmd modules.order Module.symvers
