obj-m += hc_sr04.o
COPS = -Wall -O2  -std=gnu99 -lc -lm -lg -lgcc
PWD := $(shell pwd)
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
include $(KERNELDIR)/.config


EXTRA_CFLAGS = -D__KERNEL__ -DMODULE -I$(KERNELDIR)/include \
   -O -Wall
	
all:
	make -C $(KERNELDIR) M=$(PWD) modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean

install:	
	make -C $(KERNELDIR) M=$(PWD) modules_install
	cp 83-hc_sr04.rules /usr/lib/udev/rules.d 
	depmod

quickInstall:
	cp $(MODULES) /lib/modules/`uname -r`/extra
