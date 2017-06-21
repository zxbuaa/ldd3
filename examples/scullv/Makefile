
# Comment/uncomment the following line to enable/disable debugging
#DEBUG = y


ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DSCULLV_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

CFLAGS += $(DEBFLAGS) -I$(LDDINC)

TARGET = scullv

ifneq ($(KERNELRELEASE),)

scullv-objs := main.o mmap.o

obj-m	:= scullv.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(PWD) modules
install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(PWD) modules_install
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(PWD) clean

endif

