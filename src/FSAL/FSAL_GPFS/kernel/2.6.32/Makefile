obj-m += open-by-handle.o
open-by-handle-objs := open_by_handle.o handle_ioctl.o

all: modules

install: all
	install -d $(DESTDIR)/lib/modules/$(shell uname -r)/extra
	$(MAKE) -C /lib/modules/$(shell uname -r)/build/ INSTALL_MOD_PATH=$(DESTDIR)/ M=$(shell pwd) modules_install
# needed on a new install
	/sbin/depmod -b $(DESTDIR)/

clean:
	$(MAKE) -C $(DESTDIR)/lib/modules/$(shell uname -r)/build/ M=$(shell pwd) clean

modules:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd)
