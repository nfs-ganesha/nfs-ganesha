
dFLAGS=-g -DDEBUG
FLAGS= -g -O -Wall -Werror
PFLAGS= -p -g3

#
# The following is "boilerplate" to set up the standard compilation
# commands:
.SUFFIXES:
.SUFFIXES: .cpp .c .cc .h .o
.c.o:  ; gcc $(FLAGS) -c $*.c

all: ml_console ml_posix_client

clean:
	rm -f *.o ml_console ml_posix_client *.out *.file

ml_console:		ml_functions.o ml_console.o
			gcc $(FLAGS) -lm -o ml_console ml_functions.o ml_console.o

ml_posix_client:	ml_functions.o ml_posix_client.o
			gcc $(FLAGS) -lm -o ml_posix_client ml_functions.o ml_posix_client.o

ml_console.o:		ml_console.c multilock.h

ml_posix_client.o:	ml_posix_client.c multilock.h

ml_functions.o:		ml_functions.c multilock.h
