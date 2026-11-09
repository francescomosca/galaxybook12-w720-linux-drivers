obj-m := dw9807-vcm.o
KERNEL_VER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_VER)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean 2>/dev/null || rm -f *.o *.ko *.mod* .*.cmd modules.order Module.symvers

