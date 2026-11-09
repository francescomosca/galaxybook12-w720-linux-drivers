KERNEL_VER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_VER)/build

KERNEL_CLANG := $(shell grep -q "^CONFIG_CC_IS_CLANG=y" $(KDIR)/.config 2>/dev/null && echo 1 || echo 0)
ifeq ($(KERNEL_CLANG),1)
    EXTRA_FLAGS := CC=clang LLVM=1
else
    EXTRA_FLAGS :=
endif

all:
	$(MAKE) -C imx258-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) $(EXTRA_FLAGS)
	$(MAKE) -C imx241-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) $(EXTRA_FLAGS)
	$(MAKE) -C dw9807-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) $(EXTRA_FLAGS)
	$(MAKE) -C int3472-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) $(EXTRA_FLAGS)
	$(MAKE) -C bridge-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) $(EXTRA_FLAGS)
	$(MAKE) -C cio2-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) $(EXTRA_FLAGS)

clean:
	$(MAKE) -C imx258-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) clean 2>/dev/null || true
	$(MAKE) -C imx241-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) clean 2>/dev/null || true
	$(MAKE) -C dw9807-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) clean 2>/dev/null || true
	$(MAKE) -C int3472-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) clean 2>/dev/null || true
	$(MAKE) -C bridge-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) clean 2>/dev/null || true
	$(MAKE) -C cio2-mod KERNEL_VER=$(KERNEL_VER) KDIR=$(KDIR) clean 2>/dev/null || true
