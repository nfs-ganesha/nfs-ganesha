set( RPM_NAME ${PROJECT_NAME} )
set( PACKAGE_VERSION "${${PROJECT_NAME}_MAJOR_VERSION}.${${PROJECT_NAME}_MINOR_VERSION}.${${PROJECT_NAME}_PATCH_LEVEL}" ) 
set( RPM_SUMMARY "NFS-Ganesha is a NFS Server running in user space" )
set( RPM_RELEASE_BASE 1 )
set( RPM_RELEASE ${RPM_RELEASE_BASE}.git${_GIT_HEAD_COMMIT_ABBREV} )
set( RPM_PACKAGE_LICENSE "LGPLv3" )
set( RPM_PACKAGE_GROUP "Applications/System" )
set( RPM_URL "http://nfs-ganesha.sourceforge.net" )
set( RPM_CHANGELOG_FILE ${PROJECT_SOURCE_DIR}/rpm_changelog )

set( RPM_DESCRIPTION 
"NFS-GANESHA is a NFS Server running in user space.
It comes with various back-end modules (called FSALs) provided as shared objects to support different file systems and
name-spaces." )

