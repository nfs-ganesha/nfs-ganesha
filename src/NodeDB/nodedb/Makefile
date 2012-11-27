

all: ../interface.c nodedb


.PHONY: all


CDEPS = sockbuf.c encoding.c nodedb.c redblack.c connection.c connectionpool.c scanmount.c fastdb.c
HDEPS = connection.h connectionpool.h encoding.h marshal.h nodedb.h redblack.h sockbuf.h scanmount.h fastdb.h

CFILES = interface.c $(CDEPS)
HFILES = interface.h ops.h $(HDEPS)


../interface.c nodedb: $(CDEPS) $(HDEPS) marshal.c
	
	gcc -ansi -pedantic marshal.c -o marshal -Wall
	
	./marshal -i nodedb.c -oc interface.c -oi interface.h -oo ops.h
	
	for i in $(CFILES) $(HFILES) ; do cp $$i ../ ; done
	
	if test x`hostname` = xdesktop ; then \
	    gcc  -DUNIT_TEST  -pthread -O0 -g -Wall -o nodedb -ansi -pedantic -pthread interface.c sockbuf.c  encoding.c nodedb.c redblack.c connection.c connectionpool.c scanmount.c fastdb.c -lpthread -Wno-long-long ; \
	    gcc  -DUNITTEST  -pthread -O0 -g -Wall -o fastdb -ansi -pedantic -pthread fastdb.c redblack.c -lpthread -Wno-long-long ; \
	fi


clean:
	rm -f marshal ops.h interface.c interface.h nodedb
	for i in $(CFILES) $(HFILES) ; do rm -f ../$$i ; done



