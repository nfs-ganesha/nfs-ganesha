# Copyright IBM Corporation, 2012
#  Contributor: Frank Filz  <ffilz@us.ibm.com>
#
#
# This software is a server that implements the NFS protocol.
#
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

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
			gcc $(FLAGS) -lm -lpthread -o ml_posix_client ml_functions.o ml_posix_client.o

ml_console.o:		ml_console.c multilock.h

ml_posix_client.o:	ml_posix_client.c multilock.h

ml_functions.o:		ml_functions.c multilock.h
