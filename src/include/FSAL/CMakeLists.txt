add_subdirectory(FSAL_FUSELIKE)
add_subdirectory(FSAL_GPFS)
add_subdirectory(FSAL_HPSS)
add_subdirectory(FSAL_LUSTRE)
add_subdirectory(FSAL_POSIX)
add_subdirectory(FSAL_TEMPLATE)
add_subdirectory(FSAL_ZFS)


########### install files ###############




#original Makefile.am contents follow:

#noinst_HEADERS = access_check.h \
#                 fsal_commonlib.h \
#                 fsal_init.h \
#                 common_methods.h \
#                 fsal_config.h 
#
#SUBDIRS = FSAL_FUSELIKE FSAL_GPFS FSAL_HPSS FSAL_LUSTRE FSAL_POSIX FSAL_TEMPLATE FSAL_XFS FSAL_ZFS
