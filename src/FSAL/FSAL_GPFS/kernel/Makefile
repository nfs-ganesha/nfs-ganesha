dir:=$(shell uname -r | cut -d- -f1)

# this is required so we can still test the rest of the build on non RHEL platforms
direxists:=$(shell test -d $(dir) && echo 1)

ifeq ($(direxists),1) 
all:
	$(MAKE) -C $(dir) all
	$(MAKE) -C tests all

install:
	$(MAKE) -C $(dir) install

clean:
	$(MAKE) -C $(dir) clean
	$(MAKE) -C tests clean
else

all:
install:
clean:

endif

distclean: clean

.PHONY: distclean distdir check
