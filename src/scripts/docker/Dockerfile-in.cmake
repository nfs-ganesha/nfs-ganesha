# CEPH BASE IMAGE
# CEPH VERSION: Hammer
# CEPH VERSION DETAIL: 0.94.x

FROM @DOCKER_DISTRO@:@DOCKER_DISTRO_VERSION@
MAINTAINER Daniel Gryniewicz "dang@redhat.com"

#ENV ETCDCTL_VERSION v2.2.0
#ENV ETCDCTL_ARCH linux-amd64
#ENV CEPH_VERSION 10.0.3-2378.gd3db533

# Install prerequisites
RUN dnf install -y tar redhat-lsb-core

# Install deps
RUN dnf install -y libcap libblkid libuuid dbus nfs-utils rpcbind libnfsidmap libattr

#install ganesha
ADD root/ /
RUN mkdir -p @CMAKE_INSTALL_PREFIX@/var/log/ganesha

ADD entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
