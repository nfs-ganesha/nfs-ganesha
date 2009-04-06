#!/usr/bin/env python2.2

from nfs4constants import *
from nfs4types import *
import nfs4packer
import rpc
import nfs4lib
import os
import time
import StringIO
from stat import *
import sha

class NFSFileSystem:
	"""This implements the NFS4 pseudo-filesystem using NFS4VirtualHandles."""
        def __init__(self):
                self.dirent = {}
                self.filehandles = {}	
		
        def bind(self, object):
                self.root = object

        def get_fh(self, handle):
                if self.filehandles.has_key(handle):
                        return self.filehandles[handle]
                else:
                        return 0

	def get_root(self):
                return self.root

        def register(self, handle, object):
                self.filehandles[handle] = object
                self.filehandles[object.ref] = object
                
        def unregister(self, handle):
                if self.filehandles.has_key(handle):
                        del self.filehandles[object.ref]
                        del self.filehandles[handle]

class NFSFileHandle:
	def __init__(self, server, ref, file):
                self.ref = ref
		self.file = file
                self.server = server
		self.handle = self.get_fhclass() + sha.new(ref).hexdigest()
                self.server.register(self.handle, self)
                self.change = 0
		self.lock_status = {}
		
        def __repr__(self):
		return "<NFSFileHandle(%s): %s>" % (self.get_fhclass(), self.ref)

	def add_lock(self, lock_owner, lock_data):
		if self.lock_status.has_key(lock_owner):
			self.lock_status[lock_owner].append(lock_data)
		else:
			self.lock_status[lock_owner] = [lock_data]

        def get_attributes(self, ncl=nfs4lib.DummyNcl(), attrlist=None):
                raise FileHandle, "Implement get_attributes"
                
        def get_directory(self):
                raise FileHandle, "Implement get_directory"                

	def get_type(self):
		raise FileHandle, "Implement get_type"
	
	def read(self, offset, count):
		raise FileHandle, "Implement read"

	def release_lock(self, lock_owner, lock_data):
		for i in self.lock_status[lock_owner]:
			if lock_data.offset == i.offset and lock_data.length == i.length and lock_data.locktype == i.locktype:
				self.lock_status[lock_owner].remove(i)
				if self.lock_status[lock_owner] == []:
					del self.lock_status[lock_owner]
					return

class VirtualHandle(NFSFileHandle):
        def __init__(self, server, ref, file, type):
                NFSFileHandle.__init__(self, server, ref, file)
                self.type = type;
                self.st_mtime = int(time.time())
                self.st_atime = int(time.time())
                self.st_ctime = int(time.time())
                self.st_size = 0
                self.st_ino = 0
                self.st_uid = 0
                self.st_gid = 0
                self.named_attr = {}
		
		if type == NF4DIR:
                        self.dirent = []
                else:
                        self.file = StringIO.StringIO()

	def create(self, name):
		if self.type <> NF4DIR:
			raise FileHandle, "create called on non-directory (%s)" % self.ref
		fh = VirtualHandle(self.server, os.path.join(self.ref, name),
				   os.path.join(self.ref, name), NF4REG)
		self.dirent.append(fh)
		return fh

	def create_dir(self, name):
		if self.type <> NF4DIR:
			raise FileHandle, "create_dir called on non-directory (%s)" % self.ref
		fh = VirtualHandle(self.server, os.path.join(self.ref, name),
				   os.path.join(self.ref, name), NF4DIR)
		self.dirent.append(fh)
		return fh

        def get_attributes(self, ncl=nfs4lib.DummyNcl(), attrlist=None):
                ret_dict = {};
                for attr in attrlist:
                        if attr == FATTR4_SUPPORTED_ATTRS:
				ret_dict[attr] = list2attrmask([ FATTR4_SUPPORTED_ATTRS, FATTR4_TYPE,
								 FATTR4_CHANGE, FATTR4_SIZE, FATTR4_FSID,
								 FATTR4_LEASE_TIME, FATTR4_FILEID,
								 FATTR4_MAXFILESIZE, FATTR4_MAXREAD,
								 FATTR4_MAXWRITE, FATTR4_MODE,
								 FATTR4_NUMLINKS, FATTR4_OWNER,
								 FATTR4_OWNER_GROUP, FATTR4_RAWDEV ])
			elif attr == FATTR4_TYPE:
                                if type == NF4DIR:
                                        ret_dict[attr] = NF4DIR
                                else:
                                        ret_dict[attr] = NF4REG
        		elif attr == FATTR4_CHANGE:
                                ret_dict[attr] = nfstime4(ncl, self.st_ctime, 0);
        		elif attr == FATTR4_SIZE:
                                ret_dict[attr] = self.st_size
                        elif attr == FATTR4_FSID:
                                ret_dict[attr] = "woop"
                        elif attr == FATTR4_LEASE_TIME:
                                ret_dict[attr] = 1700
        		elif attr == FATTR4_FILEID: 
                                ret_dict[attr] = self.st_ino
                        elif attr == FATTR4_MAXFILESIZE:
                                ret_dict[attr] = 1000000
                        elif attr == FATTR4_MAXREAD:
                                ret_dict[attr] = 1000
                        elif attr == FATTR4_MAXWRITE:
                                ret_dict[attr] = 1000
                        elif attr == FATTR4_MODE:
                                ret_dict[attr] = self.st_mode
                        elif attr == FATTR4_NUMLINKS:
                                ret_dict[attr] = self.st_nlink
                        elif attr == FATTR4_OWNER:
                                ret_dict[attr] = self.st_uid
                        elif attr == FATTR4_OWNER_GROUP:
                                ret_dict[attr] = self.st_gid
                        elif attr == FATTR4_RAWDEV:
                                ret_dict[attr] = 0
                        elif attr == FATTR4_TIME_ACCESS:
                                ret_dict[attr] = nfstime4(ncl, self.st_atime, 0)
			elif attr == FATTR4_TIME_MOIDIFY:
				ret_dict[attr] = nfstime4(ncl, self.st_mtime, 0)
                return ret_dict

	def get_fhclass(self):
		return "virt"

	def get_type(self):
		return self.type;

	def read(self, offset, count):
		self.file.seek(offset)
		data = self.file.read(count)
		return data

	def read_dir(self):
                if self.type == NF4DIR:
                        return self.dirent

	def write(self, offset, data):
		self.file.seek(offset)
		count = self.file.write(data)
		return len(data)
		
