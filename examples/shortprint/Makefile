# Comment/uncomment the following line to disable/enable debugging
#DEBUG = y

CFLAGS += -O2 -I..

ifneq ($(KERNELRELEASE),)
# call from kernel build system

obj-m	:= shortprint.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

endif

