#!/usr/bin/env python2.2
#
# nfs4server.py - NFS4 server in python
#
# Written by Martin Murray <mmurray@deepthought.org>
# Copyright (C) 2001 University of Michigan, Center for 
#                    Information Technology Integration
#


from nfs4constants import *
from nfs4types import *
import nfs4packer
import rpc
import nfs4lib
import os
import time
import StringIO
from stat import *
import nfs4state
import sha

class NFSServer:
	def __init__(self):
                self.clients = {}
		self.clientenum = 4000
		
	def accept_host(self, host):
                name, port = host
                if name <> '127.0.0.1':
                        return NFS4ERR_ACCES
                else:
                        return NFS4_OK

        def new_client(self, host):
		print "NEW CLIENT: %s" % host
		self.clientenum = self.clientenum + 1
		client_handle = nfs4state.NFSClientHandle()
                handle = sha.new(host).hexdigest()
                self.clients[handle] = client_handle
                client_handle.clientid = self.clientenum
		return self.clientenum
                
        def get_client(self, host):
                handle = sha.new(host).hexdigest()
                if self.clients.has_key(handle):
                        return self.clients[handle]
		self.new_client(host)
		return self.get_client(host)
	
	def prep_client(self, host):
		print "PREPCLIENT"
		self.curr_fh = None
		self.client = self.get_client("%s %s" % host)
	
	def bind_fs(self, filesystem):
                self.fs = filesystem

	def O_Compound(self, ncl, cmp4args):
		try:
			cmp4args.unpack()
		except BadDiscriminant:
			print " ! BAD DISCRIMINANT unpacking COMPOUND4args"
			print " ! error NFS4ERR_NOTSUPP"
			return (NFS4ERR_OP_ILLEGAL, [ILLEGAL4res(ncl, NFS4ERR_OP_ILLEGAL)])
         	print " UDP NFSv4 COMPOUND call, tag: '%s', minor: %d, n_ops: %d from %s" % (cmp4args.tag, cmp4args.minorversion, len(cmp4args.argarray), self.remote)
		if cmp4args.minorversion <> 0:
			print "  ! MINOR VERSION MISMATCH"
			print "  ! error NFS4ERR_MINOR_VERS_MISMATCH"
			return (NFS4ERR_MINOR_VERS_MISMATCH, [])
		self.prep_client(self.sender_port)			
		results = []
		ok = NFS4_OK
		for op in cmp4args.argarray:
			print " NFSv4 Operation: %s (%d)" % ( nfs_opnum4_id[op.argop], op.argop )
			op.remote = self.remote
                        op.host_port = self.sender_port
                        ok = NFS4ERR_INVAL
			if(op.argop == OP_ACCESS):
				ok, result = self.O_Access(ncl, op)
			if(op.argop == OP_CREATE):
				ok, result = self.O_Create(ncl, op)
			if(op.argop == OP_CLOSE):
				ok, result = self.O_Close(ncl, op)
			if(op.argop == OP_COMMIT):
				ok, result = self.O_Commit(ncl, op)
			if(op.argop == OP_DELEGPURGE):
				ok, result = self.O_DelegPurge(ncl, op)
			if(op.argop == OP_DELEGRETURN):
				ok, result = self.O_DelegReturn(ncl, op)
			if(op.argop == OP_GETATTR):
				ok, result = self.O_GetAttr(ncl, op)
			if(op.argop == OP_GETFH):
				ok, result = self.O_GetFH(ncl, op)
			if(op.argop == OP_LOCK): 
				ok, result = self.O_Lock(ncl, op)
			if(op.argop == OP_LOCKT):
				ok, result = self.O_LockT(ncl, op)
			if(op.argop == OP_LOCKU):
				ok, result = self.O_LockU(ncl, op)
			if(op.argop == OP_LOOKUP):
				ok, result = self.O_LookUp(ncl, op)
			if(op.argop == OP_LOOKUPP):
				ok, result = self.O_LookUPP(ncl, op)
			if(op.argop == OP_NVERIFY):
				ok, result = self.O_NVerify(ncl, op)
			if(op.argop == OP_OPEN):
				ok, result = self.O_Open(ncl, op)
			if(op.argop == OP_OPENATTR):
				ok, result = self.O_OpenAttr(ncl, op)
			if(op.argop == OP_OPEN_CONFIRM):
				ok, result = self.O_OpenConfirm(ncl, op)
			if(op.argop == OP_OPEN_DOWNGRADE):
				ok, result = self.O_OpenDowngrade(ncl, op)
			if(op.argop == OP_PUTFH):
				ok, result = self.O_PutFH(ncl, op)
			if(op.argop == OP_PUTPUBFH):
				ok, result = self.O_PutPubFH(ncl, op)
			if(op.argop == OP_PUTROOTFH):
				ok, result = self.O_PutRootFH(ncl, op)
			if(op.argop == OP_READ):
				ok, result = self.O_Read(ncl, op)
			if(op.argop == OP_READDIR):
				ok, result = self.O_ReadDir(ncl, op)
			if(op.argop == OP_READLINK):
				ok, result = self.O_ReadLink(ncl, op)
			if(op.argop == OP_REMOVE):
				ok, result = self.O_Remove(ncl, op)
			if(op.argop == OP_RENAME):
				ok, result = self.O_Rename(ncl, op)
			if(op.argop == OP_RENEW):
				ok, result = self.O_Renew(ncl, op)
			if(op.argop == OP_RESTOREFH):
				ok, result = self.O_RestoreFH(ncl, op)
			if(op.argop == OP_SAVEFH):
				ok, result = self.O_SaveFH(ncl, op)
			if(op.argop == OP_SECINFO):
				ok, result = self.O_SecInfo(ncl, op)
			if(op.argop == OP_SETATTR):
				ok, result = self.O_SetAttr(ncl, op)
			if(op.argop == OP_SETCLIENTID):
				ok, result = self.O_SetClientID(ncl, op)
			if(op.argop == OP_SETCLIENTID_CONFIRM):
				ok, result = self.O_SetClientIDConfirm(ncl, op)
			if(op.argop == OP_VERIFY):
				ok, result = self.O_Verify(ncl, op)
			if(op.argop == OP_WRITE):
				ok, result = self.O_Write(ncl, op)
			if(op.argop > OP_WRITE):
				ok = NFS4ERR_NOTSUPP
			results += [ result ]
			if ok <> NFS4_OK:
                                print "  ! error %s" % nfsstat4_id[ok]
				break
		return (ok, results)

	# FIXME: needs to actually query for the access to the file!
	def O_Access(self, ncl, op):
		print "SERVER ACCESS"
		print "  CURRENT FILEHANDLE: %s" % self.curr_fh.ref
                a4_supported = op.opaccess.access
                a4_access = op.opaccess.access
                a4resok = ACCESS4resok(ncl, a4_supported, a4_access)
                a4res = ACCESS4res(ncl, NFS4_OK, a4resok)
                argop = nfs_resop4(ncl, resop=OP_ACCESS, opaccess=a4res)
                return (NFS4_OK, argop)

	def O_Close(self, ncl, op):
		print "SERVER CLOSE"
		print "  CURRENT FILEHANDLE: %s" % self.curr_fh.ref
		sid4 = stateid4(ncl, seqid=op.opclose.seqid, other="notsure")
		c4res = CLOSE4res(ncl, status=NFS4_OK, open_stateid=sid4)
		argop = nfs_resop4(ncl, resop=OP_CLOSE, opclose=c4res)
		return (NFS4_OK, argop)
	
	def O_Create(self, ncl, op):
		print "SERVER CREATE"
		print "  CURRENT FILEHANDLE: %s" % self.curr_fh.ref
		old_cinfo = self.curr_fh.change
		if op.opcreate.objtype.type == NF4DIR:
			newfh = self.curr_fh.create_dir(op.opcreate.objname)
		else:
			newfh = self.curr_fh
		new_cinfo = self.curr_fh.change
		self.curr_fh = newfh
		attrs = nfs4lib.list2attrmask([])
		cin4 = change_info4(ncl, before=old_cinfo, after=new_cinfo, atomic=1)
		c4resok = CREATE4resok(ncl, cinfo=cin4, attrset = attrs)
		c4res = CREATE4res(ncl, NFS4_OK, c4resok)
		argop = nfs_resop4(ncl, resop=OP_CREATE, opcreate=c4res)
		return (NFS4_OK, argop)

	def O_GetAttr(self, ncl, op):
		print "SERVER GETATTR"
		print "  ATTRMASK: %s" % str(nfs4lib.attrmask2attrs(op.opgetattr.attr_request))
                attrs = nfs4lib.attrmask2list(op.opgetattr.attr_request)
		attrvals = self.curr_fh.get_attributes(ncl, attrs)
                f4 = nfs4lib.dict2fattr(attrvals, ncl)
		garesok = GETATTR4resok(ncl, f4)
		gares = GETATTR4res(ncl, NFS4_OK, garesok)
		argop = nfs_resop4(ncl, resop=OP_GETATTR, opgetattr=gares)
		return (NFS4_OK, argop)

	def O_GetFH(self, ncl, op):
		print "SERVER GETFH"
		print "  FILEHANDLE %s" % self.curr_fh.handle
		confirmres = GETFH4resok(ncl, str(self.curr_fh.handle))
		fhres = GETFH4res(ncl, NFS4_OK, confirmres)
		argop = nfs_resop4(ncl, resop=OP_GETFH, opgetfh=fhres)
		return (NFS4_OK, argop)


        def O_Lock(self, ncl, op):
                print "SERVER LOCK"
                print "  CURRENT FILEHANDLE %s" % self.curr_fh.ref

		# Conditionals to test for a lock conflict: 
                if self.curr_fh.lock_status:
			offset = op.oplock.offset
			length = op.oplock.length
			for key in self.curr_fh.lock_status.keys():
				for entry in self.curr_fh.lock_status[key]:
					if ( (offset >= entry.offset and offset <= (entry.offset + entry.length)) or (entry.offset >= offset and entry.offset <= (offset + length)) ):
						lock_denied = LOCK4denied(ncl, entry.offset,
									  entry.length,
									  entry.locktype,
									  owner=key)
						l4res = LOCK4res(ncl, NFS4ERR_DENIED, denied=lock_denied)
						argop = nfs_resop4(ncl, resop=OP_LOCK, oplock=l4res)
						return (NFS4ERR_DENIED, argop)

		# If no lock conflict exists, create lock:
		self.client.lock_owner = op.oplock.locker.open_owner.lock_owner
		lock_stateid = stateid4(ncl, op.oplock.locker.open_owner.lock_seqid, "FIXME")
		lock_data = nfs4lib.LockData(op.oplock.offset, op.oplock.length,
					     op.oplock.locktype, lock_stateid)
		self.curr_fh.add_lock(self.client.lock_owner, lock_data)
		l4resok = LOCK4resok(ncl, lock_stateid)
		l4res = LOCK4res(ncl, NFS4_OK, l4resok)
		argop = nfs_resop4(ncl, resop=OP_LOCK, oplock=l4res)
		return (NFS4_OK, argop)


	def O_LockT(self, ncl, op):
		print "SERVER LOCKT"
		print "  CURRENT FILEHANDLE %s" % self.curr_fh.ref

		# Conditionals to test for a lock conflict: 
                if self.curr_fh.lock_status:
			offset = op.oplockt.offset
			length = op.oplockt.length
			for key in self.curr_fh.lock_status.keys():
				for entry in self.curr_fh.lock_status[key]:
					if ( (offset >= entry.offset and offset <= (entry.offset + entry.length)) or (entry.offset >= offset and entry.offset <= (offset + length)) ):
						lock_denied = LOCK4denied(ncl, entry.offset,
									  entry.length,
									  entry.locktype,
									  owner=key)
						l4res = LOCKT4res(ncl, NFS4ERR_DENIED, denied=lock_denied)
						argop = nfs_resop4(ncl, resop=OP_LOCKT, oplockt=l4res)
						return (NFS4ERR_DENIED, argop)

		# If no lock conflict exists, return NFS4_OK:
		l4res = LOCKT4res(ncl, NFS4_OK)
		argop = nfs_resop4(ncl, resop=OP_LOCKT, oplockt=l4res)
		return (NFS4_OK, argop)


	# FIXME: Should this be able to release a lock not owned by the client?
	#        Very limited range of functionality right now.
	def O_LockU(self, ncl, op):
		print "SERVER LOCKU"
		print "  CURRENT FILEHANDLE %s" % self.curr_fh.ref
		lock_data = nfs4lib.LockData(op.oplocku.offset, op.oplocku.length,
					     op.oplocku.locktype, op.oplocku.lock_stateid)
		self.curr_fh.release_lock(self.client.lock_owner, lock_data)
		lu4res = LOCKU4res(ncl, NFS4_OK, op.oplocku.lock_stateid)
		argop = nfs_resop4(ncl, resop=OP_LOCKU, oplocku=lu4res)
		return (NFS4_OK, argop)


        def O_LookUp(self, ncl, op):
                print "SERVER LOOKUP"
                print "  CURRENT FILEHANDLE %s" % self.curr_fh.ref
                print "  REQUESTED OBJECT %s" % op.oplookup.objname
                r = os.path.join(self.curr_fh.ref, op.oplookup.objname)
                self.curr_fh.read_dir() # ensure that all filehandles are read.
                if self.fs.get_fh(r):
                        self.curr_fh = self.fs.get_fh(r)
                        res = LOOKUP4res(ncl, NFS4_OK)
                        argop = nfs_resop4(ncl, resop=OP_LOOKUP, oplookup=res)
                        return (NFS4_OK, argop)
                else:
                        res = LOOKUP4res(ncl, NFS4ERR_NOENT)
                        argop = nfs_resop4(ncl, resop=OP_LOOKUP, oplookup=res)
                        return (NFS4ERR_NOENT, argop)

	def O_LookUPP(self, ncl, op):
                print "SERVER LOOKUPP"
                print "  CURRENT FILEHANDLE %s" % self.curr_fh.ref
                if self.curr_fh.get_type() == NF4DIR:
			self.curr_fh = self.curr_fh.do_lookupp()
                        lupp4res = LOOKUPP4res(ncl, NFS4_OK)
                        argop = nfs_resop4(ncl, resop=OP_LOOKUPP, oplookupp=lupp4res)
                        return (NFS4_OK, argop)
                else:
                        lupp4res = LOOKUPP4res(ncl, NFS4ERR_NOTDIR)
                        argop = nfs_resop4(ncl, resop=OP_LOOKUPP, oplookupp=lupp4res)
                        return (NFS4ERR_NOTDIR, argop)

        def O_Open(self, ncl, op):
                print "SERVER OPEN"
		print "  CURRENT FILEHANDLE: %s" % self.curr_fh.ref
		if not self.curr_fh or self.curr_fh.get_type() <> NF4DIR:
			print "  ! CURRENT FILEHANDLE IS NOT A DIRECTORY."
			print "  ! error NFS4ERR_NOTDIR"
			res = OPEN4res(ncl, NFS4ERR_NOTDIR)
			argop = nfs_resop4(ncl, resop=OP_OPEN, opopen=res)
			return (NFS4ERR_NOTDIR, argop)
		self.curr_fh.read_dir() # ensure all filehandles are read
		print "  FILE %s" % (op.opopen.claim.file)
		old_cinfo = self.curr_fh.change
		filename = os.path.join(self.curr_fh.ref, op.opopen.claim.file)
		if op.opopen.openhow.opentype == OPEN4_CREATE:
			print "  CREATING FILE."
			self.curr_fh.create(op.opopen.claim.file)
		else:
			print "  OPENING EXISTING FILE."
		self.client.seqid = op.opopen.seqid
		sid = stateid4(ncl, seqid=self.client.seqid, other="fixme")
		new_cinfo = self.curr_fh.change
		cif4 = change_info4(ncl, atomic=1, before=old_cinfo, after=new_cinfo)
		od4 = open_delegation4(ncl, delegation_type=OPEN_DELEGATE_NONE)
		o4rok = OPEN4resok(ncl, stateid=sid, cinfo=cif4, rflags=0,
				   attrset=[0], delegation=od4)
		self.curr_fh = self.fs.get_fh(filename)
		res = OPEN4res(ncl, status=NFS4_OK, resok4=o4rok)
		argop = nfs_resop4(ncl, resop=OP_OPEN, opopen=res)
		return (NFS4_OK, argop)
		
		
	def O_PutFH(self, ncl, op):
		print "SERVER PUTFH"
		print "  FILEHANDLE %s" % op.opputfh.object
                self.curr_fh = self.fs.get_fh(op.opputfh.object)
		fhres = PUTFH4res(ncl, NFS4_OK)
		argop = nfs_resop4(ncl, resop=OP_PUTFH, opputfh=fhres)
		return (NFS4_OK, argop)

	def O_PutPubFH(self, ncl, op):
		print "SERVER PUTPUBFH"
		self.curr_fh = self.fs.get_root()
		print "  NEW FILEHANDLE %s" % self.curr_fh.handle
		confirmres = PUTPUBFH4res(ncl, NFS4_OK)
		argop = nfs_resop4(ncl, resop=OP_PUTPUBFH, opputpubfh=confirmres)
		return (NFS4_OK, argop)
	
	def O_PutRootFH(self, ncl, op):
		print "SERVER PUTROOTFH"
		self.curr_fh = self.fs.get_root()
		print "  NEW FILEHANDLE %s" % self.curr_fh.handle
		confirmres = PUTROOTFH4res(ncl, NFS4_OK)
		argop = nfs_resop4(ncl, resop=OP_PUTROOTFH, opputrootfh=confirmres)
		return (NFS4_OK, argop)

	def O_Read(self, ncl, op):
		print "SERVER READ"
		print "  CURRENT FILEHANDLE %s" % self.curr_fh.ref
		print "  OFFSET: %d COUNT %d" % (op.opread.offset, op.opread.count)
		read_data = self.curr_fh.read(op.opread.offset, op.opread.count)
		if len(read_data) < op.opread.count:
			read_eof = 1
		else:
			read_eof = 0
		r4rok = READ4resok(ncl, eof=read_eof, data=read_data)
		r4res = READ4res(ncl, NFS4_OK, resok4 = r4rok)
		argop = nfs_resop4(ncl, resop=OP_READ, opread=r4res)
		return (NFS4_OK, argop)

	def O_ReadDir(self, ncl, op):
		print "SERVER READDIR"
		print "  CURRENT FILEHANDLE %s" % self.curr_fh.ref
                print "  COOKIEVERF: %s, %s" % ( op.opreaddir.cookieverf, op.opreaddir.cookie)
                print "  ATTRMASK: %s" % str(nfs4lib.attrmask2attrs(op.opreaddir.attr_request))

                if op.opreaddir.cookie == 0:
                        cookie = str(int(time.time()))[2:]
                        self.client.dirlist[cookie] = self.curr_fh.read_dir()
                        ncoky = 1 
                        start = 0
                else:
                        cookie = op.opreaddir.cookieverf
                        start = op.opreaddir.cookie*50
                        ncoky = op.opreaddir.cookie+1
                
		# FIXME: Bad system for counting the size of the readdir requests
                attrs = nfs4lib.attrmask2list(op.opreaddir.attr_request)
		e4 = []
                cnt = 100
                while len(self.client.dirlist[cookie])>(start) and cnt >0:
                        cnt = cnt - 1
                        entry = self.client.dirlist[cookie][start]
			attrvals = entry.get_attributes(ncl, attrs) 
			f4 = nfs4lib.dict2fattr(attrvals, ncl)
			e4 = [ entry4(ncl, ncoky, name=entry.ref, attrs=f4, nextentry=e4)]
                        start = start + 1
                if start < len(self.client.dirlist[cookie]):
                        d4 = dirlist4(ncl, e4, eof=0)
                else:
                        del self.client.dirlist[cookie]
                        d4 = dirlist4(ncl, e4, eof=1)
		rdresok = READDIR4resok(ncl, cookieverf=cookie, reply=d4)
		rdres = READDIR4res(ncl, NFS4_OK, rdresok)
		argop = nfs_resop4(ncl, resop=OP_READDIR, opreaddir=rdres)	
		return (NFS4_OK, argop)

        def O_ReadLink(self, ncl, op):
                print "SERVER READLINK"
                print "  CURRENT FILEHANDLE: %s" % self.curr_fh.ref
                if self.curr_fh.get_type() == NF4LNK:
                        link_text = self.curr_fh.get_link()
                        rl4resok = READLINK4resok(ncl, link_text)
                        rl4res = READLINK4res(ncl, NFS4_OK, rl4resok)
                        argop = nfs_resop4(ncl, resop=OP_READLINK, opreadlink=rl4res)
                        return (NFS4_OK, argop)
                else:
                        rl4res = READLINK4res(ncl, NFS4ERR_INVAL)
                        argop = nfs_resop4(ncl, resop=OP_READLINK, opreadlink=rl4res)
                        return (NFS4ERR_INVAL, argop)

	def O_SetAttr(self, ncl, op):
		attrsset = nfs4lib.list2attrmask([])
		sa4res = SETATTR4res(ncl, NFS4_OK, attrsset)
		argop = nfs_resop4(ncl, resop=OP_SETATTR, opsetattr=sa4res)
		return (NFS4_OK, argop)
	
	def O_SetClientID(self, ncl, op):
		print "SERVER SETCLIENTID"
		print "  ARGS, server: %s" % ( op.opsetclientid.client.id)
		print op.opsetclientid.callback
                
                err = self.accept_host(op.host_port)
		print "  CLIENTID %s" % self.client.clientid
		if err <> NFS4_OK:
                        scres = SETCLIENT4res(ncl, err, null)
                        argop = nfs4_resop4(ncl, resop=OP_SETCLIENTID, opsetclientid=scres)
                        return (err, argop)
                setclientres = SETCLIENTID4res(ncl, NFS4_OK,
					       SETCLIENTID4resok(ncl, self.client.clientid),
					       "bonus")
		argop = nfs_resop4(ncl, resop=OP_SETCLIENTID, opsetclientid=setclientres)
		return (NFS4_OK, argop)
		
	def O_SetClientIDConfirm(self, ncl, op):
		print "SERVER SETCLIENTID_CONFIRM"
		print "  ARGS, clientid %s" % str(op.opsetclientid_confirm.clientid)
                self.client.confirm()
                confirmres = SETCLIENTID_CONFIRM4res(ncl, NFS4_OK)
		argop = nfs_resop4(ncl, resop=OP_SETCLIENTID_CONFIRM,
				   opsetclientid_confirm=confirmres)
		return (NFS4_OK, argop)
		
	def O_Write(self, ncl, op):
		print "SERVER WRITE"
		print "  CURRENT FILEHANDLE %s" % self.curr_fh.ref
		print "  OFFSET: %d COUNT %d" % (op.opwrite.offset, len(op.opwrite.data))
		written_data = self.curr_fh.write(op.opwrite.offset, op.opwrite.data)
		w4resok = WRITE4resok(ncl, len(op.opwrite.data), op.opwrite.stable, "")
		w4res= WRITE4res(ncl, NFS4_OK, w4resok)
		argop = nfs_resop4(ncl, resop=OP_WRITE, opwrite = w4res)
		return (NFS4_OK, argop)