class HardHandle(NFSFileHandle):
        def __init__(self, server, ref, file):
                NFSFileHandle.__init__(self, server, ref, file)
		
	def do_lookupp(self):
		return self.server.get_fh(os.path.dirname(self.ref))

	def get_attributes(self, ncl=nfs4lib.DummyNcl(), attrlist=None):
		stat_struct = os.lstat(self.file)
                ret_dict = {};
                for attr in attrlist:
                        if attr == FATTR4_TYPE:
                                if S_ISDIR(stat_struct.st_mode):
        			        ret_dict[attr] = NF4DIR
                		elif S_ISREG(stat_struct.st_mode):
                			ret_dict[attr] = NF4REG
				elif S_ISLNK(stat_struct.st_mode):
					ret_dict[attr] = NF4LNK
        		elif attr == FATTR4_CHANGE:
                                ret_dict[attr] = nfstime4(ncl, stat_struct.st_ctime, 0);
        		elif attr == FATTR4_SIZE:
                                ret_dict[attr] = stat_struct.st_size
                        elif attr == FATTR4_FSID:
                                ret_dict[attr] = "woop"
                        elif attr == FATTR4_LEASE_TIME:
                                ret_dict[attr] = 1700
        		elif attr == FATTR4_FILEID: 
                                ret_dict[attr] = stat_struct.st_ino
                        elif attr == FATTR4_MAXFILESIZE:
                                ret_dict[attr] = 1000000
                        elif attr == FATTR4_MAXREAD:
                                ret_dict[attr] = 1000
                        elif attr == FATTR4_MAXWRITE:
                                ret_dict[attr] = 1000
                        elif attr == FATTR4_MODE:
                                ret_dict[attr] = stat_struct.st_mode
                        elif attr == FATTR4_NUMLINKS:
                                ret_dict[attr] = stat_struct.st_nlink
                        elif attr == FATTR4_OWNER:
                                ret_dict[attr] = stat_struct.st_uid
                        elif attr == FATTR4_OWNER_GROUP:
                                ret_dict[attr] = stat_struct.st_gid
                        elif attr == FATTR4_RAWDEV:
                                ret_dict[attr] = 0
                        elif attr == FATTR4_TIME_ACCESS:
                                ret_dict[attr] = nfstime4(ncl, stat_struct.st_atime, 0);
 			elif attr == FATTR4_TIME_MODIFY:
                                ret_dict[attr] = nfstime4(ncl, stat_struct.st_mtime, 0);
                return ret_dict

 	def get_fhclass(self):
		return "hard"
	
        def get_link(self):
                return os.readlink(self.file)

	def get_type(self):
		stat_struct = os.lstat(self.file)
		if S_ISDIR(stat_struct.st_mode):
			return NF4DIR
		elif S_ISREG(stat_struct.st_mode):
			return NF4REG
		elif S_ISLNK(stat_struct.st_mode):
			return NF4LNK
		else:
			return NF4REG

	def read(self, offset, count):
		fh = open(self.file)
		fh.seek(offset)
		data = fh.read(count)
		fh.close()
		return data

	def read_dir(self):
                stat_struct = os.stat(self.file)
                if not S_ISDIR(stat_struct.st_mode):
                	return []
		list = []
                for i in os.listdir(self.file):
                        fullref = os.path.join(self.ref, i)
                        fullfile = os.path.join(self.file, i)
                        if self.server.get_fh(fullref):
                                list.append(self.server.get_fh(fullref))
                        else:
                                fh = HardHandle(self.server, fullref, fullfile)
                                list.append(fh)
                return list

        def write(self, offset, count, data):
                fh = open(self.file, 'r+')
                fh.seek(offset)
                fh.write(data)
                fh.close()
                return len(data) 

		
class NFSClientHandle:
	def __init__(self):
		self.confirmed = 0
                self.curr_fh = 0
                self.fh_stack = []
                self.dirlist = {}
                self.entry = {}
		self.lock_owner = None

	def confirm(self):
		self.confirmed = 1
