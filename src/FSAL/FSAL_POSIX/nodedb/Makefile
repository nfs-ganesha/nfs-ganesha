

all: ../interface.c nodedb


.PHONY: all


CDEPS = sockbuf.c encoding.c nodedb.c redblack.c connection.c connectionpool.c
HDEPS = connection.h connectionpool.h encoding.h marshal.h nodedb.h redblack.h sockbuf.h

CFILES = interface.c $(CDEPS)
HFILES = interface.h ops.h $(HDEPS)


../interface.c nodedb: $(CDEPS) $(HDEPS) marshal.c
	
	gcc -ansi -pedantic marshal.c -o marshal -Wall
	
	./marshal > interface.c
	
	grep '@PROTOTYPE@' interface.c > interface.h
	
	for i in $(CFILES) $(HFILES) ; do cp $$i ../ ; done
	
	gcc  -DUNIT_TEST  -pthread -O0 -g -Wall -o nodedb -ansi -pedantic -pthread interface.c sockbuf.c  encoding.c nodedb.c redblack.c connection.c connectionpool.c -lpthread -Wno-long-long


clean:
	rm -f marshal ops.h interface.c interface.h nodedb
	for i in $(CFILES) $(HFILES) ; do rm -f ../$$i ; done



