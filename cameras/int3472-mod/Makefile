obj-m += intel_skl_int3472_common.o
obj-m += intel_skl_int3472_discrete.o

intel_skl_int3472_common-objs := common.o
intel_skl_int3472_discrete-objs := discrete.o clk_and_regulator.o led.o discrete_quirks.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
