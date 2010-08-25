#
# Makefile for Linux samplefs
#
obj-m += open-by-handle.o
open-by-handle-objs := open_by_handle.o handle_ioctl.o

ifeq ($(shell uname -r | sed 's/-.*//'), 2.6.18) 

all: modules userspace

install:
	install -d $(DESTDIR)/lib/modules/$(shell uname -r)/extra
	$(MAKE) -C /lib/modules/$(shell uname -r)/build/ INSTALL_MOD_PATH=$(DESTDIR)/ M=$(shell pwd) modules_install
# needed on a new install
	/sbin/depmod -b $(DESTDIR)/

clean: userclean
	$(MAKE) -C $(DESTDIR)/lib/modules/$(shell uname -r)/build/ M=$(shell pwd) clean

# This should be a real test 
check: userspace


else
all: error
install: error
clean: error
check: error

error:
	@echo kernel module only works on 2.6.18 kernels

endif

userspace: append_handle_prg create_handle_prg hardlink_handle_prg \
	 read_handle_prg readlink_handle_prg \
	 truncate_handle_prg create_handle_from_fd_prg

modules:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd)

userclean:
	$(RM) append_handle_prg create_handle_prg hardlink_handle_prg \
	 read_handle_prg readlink_handle_prg \
	 truncate_handle_prg create_handle_from_fd_prg

distdir:

distclean: clean

%: %.c
	$(CC) -o $@ $^