class UDPServer(rpc.UDPServer):
	def addserver(self, server):
		self.server = server

	def addpackers(self):
		self.packer = nfs4packer.NFS4Packer(self)
		self.unpacker = nfs4packer.NFS4Unpacker(self,'')

        def donotusebindsocket(self, sock=853):
	        if self.prog <0x200000:
	                try:
	                        self.sock.bind(('', sock))
	                except socket.error:
	                        self.bindsocket(sock+1)
	        else:
        	        self.sock.bind(('', 0))
	
	def handle_1(self):
		cmp4args = COMPOUND4args(self)
		print
		print "UDP RPC CALL"
		print " Credentials: %s" % repr(self.recv_cred)
                print " Verifier: %s" % repr(self.recv_verf)
		self.server.sender_port = self.sender_port
		self.server.remote = "%s %s" % self.sender_port
		ok, results = self.server.O_Compound(self, cmp4args)
		try:
			self.turn_around()
		except rpc.RPCUnextractedData:
			print "*** Unextracted Data in request!"
		cmp4res = COMPOUND4res(self, ok, cmp4args.tag, results)
                print "*** CMP4RES: %s" % repr(cmp4res)
		cmp4res.pack()
                return
	
	def turn_around_deux(self):
		try:
			self.unpacker.done()
		except xdrlib.Error:
			print "**** Unextracted Data in Request!"

		self.packer.pack_uint(SUCCESS)
		
def main():
        udpserver = UDPServer('', NFS4_PROGRAM, NFS_V4, 2049)
        udpserver.register()
        filesystem = nfs4state.NFSFileSystem()
        #root_fh = nfs4state.VirtualHandle(filesystem, "/", "/", NF4DIR)
	root_fh = nfs4state.HardHandle(filesystem, "/", "/")
        filesystem.bind(root_fh)
        server = NFSServer()
        server.bind_fs(filesystem)
        udpserver.addserver(server)
	print "Python NFS4 Server, (c) CITI, Regents of the University of Michigan"
	print "Starting Server, root handle: %s" % root_fh 
	try:
        	udpserver.loop()
        finally:
        	udpserver.unregister()

main()
