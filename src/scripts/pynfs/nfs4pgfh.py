import _pg
import pg
import string
import os
import nfs4state
from nfs4constants import *
import sha

class PGFileHandle(nfs4state.NFSFileHandle):
	def __init__(self, server, ref, file, pgconnect=None, db=None):
		if db == None:
			print pgconnect
			self.db = _pg.connect(pgconnect)
		else:
			self.db = db
		res = self.db.query("SELECT f_handle, type FROM meta WHERE ref=\'%s\'" % file);
		if len(res.getresult()) == 0 and file == '/':
			self.db.query("INSERT INTO meta (f_handle, ref, type) VALUES ('rootfh', '/', 2)");
			self.f_handle = 'rootfh'
			self.type = NF4DIR
		else:
			self.f_handle, self.type = res.getresult()[0]
		if self.type == NF4REG:
			res = self.db.query("SELECT obj_data FROM files WHERE f_handle = '%s'" % self.f_handle)
			self.oid = res.getresult()[0][0]
			print "OID: %d" % self.oid
		print "%s, %d" % (self.f_handle, self.type)
		self.handle = self.f_handle
		nfs4state.NFSFileHandle.__init__(self, server, ref, file)
		
	def get_fhclass(self):
		return "psql"

	def get_type(self):
		return self.type

	def read_dir(self):
		if self.type <> NF4DIR:
			return []
		res = self.db.query("SELECT obj_name FROM dirent WHERE d_handle = \'%s\'" % self.f_handle)
		dirent = []
		for result in res.getresult():
			name = result[0];
			fullref = os.path.join(self.ref, name)
			fullfile = os.path.join(self.file, name)
                        if self.server.get_fh(fullref):
				dirent.append(self.server.get_fh(fullref))
			else:
				dirent.append(PGFileHandle(self.server, fullref, fullfile, db=self.db))
		return dirent
	
	def create_dir(self, name):
		if self.type <> NF4DIR:
			raise nfs4state.FileHandle, "create_dir called on non-directory (%s)" % self.refi
		new_path = os.path.join(self.file, name)
		new_ref = os.path.join(self.ref, name)
		new_f_handle = sha.new(new_path).hexdigest()
		res = self.db.query("INSERT INTO meta (f_handle, ref, type) VALUES ('%s', '%s', %d)" % ( new_f_handle, new_path, NF4DIR))
		res = self.db.query("INSERT INTO dirent (d_handle, obj_name, f_handle) VALUES ('%s', '%s', '%s')" % ( self.f_handle, name, new_f_handle ))
		fh = PGFileHandle(self.server, new_ref, new_path, db=self.db)
		return fh;

	def create(self, name):
		large = self.db.locreate(pg.INV_WRITE+pg.INV_READ)
		print "OID: %d" % large.oid
		new_path = os.path.join(self.file, name)
		new_ref = os.path.join(self.ref, name)
		new_f_handle = sha.new(new_path).hexdigest()
		try:
			large.open(pg.INV_WRITE+pg.INV_READ)
		except IOError:
			print "Error: %s" % large.error
		large.write("poop")
		res = self.db.query("INSERT INTO meta (f_handle, ref, type) VALUES ('%s', '%s', %d)" % ( new_f_handle, new_path, NF4REG))
                res = self.db.query("INSERT INTO dirent (d_handle, obj_name, f_handle) VALUES ('%s', '%s', '%s')" % ( self.f_handle, name, new_f_handle ))
		res = self.db.query("INSERT INTO files (f_handle, obj_data) VALUES ('%s', %d)" % (new_f_handle, large.oid))
		large.close()
		fh = PGFileHandle(self.server, new_ref, new_path, db=self.db)
		return fh

	def write(self, offset, data):
		print type(self.oid), self.oid
		large = self.db.getlo(self.oid)
		large.open(pg.INV_WRITE+pg.INV_READ)
		large.seek(offset)
		large.write(data)
		large.close()
	
	def read(self, offset, count):
		large = self.db.getlo(self.oid)
		large.open(pg.INV_READ)
		large.seek(offset)
		data = large.read(data)
		large.close
		return data

