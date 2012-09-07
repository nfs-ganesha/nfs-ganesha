OPTS="$*"
OPTS="$OPTS --enable-strict-compile"
OPTS="$OPTS --enable-debug-symbols"
OPTS="$OPTS --with-fsal=GPFS"
OPTS="$OPTS --enable-nlm"
OPTS="$OPTS --enable-stat-exporter"
OPTS="$OPTS --enable-snmp-adm"
OPTS="$OPTS --enable-fsal-up"
OPTS="$OPTS --with-nfs4-minorversion=0"
OPTS="$OPTS --enable-nfs4-acl"
OPTS="$OPTS --prefix=/usr"
./configure $OPTS
make rpm
