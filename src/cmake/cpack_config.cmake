# A few CPack related variables
set(CPACK_PACKAGE_NAME "nfs-ganesha-${PACKNAME}" )
set(CPACK_PACKAGE_VENDOR "NFS-Ganesha Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NFS-Ganesha - A NFS Server runnning in user space")

# CPack's debian stuff
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "nfs-ganesha-devel@lists.sourceforge.net") 

set(CPACK_RPM_COMPONENT_INSTALL OFF)
set(CPACK_COMPONENTS_IGNORE_GROUPS "IGNORE")

# Tell CPack the kind of packages to be generated
set(CPACK_GENERATOR "TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ")

set(CPACK_SOURCE_IGNORE_FILES
  "/.git/;/.gitignore/;/build/;/.bzr/;~$;${CPACK_SOURCE_IGNORE_FILES}")


