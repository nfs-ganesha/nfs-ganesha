# A few CPack related variables
set(CPACK_PACKAGE_NAME "nfs-ganesha")
set(CPACK_PACKAGE_VENDOR "NFS-Ganesha Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NFS-Ganesha - A NFS Server runnning in user space")
set(CPACK_PACKAGE_VERSION_MAJOR ${${PROJECT_NAME}_MAJOR_VERSION})
set(CPACK_PACKAGE_VERSION_MINOR ${${PROJECT_NAME}_MINOR_VERSION})
set(CPACK_PACKAGE_VERSION_PATCH ${${PROJECT_NAME}_PATCH_LEVEL})

# CPack's debian stuff
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "nfs-ganesha-devel@lists.sourceforge.net") 

# CPack's RPM's stuff
set( CPACK_RPM_PACKAGE_SUMMARY "NFS Server running in user space" )
set( CPACK_RPM_PACKAGE_NAME ${CPACK_PACKAGE_NAME} )
set( CPACK_RPM_PACKAGE_VERSION ${${PROJECT_NAME}_MAJOR_VERSION}.${${PROJECT_NAME}_MINOR_VERSION}.${${PROJECT_NAME}_PATCH_LEVEL})
set( CPACK_RPM_PACKAGE_RELEASE "1" )
set( CPACK_RPM_PACKAGE_LICENSE "LGPLv3" )
set( CPACK_RPM_PACKAGE_GROUP "Applications/System" )
set( CPACK_RPM_PACKAGE_VENDOR "NFS-Ganesha Project")
set( CPACK_RPM_PACKAGE_DESCRIPTION 
"NFS-Ganesha is a NFS Server running in user space with a large cache.
It comes with various back-end modules to support different file systems and
name-spaces." )
set( CPACK_RPM_CHANGELOG_FILE ${PROJECT_SOURCE_DIR}/rpm_changelog )

set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_IGNORE_GROUPS "IGNORE")

# Tell CPack the kind of packages to be generated
set(CPACK_GENERATOR "STGZ;TGZ;TZ;DEB;RPM")
set(CPACK_SOURCE_GENERATOR "TGZ;TBZ2;TZ;DEB;RPM")

