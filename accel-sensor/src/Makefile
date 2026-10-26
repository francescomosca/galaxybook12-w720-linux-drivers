obj-m += st_accel_samsung.o

KERNEL_VER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_VER)/build

MODULE_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

# Support LLVM/Clang if specified or if LLVM is requested
LLVM ?= $(if $(shell which clang 2>/dev/null),1,0)
ifeq ($(LLVM),1)
    EXTRA_FLAGS := LLVM=1
else
    EXTRA_FLAGS :=
endif

all:
	$(MAKE) -C $(KDIR) M=$(MODULE_DIR) $(EXTRA_FLAGS) modules

clean:
	$(MAKE) -C $(KDIR) M=$(MODULE_DIR) clean
