dir:=$(shell uname -r | cut -d- -f1)

all:
	$(MAKE) -C $(dir) all
	$(MAKE) -C tests all

distdir:

check:

install:
	$(MAKE) -C $(dir) install

clean:
	$(MAKE) -C $(dir) clean
	$(MAKE) -C tests clean
