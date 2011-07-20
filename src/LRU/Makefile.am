noinst_LTLIBRARIES            = liblru.la

liblru_la_SOURCES             = LRU_List.c

check_PROGRAMS                = test_configurable_lru  test_lru

if USE_BUDDY_SYSTEM
BUDDY_LIB_FLAGS = ../BuddyMalloc/libBuddyMalloc.la
else
BUDDY_LIB_FLAGS =
endif

test_lru_SOURCES              = test_lru.c
test_lru_LDADD                = liblru.la $(BUDDY_LIB_FLAGS) ../Log/liblog.la -lpthread

test_configurable_lru_SOURCES = test_configurable_lru.c
test_configurable_lru_LDADD   = liblru.la $(BUDDY_LIB_FLAGS) ../Log/liblog.la -lpthread

# these are tests we should be running on 'make check'
TESTS = test_lru

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

testrunner:  $(check_PROGRAMS)
	../tools/maketest -x LRU -f ./maketest.conf > ../testres-xml/LRU.xml

