import nfs4pgfh   
import nfs4state 
import time
import sha
import sys

fs = nfs4state.NFSFileSystem()
r = nfs4pgfh.PGFileHandle(fs, "/", "/", pgconnect=('mmurray'))
print r.read_dir()
if len(sys.argv) > 1:
	fh=r.create(sys.argv[1])
	fh.write(0, "Howdy-Ho, Neighboroo!")
	data = fh.read(0, 100)
	print data


