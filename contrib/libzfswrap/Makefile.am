ACLOCAL_AMFLAGS = -I m4

SUBDIRS = lib zfswrap

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libzfswrap.pc

EXTRA_DIST = debian \
             libzfswrap.spec \
             README

doc:
	curdir=`pwd` ; cd zfswrap ; make doc ; cd $$curdir

deb: distdir
	tar cvzf libzfswrap_@PACKAGE_VERSION@.orig.tar.gz libzfswrap-@PACKAGE_VERSION@ ; \
	cd libzfswrap-@PACKAGE_VERSION@ ; \
	dpkg-buildpackage ; \
	cd ../

rpms: rpm srpm

rpm: dist-bzip2
	rpmbuild --define "_topdir `pwd`/rpm" -tb $(distdir).tar.bz2

srpm: dist-bzip2
	rpmbuild --define "_topdir `pwd`/rpm" -ts $(distdir).tar.bz2
