#
# Makefile for Linux samplefs
#
obj-m += open-by-handle.o
open-by-handle-objs := open_by_handle.o handle_ioctl.o

ifeq ($(shell uname -r | sed 's/-.*//'), 2.6.18) 

all: modules userspace

install:
	$(MAKE) -C $(DESTDIR)/lib/modules/$(shell uname -r)/build/ M=$(shell pwd) modules_install
# needed on a new install
	/sbin/depmod

clean: userclean
	$(MAKE) -C $(DESTDIR)/lib/modules/$(shell uname -r)/build/ M=$(shell pwd) clean

else
all: error
install: error
clean: error

error:
	@echo kernel module only works on 2.6.18 kernels

endif

userspace: append_handle_prg create_handle_prg hardlink_handle_prg \
	 read_handle_prg readlink_handle_prg \
	 truncate_handle_prg

modules:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd)

userclean:
	$(RM) append_handle_prg create_handle_prg hardlink_handle_prg \
	 read_handle_prg readlink_handle_prg \
	 truncate_handle_prg

%: %.c
	$(CC) -o $@ $^
