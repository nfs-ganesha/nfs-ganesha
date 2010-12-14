noinst_LTLIBRARIES = libsolcompat.la
libsolcompat_la_SOURCES = getmntany.c mkdirp.c strlcpy.c strlcat.c u8_textprep.c zone.c

AM_CFLAGS = -I${top_srcdir}/lib/libsolcompat/include \
            -I${top_srcdir}/lib/libatomic/include/@ARCH@ \
            @DEBUG_CFLAGS@
AM_CCASFLAGS = $(AM_CFLAGS)

noinst_HEADERS = gen_synonyms.h \
                 include/aclutils.h \
                 include/atomic.h \
                 include/devid.h \
                 include/libc.h \
                 include/libdevinfo.h \
                 include/libdiskmgt.h \
                 include/libgen.h \
                 include/libshare.h \
                 include/mtlib.h \
                 include/priv.h \
                 include/stdarg.h \
                 include/stdio_ext.h \
                 include/string.h \
                 include/strings.h \
                 include/synch.h \
                 include/thread.h \
                 include/ucred.h \
                 include/unistd_aux.h \
                 include/unistd.h \
                 include/zone.h \
                 include/rpc/xdr.h \
                 include/sys/acl.h \
                 include/sys/acl_impl.h \
                 include/sys/bitmap.h \
                 include/sys/byteorder.h \
                 include/sys/callb.h \
                 include/sys/cmn_err.h \
                 include/sys/cred.h \
                 include/sys/ddi.h \
                 include/sys/debug.h \
                 include/sys/dkio.h \
                 include/sys/dklabel.h \
                 include/sys/efi_partition.h \
                 include/sys/feature_tests.h \
                 include/sys/file.h \
                 include/sys/fm/protocol.h \
                 include/sys/fm/util.h \
                 include/sys/idmap.h \
                 include/sys/int_limits.h \
                 include/sys/int_types.h \
                 include/sys/inttypes.h \
                 include/sys/ioctl.h \
                 include/sys/isa_defs.h \
                 include/sys/kmem.h \
                 include/sys/kstat.h \
                 include/sys/mkdev.h \
                 include/sys/mntent.h \
                 include/sys/mntio.h \
                 include/sys/mnttab.h \
                 include/sys/modctl.h \
                 include/sys/mount.h \
                 include/sys/note.h \
                 include/sys/param.h \
                 include/sys/priv.h \
                 include/sys/processor.h \
                 include/sys/proc.h \
                 include/sys/rctl.h \
                 include/sys/sdt.h \
                 include/sys/stack.h \
                 include/sys/stat.h \
                 include/sys/stropts.h \
                 include/sys/sunddi.h \
                 include/sys/sysevent/eventdefs.h \
                 include/sys/sysevent.h \
                 include/sys/sysmacros.h \
                 include/sys/systeminfo.h \
                 include/sys/systm.h \
                 include/sys/time.h \
                 include/sys/trap.h \
                 include/sys/types32.h \
                 include/sys/types.h \
                 include/sys/u8_textprep_data.h \
                 include/sys/u8_textprep.h \
                 include/sys/uio.h \
                 include/sys/user.h \
                 include/sys/utsname.h \
                 include/sys/uuid.h \
                 include/sys/va_list.h \
                 include/sys/varargs.h \
                 include/sys/vmem.h \
                 include/sys/vtoc.h \
                 include/sys/zmod.h \
                 include/sys/zone.h \
                 include/tsol/label.h

