#!/bin/sh 

#if making code changes please run with these at minimum

CONFIG_ON="
	--enable-strict-compile --enable-dbus --enable-debug-symbols
"

./configure $CONFIG_ON

# ON in a list
# --enable-strict-compile
# --enable-dbus
# --enable-debug-symbols

# All options taken from ./configure --help
# TODO: I made these by hand I should write an awk script to do this
# automatically.
#
# --disable-libtool-lock
# --disable-largefile
# --enable-dbus
# --enable-upcall-simulato
# --enable-cb-simulator
# --disable-fsal-proxy
# --disable-fsal-vfs
# --disable-fsal-posix
# --disable-ceph
# --disable-lustre
# --disable-shook
# --enable-fsal-hpss
# --enable-fsal-fuse
# --enable-fsal-xfs
# --enable-fsal-gpfs
# --enable-fsal-zfs
# --enable-fsal-shook
# --enable-fsal-up
# --enable-ipv6
# --disable-nfsidmap
# --enable-snmp-adm
# --enable-error-injection
# --enable-stat-exporter
# --enable-efence
# --disable-tcp-register
# --disable-portmapper
# --disable-xattr-director
# --enable-debug-memleakse
# --enable-debug-nfsshell
# --enable-cache-path
# --enable-handle-mapping
# --enable-debug-symbols
# --enable-profiling
# --enable-strict-compile
# --enable-mspac
# --enable-9p
# --enable-9p-rdma
# --enable-strip-cs-uuid
