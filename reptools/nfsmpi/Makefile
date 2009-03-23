nfsmpi: nfsmpi.o
	mpicc -g -o nfsmpi nfsmpi.o ../../src/XDR/.libs/libnfs_mnt_xdr.a 

nfs4mpi: nfs4mpi.o
	mpicc -g -o nfs4mpi nfs4mpi.o ../../src/XDR/.libs/libnfs_mnt_xdr.a 

all: nfsmpi nfs4mpi

clean:
	rm -f nfsmpi nfsmpi.o nfs4mpi nfs4mpi.o 

new: clean all
