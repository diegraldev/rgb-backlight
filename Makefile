# SPDX-License-Identifier: GPL-2.0
#
# Kbuild para rgb-backlight.
#
ifneq ($(KERNELRELEASE),)

obj-m += rgb-backlight.o

else

# Compilacion manual:  make
#                   make KDIR=/lib/modules/$(uname -r)/build
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a

endif
