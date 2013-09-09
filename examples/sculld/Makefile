
# Comment/uncomment the following line to enable/disable debugging
#DEBUG = y


ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DSCULLD_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

ccflag-y += $(DEBFLAGS) -I$(LDDINC)

TARGET = sculld

ifneq ($(KERNELRELEASE),)

sculld-objs := main.o mmap.o

obj-m	:= sculld.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	cp ../lddbus/Module.symvers .
	$(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(PWD) modules
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(PWD) clean

endif


install:
	install -d $(INSTALLDIR)
	install -c $(TARGET).o $(INSTALLDIR)



depend .depend dep:
	$(CC) $(ccflag-y) -M *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif
