
noinst_LTLIBRARIES            = libnodedb.la

libnodedb_la_SOURCES = \
			connection.c     \
			connectionpool.c \
			encoding.c       \
			fastdb.c         \
			interface.c      \
			nodedb.c         \
			redblack.c       \
			scanmount.c      \
			sockbuf.c


libnodedb_la_CFLAGS   = $(FSAL_CFLAGS) $(SEC_CFLAGS)

new: clean all

all: nodedb all-am

nodedb:
	cd nodedb && make all

connection.c: nodedb
	cd nodedb && make all

connection.lo: connection.c
connectionpool.lo: connectionpool.c
encoding.lo: encoding.c
fastdb.lo: fastdb.c
interface.lo: interface.c
nodedb.lo: nodedb.c
redblack.lo: redblack.c
scanmount.lo: scanmount.c
sockbuf.lo: sockbuf.c

