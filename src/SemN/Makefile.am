noinst_LTLIBRARIES    = libSemN.la

libSemN_la_SOURCES  = SemN.c              \
                      ../include/SemN.h

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make
