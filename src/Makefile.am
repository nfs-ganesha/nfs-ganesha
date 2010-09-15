ACLOCAL_AMFLAGS = -I m4

SUBDIRS = include             \
          Log 		      \
          BuddyMalloc         \
          Common              \
          ConfigParsing       \
          RW_Lock             \
          SemN                \
          test                \
          LRU                 \
          HashTable           \
	  NodeList	      \
          FSAL                \
          MFSL                \
          Cache_inode         \
          File_Content        \
          File_Content_Policy \
          XDR                 \
          NFS_Protocols       \
          support             \
          IdMapper            \
          snmp_adm            \
          MainNFSD            \
          cmdline_tools       \
          shell               \
          cmdline_tools       \
          Docs                \
          tools               \
          config_samples      \
          check               \
	  debian              \
          rpm

if USE_FSAL_FUSE
SUBDIRS += example-fuse
endif

DOCDIRS = Cache_inode ConfigParsing File_Content File_Content_Policy HashTable IdMapper LRU Log MainNFSD NFS_Protocols SemN StuffAlloc shell support \
          FSAL/FSAL_HPSS FSAL/FSAL_POSIX FSAL/FSAL_PROXY FSAL/FSAL_SNMP FSAL/FSAL_FUSE FSAL/FSAL_ZFS \
          MFSL/MFSL_NULL MFSL/MFSL_ASYNC MFSL/MFSL_PROXY_RPCSECGSS

new: clean all

deb:
	dpkg-buildpackage -rfakeroot -d

rpms: rpm srpm

codecheck:
	 cppcheck -v --force --enable=all -I`pwd`/include . 2>cppcheck.log

cscope:
	find . -type f -name "*.[ch]" | grep -v rpm/ > cscope.namefile
	cscope -i cscope.namefile

doc:
	curdir=`pwd` ; for i in $(DOCDIRS) ; do cd $$i ; make doc ; cd $$curdir ; done

# Remove HPSS specifc (and non OpenSource) code from distribution
remove_hpss: dist 
	tar xzf $(distdir).tar.gz
	mv $(distdir).tar.gz $(distdir).full.tar.gz 
	for i in `find $(distdir) -name FSAL_HPSS ` ; do \
		 find $$i -type f -name "*.[ch]" -exec cp /dev/null {} \; ; done
	tar cfz $(distdir).tar.gz $(distdir)
	rm -fr $(distdir)
	mkdir with-hpss without-hpss 
	cd without-hpss ; tar xzf ../$(distdir).tar.gz 
	cd with-hpss ; tar xzf ../$(distdir).full.tar.gz 
	diff -urp without-hpss/$(distdir) with-hpss/$(distdir) > patch-for-HPSS-$(distdir) ; echo "Patch done"
	rm -fr with-hpss without-hpss 

remove_hpss_bzip2: dist-bzip2
	tar xjf $(distdir).tar.bz2
	mv $(distdir).tar.bz2 $(distdir).full.tar.bz2 
	for i in `find $(distdir) -name FSAL_HPSS ` ; do \
		find $$i -type f -name "*.[ch]" -exec cp /dev/null {} \; ; done
	tar cjf $(distdir).tar.bz2 $(distdir)
	rm -fr $(distdir)

if USE_PGSQL
RPM_DB_ARGS= --with postgresql
else
RPM_DB_ARGS= --without postgresql
endif

#indent:
#	find . -name '*.[ch]' ! -path './rpm/*' -exec indent -linux -nsaf -nsai -nsaw -nut -bl -bls -i4 -l90 -lc90 -nce -hnl {} \;

rpm/SPECS rpm/SOURCES rpm/RPMS rpm/BUILD rpm/SRPMS:
	mkdir -p $@

rpmdirs: rpm/SPECS rpm/SOURCES rpm/RPMS rpm/BUILD rpm/SRPMS

if USE_FSAL_HPSS
rpm: nfs-ganesha.spec dist rpmdirs
	rpmbuild $(extra_arg) --define "_topdir `pwd`/rpm" -tb $(distdir).tar.gz

srpm: nfs-ganesha.spec dist rpmdirs
	rpmbuild $(extra_arg) --define "_topdir `pwd`/rpm" -ts $(distdir).tar.gz

else  
rpm: nfs-ganesha.spec remove_hpss rpmdirs
	rpmbuild  $(RPM_DB_ARGS) $(extra_arg) --define "_topdir `pwd`/rpm" -tb $(distdir).tar.gz

srpm: nfs-ganesha.spec remove_hpss rpmdirs
	rpmbuild $(RPM_DB_ARGS) $(extra_arg) --define "_topdir `pwd`/rpm" -ts $(distdir).tar.gz

endif

EXTRA_DIST=nfs-ganesha.spec           \
           COMPILING_HOWTO.txt        \
           LICENSE.txt                \
           ganesha.vim                \
           ganesha_au.vim             \
           ChangeLog                  \
           TODO                       \
           THANKS                     \
           KNOWN_BUGS                 \
           nfs-ganesha.posix.init     \
           nfs-ganesha.lustre.init    \
           nfs-ganesha.xfs.init       \
           nfs-ganesha.gpfs.init      \
           nfs-ganesha.hpss.init      \
           nfs-ganesha.proxy.init     \
           nfs-ganesha.snmp.init      \
           nfs-ganesha.zfs.init       \
           nfs-ganesha.dynfsal.init

pkgconfigdir=$(libdir)/pkgconfig
if USE_FSAL_FUSE
pkgconfig_DATA = libganeshaNFS.pc
endif

