#!/usr/bin/env python2

# nfs4lib.py - NFS4 library for Python. 
#
# Written by Peter Åstrand <peter@cendio.se>
# Copyright (C) 2001 Cendio Systems AB (http://www.cendio.se)
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License. 
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

# TODO: 
# Implement buffering in NFS4OpenFile.

__pychecker__ = 'no-callinit no-reimport'

NFS_PORT = 2049

BUFSIZE = 4096

import rpc
from nfs4constants import *
from nfs4types import *
import nfs4packer
import random
import array
import socket
import os
import re
from types import *

try:
    import pwd
except ImportError:
    class pwdStub:
	def getpwuid(self, uid):
	    return "winuser"
    pwd = pwdStub()


# Stubs for Win32 systems
if not hasattr(os, "getuid"):
    os.getuid = lambda: 1

if not hasattr(os, "getgid"):
    os.getgid = lambda: 1

if not hasattr(os, "getgroups"):
    os.getgroups = lambda: []

# Class to move around locking data for a specific filehandle.
class LockData:
    def __init__(self, offset, length, locktype, stateid):
	self.offset = offset
	self.length = length
	self.locktype = locktype
	self.stateid = stateid

# All NFS errors are subclasses of NFSException
class NFSException(rpc.RPCException):
    pass

class BadCompoundRes(NFSException):
    """The COMPOUND procedure returned some kind of error"""
    def __init__(self, operation, errcode):
        self.operation = operation
        self.errcode = errcode

    def __str__(self):
        return "operation %s returned result %s" % (nfs_opnum4_id[self.operation],
                                                    nfsstat4_id[self.errcode])

class EmptyBadCompoundRes(NFSException):
    """The COMPOUND procedure returned some kind of error. No result array"""
    def __init__(self, errcode):
        self.errcode = errcode

    def __str__(self):
        return "compound call returned %s" % nfsstat4_id[self.errcode]
    

class InvalidCompoundRes(NFSException):
    """The COMPOUND procedure returned is invalid"""
    def __init__(self, msg=""):
        self.msg = msg
    
    def __str__(self):
        if self.msg:
            return "invalid COMPOUND result: %s" % self.msg
        else:
            return "invalid COMPOUND result"

    
class EmptyCompoundRes(NFSException):
    def __str__(self):
        return "empty COMPOUND result"

class ChDirError(NFSException):
    def __init__(self, dir):
        self.dir = dir
    
    def __str__(self):
        return "Cannot change directory to %s" % self.dir


class DummyNcl:
    def __init__(self, data = ""):
        self.unpacker = nfs4packer.NFS4Unpacker(self, data)
        self.packer = nfs4packer.NFS4Packer(self)


class PartialNFS4Client:
    def __init__(self):
        # Client state variables
        self.clientid = None
        self.verifier = None
        # Current directory. A list of components, like ["doc", "porting"]
        self.cwd = []
        # Last seqid
        self.seqid = 0
        # FIXME
        self.owner = 0
        # Set in sub-classes
        self.gid = None
        self.uid = None
        # Send call stack in COMPOUND tag?
        self.debugtags = 0

    def mkcred(self):
	if self.cred == None:
            hostname = socket.gethostname()
            groups = os.getgroups()
	    self.cred = (rpc.AUTH_UNIX, rpc.make_auth_unix(1, hostname, self.uid, self.gid, groups))
	return self.cred

    def mkverf(self):
	if self.verf == None:
	    self.verf = (rpc.AUTH_NULL, rpc.make_auth_null())
	return self.verf
    
    def addpackers(self):
 	# Pass a reference to ourself to NFS4Packer and NFS4Unpacker. 
        self.packer = nfs4packer.NFS4Packer(self)
        self.unpacker = nfs4packer.NFS4Unpacker(self, '')

    #
    # RPC procedures
    #

    def null(self):
	return self.make_call(NFSPROC4_NULL, None, None, None)

    def compound(self, argarray, tag="", minorversion=0):
        """A Compound call"""
        if not tag and self.debugtags:
            tag = str(get_callstack())

        compoundargs = COMPOUND4args(self, argarray=argarray, tag=tag, minorversion=minorversion)
        res = COMPOUND4res(self)

        # Save sent operations for later checks
        sent_operations = [op.argop for op in argarray]
        try:
		self.make_call(NFSPROC4_COMPOUND, None, compoundargs.pack, res.unpack)
	except rpc.RPCUnextractedData:
		print "Warning, Unextracted Data in Reply!"

        recv_operations = [op.resop for op in res.resarray]

        # The same numbers & the same operations should be returned,
        # In case of an error, the reply is possible shorter than
        # the request.
        # FIXME: Should verify that an error indeed has occured if the response
        # is shorter
        if sent_operations[:len(recv_operations)] != recv_operations:
            raise InvalidCompoundRes("sent=%s, got=%s" \
                                     % (str(sent_operations),
                                        str(recv_operations)))
        # Check response sanity
        verify_compound_result(res)

        
        return res

    #
    # Utility methods
    #
    def gen_random_64(self):
        a = array.array('I')
        for unused in range(4):
            a.append(random.randrange(2**16))
        return a.tostring()

    def gen_uniq_id(self):
        # Use FQDN and pid as ID.
        return socket.gethostname() + str(os.getpid())

    def get_seqid(self):
        self.seqid += 1
        self.seqid = self.seqid % 2**32L
        return self.seqid

    # FIXME
    def get_owner(self):
        self.owner += 1
        self.owner = self.owner % 2**32L
        return self.owner

    def get_pathcomps_rel(self, filename):
        """Transform a unix-like pathname, relative to self.ncl,
        to a list of components. If filename is not, assume "."
        """
        # FIXME: get_pathcomps_rel(../../../fff) returns ['fff']
        if not filename:
            return self.cwd
            
        if filename[0] == "/":
            # Absolute path
            pathcomps = []
        else:
            pathcomps = self.cwd

        return unixpath2comps(filename, pathcomps)

    def cd_dotdot(self):
        self.cwd = self.cwd[:-1]

    def try_cd(self, dir):
        # FIXME: Better error messages. 
        candidate_cwd = unixpath2comps(dir, self.cwd)
        lookupops = self.lookup_path(candidate_cwd)
        operations = [self.putrootfh_op()] + lookupops
        getattrop = self.getattr([FATTR4_TYPE])
        operations.append(getattrop)

        try:
            res = self.compound(operations)
            check_result(res)
            obj_type = opaque2long(res.resarray[-1].arm.arm.obj_attributes.attr_vals)
            if not obj_type == NF4DIR:
                raise ChDirError(dir)
            
        except rpc.RPCException:
            raise ChDirError(dir)

        self.cwd = candidate_cwd

    # 
    # Operations. These come in two flawors: <operation>_op and <operation>.
    #
    # <operation>_op: This is just a wrapper which creates a
    # nfs_argop4.  The arguments for the method <operation>_op should
    # be the same as the arguments for <operation>4args. No default
    # arguments or any other kind of intelligent handling should be
    # done in the _op methods.
    #
    # <operation>: This is a convenience method. It can have default arguments 
    # and operation-specific arguments. Not all operations have <operation>
    # methods. It's pretty useless for operations without arguments, for example.
    # Eg., if the <operation> method doesn't do anything, it should not exist. 
    #
    # The _op method should be defined first. Look at read_op and read for an
    # example.
    #
    # 
    def access_op(self, access):
	args = ACCESS4args(self, access)
	return nfs_argop4(self, argop=OP_ACCESS, opaccess=args)

    def close_op(self, seqid, stateid):
        args = CLOSE4args(self, seqid, stateid)
        return nfs_argop4(self, argop=OP_CLOSE, opclose=args)

    def commit_op(self, offset, count):
        args = COMMIT4args(self, offset, count)
        return nfs_argop4(self, argop=OP_COMMIT, opcommit=args)

    def create_op(self, objtype, objname, createattrs):
        args = CREATE4args(self, objtype, objname, createattrs)
        return nfs_argop4(self, argop=OP_CREATE, opcreate=args)

    def create(self, objtype, objname):
        """CREATE with no attributes"""
        createattrs = fattr4(self, [], "")
        return self.create_op(objtype, objname, createattrs)

    def delegpurge_op(self, clientid):
        args = DELEGPURGE4args(self, clientid)
        return nfs_argop4(self, argop=OP_DELEGPURGE, opdelegpurge=args)

    def delegreturn_op(self, deleg_stateid):
        args = DELEGRETURN4args(self, deleg_stateid)
        return nfs_argop4(self, argop=OP_DELEGRETURN, opdelegreturn=args)

    def getattr_op(self, attr_request):
	args = GETATTR4args(self, attr_request)
        return nfs_argop4(self, argop=OP_GETATTR, opgetattr=args)

    def getattr(self, attrlist=[]):
	# The argument to GETATTR4args is a list of integers.
	return self.getattr_op(list2attrmask(attrlist))

    def getfh_op(self):
        return nfs_argop4(self, argop=OP_GETFH)

    def link_op(self, newname):
        args = LINK4args(self, newname)
        return nfs_argop4(self, argop=OP_LINK, oplink=args)

    def lock_op(self, locktype, reclaim, offset, length, locker):
        args = LOCK4args(self, locktype, reclaim, offset, length, locker)
        return nfs_argop4(self, argop=OP_LOCK, oplock=args)

    def lockt_op(self, locktype, offset, length, owner):
        args = LOCKT4args(self, locktype, offset, length, owner)
        return nfs_argop4(self, argop=OP_LOCKT, oplockt=args)

    def locku_op(self, locktype, seqid, lock_stateid, offset, length):
        args = LOCKU4args(self, locktype, seqid, lock_stateid, offset, length)
        return nfs_argop4(self, argop=OP_LOCKU, oplocku=args)

    def lookup_op(self, objname):
	args = LOOKUP4args(self, objname)
	return nfs_argop4(self, argop=OP_LOOKUP, oplookup=args)

    def lookup_path(self, pathcomps):
        """Generate a list of lookup operations from path components"""
        lookupops = []
        for component in pathcomps:
            lookupops.append(self.lookup_op(component))
        return lookupops

    def lookupp_op(self):
	return nfs_argop4(self, argop=OP_LOOKUPP)

    def nverify_op(self, obj_attributes):
        args = NVERIFY4args(self, obj_attributes)
	return nfs_argop4(self, argop=OP_NVERIFY, opnverify=args)

    def open_op(self, seqid, share_access, share_deny, owner, openhow, claim):
	args = OPEN4args(self, seqid, share_access, share_deny, owner, openhow, claim)
        return nfs_argop4(self, argop=OP_OPEN, opopen=args)

    # Convenience method for open. Only handles claim type CLAIM_NULL. If you want
    # to use other claims, use open_op directly. 
    def open(self, file, opentype=OPEN4_NOCREATE,
             # For OPEN4_CREATE
             mode=UNCHECKED4, createattrs=None, createverf=None,
             # Shares
             share_access=OPEN4_SHARE_ACCESS_READ, share_deny=OPEN4_SHARE_DENY_NONE):

        # claim
        claim = open_claim4(self, CLAIM_NULL, file)

        # openhow
        if mode in [UNCHECKED4, GUARDED4] and not createattrs:
            # FIXME: Consider using local umask as default mode. 
            #mask = os.umask(0)
            #os.umask(mask)
            attr_request = list2attrmask([])
            createattrs = fattr4(self, attr_request, "")
        
        how = createhow4(self, mode, createattrs, createverf)
        openhow = openflag4(self, opentype, how)

        # owner
        #ownerstring = pwd.getpwuid(os.getuid())[0]
        # FIXME
        owner = open_owner4(self, self.clientid, long2opaque(self.get_owner()))

        # seqid
        seqid = self.get_seqid()
        
        return self.open_op(seqid, share_access, share_deny, owner, openhow, claim)

        
    def openattr_op(self, createdir):
        args = OPENATTR4args(self, createdir)
        return nfs_argop4(self, argop=OP_OPENATTR, opopenattr=args)

    def open_confirm_op(self, open_stateid, seqid):
        args = OPEN_CONFIRM4args(self, open_stateid, seqid)
        return nfs_argop4(self, argop=OP_OPEN_CONFIRM, opopen_confirm=args)

    def open_downgrade_op(self, open_stateid, seqid, share_access, share_deny):
        args = OPEN_DOWNGRADE4args(self, open_stateid, seqid, share_access, share_deny)
        return nfs_argop4(self, argop=OP_OPEN_DOWNGRADE, opopen_downgrade=args)

    def putfh_op(self, object):
        args = PUTFH4args(self, object)
        return nfs_argop4(self, argop=OP_PUTFH, opputfh=args)

    def putpubfh_op(self):
        return nfs_argop4(self, argop=OP_PUTPUBFH)

    def putrootfh_op(self):
        return nfs_argop4(self, argop=OP_PUTROOTFH)

    def read_op(self, stateid, offset, count):
	args = READ4args(self, stateid, offset, count)
	return nfs_argop4(self, argop=OP_READ, opread=args)

    def read(self, stateid, offset=0, count=0):
	return self.read_op(stateid, offset, count)

    def readdir_op(self, cookie, cookieverf, dircount, maxcount, attr_request):
	args = READDIR4args(self, cookie, cookieverf, dircount, maxcount, attr_request)
	return nfs_argop4(self, argop=OP_READDIR, opreaddir=args)

    def readdir(self, cookie=0, cookieverf="", dircount=4096, maxcount=4096, attr_request=[]):
	return self.readdir_op(cookie, cookieverf, dircount, maxcount, attr_request)

    def readlink_op(self):
        return nfs_argop4(self, argop=OP_READLINK)

    def remove_op(self, target):
        args = REMOVE4args(self, target)
        return nfs_argop4(self, argop=OP_REMOVE, opremove=args)

    def rename_op(self, oldname, newname):
        args = RENAME4args(self, oldname, newname)
        return nfs_argop4(self, argop=OP_RENAME, oprename=args)

    def renew_op(self, clientid):
        args = RENEW4args(self, clientid)
        return nfs_argop4(self, argop=OP_RENEW, oprenew=args)

    def restorefh_op(self):
        return nfs_argop4(self, argop=OP_RESTOREFH)

    def savefh_op(self):
        return nfs_argop4(self, argop=OP_SAVEFH)

    def secinfo_op(self, name):
        args = SECINFO4args(self, name)
        return nfs_argop4(self, argop=OP_SECINFO, opsecinfo=args)

    def setattr_op(self, stateid, obj_attributes):
        args = SETATTR4args(self, stateid, obj_attributes)
        return nfs_argop4(self, argop=OP_SETATTR, opsetattr=args)

# To handle .x changes seamlessly, and because pynfs current doesnt handle
# call backs anyways, we insert this change.
    def setclientid_op(self, client, callback, callback_ident=2):
# for nfs4.x > 1.106
	args = SETCLIENTID4args(self, client, callback, callback_ident)
# for nfs4.x < 1.109
#	args = SETCLIENTID4args(self, client, callback)
	return nfs_argop4(self, argop=OP_SETCLIENTID, opsetclientid=args)

    def setclientid(self, verifier=None, id=None, cb_program=None, r_netid=None, r_addr=None, cb_ident=None):
        if not verifier:
            self.verifier = self.gen_random_64()
        else:
            self.verifier = verifier

        if not id:
            id = self.gen_uniq_id()

        if not cb_program:
            # FIXME
            cb_program = 0
	if not cb_ident:
	    # FIXME
	    cb_ident = 0
	    
        if not r_netid:
            # FIXME
            r_netid = "udp"

        if not r_addr:
            # FIXME
            r_addr = socket.gethostname()
        
        client_id = nfs_client_id4(self, verifier=self.verifier, id=id)
        cb_location = clientaddr4(self, r_netid=r_netid, r_addr=r_addr)
        callback = cb_client4(self, cb_program=cb_program, cb_location=cb_location)

	return self.setclientid_op(client_id, callback, cb_ident)

    def setclientid_confirm_op(self, clientid, setclientid_confirm):
	args = SETCLIENTID_CONFIRM4args(self, clientid, setclientid_confirm)
        return nfs_argop4(self, argop=OP_SETCLIENTID_CONFIRM, opsetclientid_confirm=args)

    def verify_op(self, obj_attributes):
        args = VERIFY4args(self, obj_attributes)
	return nfs_argop4(self, argop=OP_VERIFY, opverify=args)

    def write_op(self, stateid, offset, stable, data):
	args = WRITE4args(self, stateid, offset, stable, data)
	return nfs_argop4(self, argop=OP_WRITE, opwrite=args)

    def write(self, data, stateid, offset=0, stable=FILE_SYNC4):
        return self.write_op(stateid, offset, stable, data)

    def cb_getattr(self):
        # FIXME
        raise NotImplementedError()

    def cb_recall(self):
        # FIXME
        raise NotImplementedError()

    
    #
    # NFS convenience methods. Calls server. 
    #
    def init_connection(self):
        # SETCLIENTID
        setclientidop = self.setclientid()
        res = self.compound([setclientidop])

        check_result(res)
        
        self.clientid = res.resarray[0].arm.arm.clientid
	setclientid_confirm = res.resarray[0].arm.arm.setclientid_confirm
        # SETCLIENTID_CONFIRM
        setclientid_confirmop = self.setclientid_confirm_op(self.clientid, setclientid_confirm)
        res = self.compound([setclientid_confirmop])

        check_result(res)

    # def do_access

    def do_close(self, fh, stateid):
        seqid = self.get_seqid()
        putfhop = self.putfh_op(fh)
        closeop = self.close_op(seqid, stateid)
        res = self.compound([putfhop, closeop])
        check_result(res)

        return res.resarray[1].arm.open_stateid

    # def do_commit
    # def do_create
    # def do_delegpurge
    # def do_delegreturn
    # def do_getattr
    
    def do_getfh(self, pathcomps):
        """Get filehandle"""
        lookupops = self.lookup_path(pathcomps)
        operations = [self.putrootfh_op()] + lookupops
        operations.append(self.getfh_op())
        res = self.compound(operations)
        check_result(res)
        return res.resarray[-1].arm.arm.object
    
    # def do_link
    # def do_lock
    # def do_lockt
    # def do_locku
    # def do_lookup
    # def do_lookupp
    # def do_nverify
    # def do_open
    # def do_openattr
    # def do_open_confirm
    # def do_open_downgrade
    # def do_putfh 
    # def do_putpubfh
    # def do_putrootfh

    def do_read(self, stateid, fh, offset=0, size=None):
        putfhop = self.putfh_op(fh)

        data = ""
        while 1:
            readop = self.read(stateid, count=BUFSIZE, offset=offset)
            res = self.compound([putfhop, readop])
            check_result(res)
            data += res.resarray[1].arm.arm.data
            
            if res.resarray[1].arm.arm.eof:
                break

            # Have we got as much as we were asking for?
            if size and (len(data) >= size):
                break

            offset += BUFSIZE

        if size:
            return data[:size]
        else:
            return data

    def do_read_fast(self, fh, offset=0, size=None):
        """Fast implementation of do_read"""
        # FIXME: broken. 

        def fast_pack(args):
            (ncl, fh, offset) = args
            # No compound tag; zerolength opaque. 
            ncl.packer.pack_uint(0)
            # Minor version
            ncl.packer.pack_uint32_t(0)

            # Number of operations
            ncl.packer.pack_uint(2)

            # PUTFH
            ncl.packer.pack_nfs_opnum4(OP_PUTFH)
            ncl.packer.pack_opaque(fh)

            # READ
            ncl.packer.pack_nfs_opnum4(OP_READ)
            ncl.packer.pack_stateid4(0)
            ncl.packer.pack_offset4(offset)
            ncl.packer.pack_count4(BUFSIZE)

        def fast_unpack(ncl):
            status = ncl.unpacker.unpack_nfsstat4()
            if status:
                raise BadCompoundRes(OP_READ, status)
                
            # Tag
            ncl.unpacker.unpack_opaque()

            # resarray
            unused = ncl.unpacker.unpack_uint()

            # PUTFH result
            unused_argop = ncl.unpacker.unpack_nfs_opnum4()
            status = ncl.unpacker.unpack_nfsstat4()

            # READ result
            unused_argop = ncl.unpacker.unpack_nfs_opnum4()
            status = ncl.unpacker.unpack_nfsstat4()
            eof = ncl.unpacker.unpack_bool()
            data = ncl.unpacker.unpack_opaque()
            
            return (eof, data)
            
        def custom_make_call(ncl, proc, pack_func, unpack_func,
                             pack_args=None, unpack_args=None):
            """customized rpc.make_call with possible argument to unpack_func"""
            if pack_func is None and pack_args is not None:
                raise TypeError("non-null pack_args with null pack_func")
            ncl.start_call(proc)
            if pack_func:
                    pack_func(pack_args)
            ncl.do_call()
            if unpack_func:
                    result = unpack_func(unpack_args)
            else:
                    result = None
            ncl.unpacker.done()
            return result


        data = ""

        while 1:
            (eof, got_data) = custom_make_call(self, 1, fast_pack, fast_unpack,
                                               (self, fh, offset), self)
            data += got_data
            
            if eof:
                break

            # Have we got as much as we were asking for?
            if size and (len(data) >= size):
                break

            offset += BUFSIZE

        if size:
            return data[:size]
        else:
            return data


    def do_readdir(self, fh, attr_request=[]):
	# Since we may not get whole directory listing in one readdir request,
	# loop until we do. For each request result, create a flat list
	# with <entry4> objects. 
	cookie = 0
	cookieverf = ""
	entries = []
	while 1:
	    putfhop = self.putfh_op(fh)
	    readdirop = self.readdir(cookie, cookieverf, attr_request=attr_request)
	    res = self.compound([putfhop, readdirop])
	    check_result(res)

            reply = res.resarray[1].arm.arm.reply
            if not reply.entries:
                break

	    entry = reply.entries[0]

            # Loop over all entries in result. 
	    while 1:
		entries.append(entry)
		if not entry.nextentry:
		    break
		entry = entry.nextentry[0]
	    
	    if res.resarray[1].arm.arm.reply.eof:
		break

            cookie = entry.cookie
	    cookieverf = res.resarray[1].arm.arm.cookieverf

	return entries

    # def do_readlink
    
    def do_remove(self, pathcomps):
        # Lookup all but last component
        lookupops = self.lookup_path(pathcomps[:-1])
        operations = [self.putrootfh_op()] + lookupops
        operations.append(self.remove_op(pathcomps[-1]))
        res = self.compound(operations)
        check_result(res)
    
    # def do_rename
    # def do_renew
    # def do_restorefh
    # def do_savefh
    # def do_secinfo
    # def do_setattr
    # def do_setclientid
    # def do_setclientid_confirm
    # def do_verify

    def do_write(self, fh, data, stateid, offset=0, stable=FILE_SYNC4):
        putfhop = self.putfh_op(fh)
	writeop = self.write(data, stateid, offset=offset, stable=stable)
	res = self.compound([putfhop, writeop])
	check_result(res)

    #
    # Misc. convenience methods. 
    #
    def get_ftype(self, pathcomps):
        """Get file type attribute"""
        lookupops = self.lookup_path(pathcomps)
        operations = [self.putrootfh_op()] + lookupops
        getattrop = self.getattr([FATTR4_TYPE])
        operations.append(getattrop)

        res = self.compound(operations)
        check_result(res)
        obj_type = opaque2long(res.resarray[-1].arm.arm.obj_attributes.attr_vals)

        return obj_type
        
#
# Misc. helper functions. 
#
def check_result(compoundres):
    """Verify that a COMPOUND call was successful,
    raise BadCompoundRes otherwise
    """
    if not compoundres.status:
        return

    # If there was an error, it should be the last operation. 
    resop = compoundres.resarray[-1]
    raise BadCompoundRes(resop.resop, resop.arm.status)

def verify_compound_result(res):
    """Check that COMPOUND result is sane, in every way

    Raises InvalidCompoundRes on error

    There is usually no need to use this function explicitly, since compound()
    method always does that automatically. 
    """
    if res.status == NFS4_OK:
        # All operations status should also be NFS4_OK
        # Note: A zero-length res.resarray is possible
        for resop in res.resarray:
            if resop.arm.status != NFS4_OK:
                raise InvalidCompoundRes("res.status was OK, but some operations"
                                         "returned errors")
    else:
        # Note: A zero-length res.resarray is possible
        if res.resarray:
            # All operations up to the last operation returned should be NFS4_OK
            for resop in res.resarray[:-1]:
                if resop.arm.status != NFS4_OK:
                    raise InvalidCompoundRes("non-last operations returned error")

            # The last operation result must be equal to res.status
            lastop = res.resarray[-1]
            if lastop.arm.status != res.status:
                raise InvalidCompoundRes("last op not equal to res.status")

def unixpath2comps(str, pathcomps=None):
    if pathcomps == None:
        pathcomps = []
    if str[0] == "/":
        pathcomps = []
    else:
        pathcomps = pathcomps[:]
    for component in str.split("/"):
        if (component == "") or (component == "."):
            pass
        elif component == "..":
            pathcomps = pathcomps[:-1]
        else:
            pathcomps.append(component)
    return pathcomps

def comps2unixpath(comps):
    result = ""
    for component in comps:
        result += "/" + component
    return result

def opaque2long(data):
    import struct
    result = 0L
    # Decode 4 bytes at a time. 
    for intpos in range(len(data)/4):
	integer = data[intpos*4:intpos*4+4]
	val = struct.unpack(">L", integer)[0]
	shiftbits = (len(data)/4 - intpos - 1)*64
	result = result | (val << shiftbits)

    return result

def long2opaque(integer, pad_to=None):
    import struct
    # Make sure we are dealing with longs.
    l = long(integer)
    result = ""
    # Encode 4 bytes at a time.
    while l:
        lowest_bits = l & 0xffffffffL
        l = l >> 32
        result = struct.pack(">L", lowest_bits) + result

    if pad_to:
        if len(result) < pad_to:
            pad_bytes = "\x00" * (pad_to - len(result))
            result = pad_bytes + result

    return result

def intlist2long(intlist):
    # Make sure we are dealing with longs.
    # (unpack_uint in xdrlib returns an integer if possible, a long otherwise.)
    intlist = map(lambda x: long(x), intlist)

    result = 0L
    for intpos in range(len(intlist)):
        integer = intlist[intpos]
        shiftbits = intpos * 32
        result = result | (integer << shiftbits)
    
    return result

def int2binstring(val):
    numbits = 32
    if type(val) == type(1L):
        numbits = 64

    result = ""
    for bitpos in range(numbits-1, -1, -1):
        bitval = 1L << bitpos
        if bitval & val:
            result += "1"
        else:
            result += "0"
    return result


def get_attrbitnum_dict():
    """Get dictionary with attribute bit positions.

    Note: This function uses introspection. It will fail if nfs4constants.py has
    an attribute named FATTR4_<something>. 

    Returns {"fattr4_type": 1, "fattr4_change": 3 ...}
    """
    
    import nfs4constants
    attrbitnum_dict = {}
    for name in dir(nfs4constants):
        if name.startswith("FATTR4_"):
            value = getattr(nfs4constants, name)
            # Sanity checking. Must be integer. 
            assert(type(value) == type(0))
	    attrname = name[7:].lower()
	    attrbitnum_dict[attrname] = value
	    
    return attrbitnum_dict

def get_bitnumattr_dict():
    """Get dictionary with attribute bit positions.
    
    Note: This function uses introspection. It will fail if nfs4constants.py has

    an attribute named FATTR4_<something>. 

    Returns { 1: "fattr4_type", 3: "fattr4_change", ...}
    """
    
    import nfs4constants
    bitnumattr_dict = {}
    for name in dir(nfs4constants):
        if name.startswith("FATTR4_"):
            value = getattr(nfs4constants, name)
            # Sanity checking. Must be integer. 
            assert(type(value) == type(0))
            attrname = name[7:].lower()
            bitnumattr_dict[value] = attrname
          
    return bitnumattr_dict


def get_attrunpackers(unpacker):
    """Get dictionary with attribute unpackers

    Note: This function uses introspection. It depends on that nfs4packer.py
    has methods for every unpacker.unpack_fattr4_<attribute>.

    """
    import nfs4packer
    attrunpackers = {}
    for name in dir(nfs4packer.NFS4Unpacker):
	if name.startswith("unpack_fattr4_"):
            # unpack_fattr4_ is 14 chars. 
	    attrname = name[14:]
	    attrunpackers[attrname] = getattr(unpacker, name)

    return attrunpackers


def get_attrpackers(packer):
    """Get dictionary with attribute packers

    Note: This function uses introspection. It depends on that nfs4packer.py
    has methods for every packer.pack_fattr4_<attribute>.
    """
    import nfs4packer
    attrpackers = {}
    dict = get_attrbitnum_dict()
    for name in dir(nfs4packer.NFS4Packer):
        if name.startswith("pack_fattr4_"):
            # pack_fattr4 is 12 chars. 
            attrname = name[12:]
            attrpackers[dict[attrname]] = getattr(packer, name)

    return attrpackers

def dict2fattr(dict, ncl):
    """Convert a dictionary to a fattr4 object.

    Returns a fattr4 object.  
    """

    attrs = dict.keys()
    attrs.sort()

    attr_vals = ""
    rstncl = DummyNcl() 
    import nfs4packer;
    packer = nfs4packer.NFS4Packer(rstncl)
    attrpackers = get_attrpackers(packer) 
    
    for attr in attrs:
        value = dict[attr]
	if type(value) == ListType:
		packer.reset()
		packer.pack_uint(len(value))
		for item in value:
			item.packer = packer
			item.pack()
	elif type(value) <> InstanceType:
		packerfun = attrpackers[attr];
		packer.reset()
		packerfun(value)
	else:
		packer.reset()
		value.packer = packer
		value.pack()
	attr_vals += packer.get_buffer()
    attrmask = list2attrmask(attrs)
    return fattr4(ncl, attrmask, attr_vals); 



def fattr2dict(obj):
    """Convert a fattr4 object to a dictionary with attribute name and values.

    Returns a dictionary like {"size": 4711}
    """

    attrbitnum_dict = get_attrbitnum_dict()

    # Construct a dictionary with the attributes to unpack.
    # Example: {53: 'time_modify', 4: 'size', 8: 'fsid'}
    unpack_these = {}

    # Construct one long integer from the integer list.
    attrmask = 0L
    for intpos in range(len(obj.attrmask)):
        integer = long(obj.attrmask[intpos])
        attrmask = attrmask | (integer << (intpos*32))

    # Loop over all known attributes and check if they were returned. 
    for attr in attrbitnum_dict.keys():
        bitnum = attrbitnum_dict[attr]
        bitvalue = 1L << bitnum
        if bitvalue & attrmask:
            unpack_these[bitnum] = attr

    # Construct a dummy Client. 
    ncl = DummyNcl(obj.attr_vals)

    result = {}
    attrunpackers = get_attrunpackers(ncl.unpacker)
    bitnums_to_unpack = unpack_these.keys()
    # The data on the wire is ordered according to attribute bit number. 
    bitnums_to_unpack.sort()
    for bitnum in bitnums_to_unpack:
	attrname = unpack_these[bitnum]
	unpack_method = attrunpackers[attrname]
	result[attrname] = unpack_method()

    return result


def list2attrmask(attrlist):
    """Construct a bitmap4 attrmask from a list of attribute constants"""
    attr_request = []
    for attr in attrlist:
        # Lost? Se section 2.2 in RFC3010. 
        arrintpos = attr / 32
        bitpos = attr % 32

        while (arrintpos+1) > len(attr_request):
            attr_request.append(0)

        arrint = attr_request[arrintpos]
        arrint = arrint | (1L << bitpos)
        attr_request[arrintpos] = arrint
    return attr_request

def attrmask2list(attrmask):
    """Construct a list of attribute constants from the bitmap4 attrmask.
    This is intended as the conjugate function to list2attrmask."""
    offset = 0;
    attrs = [];
    for uint in attrmask:
        for bit in range(32):
            value = 1L << bit
            if uint & value:
                attrs.append(bit+offset*32)
        offset += 1
    return attrs

def attrmask2attrs(attrmask):
    """Construct a list of attribute constants from the bitmap4 attrmask.
    This is intended as the conjugate function to list2attrmask."""
    offset = 0;
    attrs = [];
    mapping = get_bitnumattr_dict();
    for uint in attrmask:
        for bit in range(32):
            value = 1L << bit
            if uint & value:
                attrs.append(mapping[bit+offset*32])
        offset += 1
    return attrs


def get_callstack_full():
    """Return a list with call stack, using traceback.extract_stack

    The last frame (this function) is omitted
    """
    import sys, traceback
    try:
        raise ZeroDivisionError
    except ZeroDivisionError:
        f = sys.exc_info()[2].tb_frame.f_back
        return traceback.extract_stack(f)[:-1]


def get_callstack():
    """Just as get_callstack_full, but omit the statement
    """
    l = get_callstack_full()
    without_stmt = [(x[0], x[1], x[2]) for x in l]
    return without_stmt[:-1]

def parse_nfs_url(s):
    """Parse NFS URL

    Returns a tuple of (host, port, directory) on success, None otherwise. 
    """
    # FIXME: _ should not be allowed. Should confirm to RFC2224 or
    # similiar.
    match = re.search(r'^(?:nfs://)?(?P<host>([a-zA-Z][\w\.^-]*|\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}))'
                      r'(?::(?P<port>\d*))?(?P<dir>/[\w/]*)?$', s)

    if match:
        return (match.group("host"), match.group("port"), match.group("dir"))
    else:
        return None

def create_client(host, port, transport, **kwargs):
    """Create instance of TCPNFS4Client or UDPNFS4Client, depending on
    given transport. transport should be tcp, udp or auto. When auto, TCP is
    tried first, then UDP. 
    """
    if transport == "auto":
        # Try TCP first, then UDP, according to RFC2224
        try:
            ncl = TCPNFS4Client(host, port, **kwargs)
        except socket.error:
            print "TCP Connection refused, trying UDP"
            ncl = UDPNFS4Client(host, port, **kwargs)
    elif transport == "tcp":
        ncl = TCPNFS4Client(host, port, **kwargs)
    elif transport == "udp":
        ncl = UDPNFS4Client(host, port, **kwargs)
    else:
        raise RuntimeError, "Invalid protocol"

    return ncl


class UDPNFS4Client(PartialNFS4Client, rpc.RawUDPClient):
    def __init__(self, host, port=NFS_PORT, uid=os.getuid(), gid=os.getgid()):
        rpc.RawUDPClient.__init__(self, host, NFS4_PROGRAM, NFS_V4, port)
        PartialNFS4Client.__init__(self)
        self.uid = uid
        self.gid = gid
        

class TCPNFS4Client(PartialNFS4Client, rpc.RawTCPClient):
    def __init__(self, host, port=NFS_PORT, uid=os.getuid(), gid=os.getgid()):
        rpc.RawTCPClient.__init__(self, host, NFS4_PROGRAM, NFS_V4, port)
        PartialNFS4Client.__init__(self)
        self.uid = uid
        self.gid = gid


class NFS4OpenFile:
    __pychecker__ = 'no-classattr'
    """Emulates a Python file object.
    """
    # BUGS: If pos is set beyond file size and data is later written,
    # we should fill in zeros. 
    def __init__(self, ncl):
        self.ncl = ncl
        self.__set_priv("closed", 1)
        self.__set_priv("mode", "")
        self.__set_priv("name", "")
        self.softspace = 0
        self.pos = 0L
        # NFS4 file handle. 
        self.fh = None
        # NFS4 stateid
        self.stateid = None

    def __setattr__(self, name, val):
        if name in ["closed", "mode", "name"]:
            raise TypeError("read only attribute")
        else:
            self.__set_priv(name, val)

    def __set_priv(self, name, val):
        self.__dict__[name] = val

    def open(self, filename, mode="r", unused_bufsize=BUFSIZE):
        # filename is a normal unix-path, relative to self.ncl.cwd.
        operations = [self.ncl.putrootfh_op()]
	pathcomps = self.ncl.get_pathcomps_rel(filename)
        operations.extend(self.ncl.lookup_path(pathcomps[:-1]))
        filename = pathcomps[-1]

	if mode == "r":
            openop = self.ncl.open(file=filename)
            operations.append(openop)
	elif mode == "w":
            # Truncate upon creation. 
            attr_request = list2attrmask([FATTR4_SIZE])
            createattrs = fattr4(self.ncl, attr_request, '\x00' * 8)
            openop = self.ncl.open(file=filename, share_access=OPEN4_SHARE_ACCESS_WRITE,
                                   opentype=OPEN4_CREATE, createattrs=createattrs)
            operations.append(openop)
	else:
	    # FIXME: More modes allowed. 
	    raise TypeError("Invalid mode")
	
        operations.append(self.ncl.getfh_op())
        res = self.ncl.compound(operations)

        check_result(res)
        
        self.__set_priv("closed", 0)
        self.__set_priv("mode", mode)
        self.__set_priv("name", os.path.join(os.sep, *pathcomps))
        # Get stateid from OPEN
        self.stateid = res.resarray[-2].arm.arm.stateid
        # Get flags
        rflags = res.resarray[-2].arm.arm.rflags
        
        # Get filehandle from GETFH
        self.fh = res.resarray[-1].arm.arm.object

        if rflags & OPEN4_RESULT_CONFIRM:
            # Confirm open
            putfhop = self.ncl.putfh_op(self.fh)
            seqid = self.ncl.get_seqid()
            opconfirm = self.ncl.open_confirm_op(self.stateid, seqid)
            res = self.ncl.compound([putfhop, opconfirm])
            check_result(res)
            # Save new stateid
            self.stateid = res.resarray[-1].arm.arm.open_stateid

    def close(self):
        if not self.closed:
            self.__set_priv("closed", 1)
            self.ncl.do_close(self.fh, self.stateid)

    def flush(self):
        if self.closed:
            raise ValueError("I/O operation on closed file")
        raise NotImplementedError()

    # isatty() should not be implemented.

    # fileno() should not be implemented.

    def read(self, size=None):
        if self.closed:
            raise ValueError("I/O operation on closed file")
        data = self.ncl.do_read(self.stateid, self.fh, self.pos, size)
        # do_read_fast is about 40% faster. But:
        # FIXME: do_read_fast is currently broken. 
        # FIXME: Verify that do_read_fast is robust. 
        #data = self.ncl.do_read_fast(self.fh, self.pos, size)
        self.pos += len(data)
        return data

    def readline(self, size=None):
        if self.closed:
            raise ValueError("I/O operation on closed file")
        data = self.ncl.do_read(self.fh, self.pos, size)
        
        if data:
            line = data.split("\n", 1)[0] + "\n"
            self.pos += len(line)
            return line
        else:
            return ""

    def readlines(self, unused_sizehint=None):
        if self.closed:
            raise ValueError("I/O operation on closed file")
        data = self.ncl.do_read(self.fh, self.pos)

        self.pos += len(data)

        lines = data.split("\n")
        if lines[len(lines)-1] == "":
            lines = lines[:-1]
        
        # Append \n on all lines.
        return map(lambda line: line + "\n", lines)

    def xreadlines(self):
        if self.closed:
            raise ValueError("I/O operation on closed file")
        import xreadlines
        return xreadlines.xreadlines(self)

    def seek(self, offset, whence=0):
        if self.closed:
            raise ValueError("I/O operation on closed file")
        if whence == 0:
            # absolute file positioning)
            newpos = offset
        elif whence == 1:
            # seek relative to the current position
            newpos = self.pos + offset
        elif whence == 2:
            # seek relative to the file's end
	    putfhop = self.ncl.putfh_op(self.fh)
	    getattrop = self.ncl.getattr([FATTR4_SIZE])
	    res =  self.ncl.compound([putfhop, getattrop])
	    check_result(res)
	    size = opaque2long(res.resarray[1].arm.arm.obj_attributes.attr_vals)
	    newpos = size + offset
        else:
            raise IOError("[Errno 22] Invalid argument")
        self.pos = max(0, newpos)

    def tell(self):
        if self.closed:
            raise ValueError("I/O operation on closed file")
        return self.pos

    def truncate(self, size=None):
        if self.closed:
            raise ValueError("I/O operation on closed file")
        if not size:
            size = self.pos
        # FIXME: SETATTR can probably be used. 
        raise NotImplementedError()
        
    def write(self, data):
        if self.closed:
            raise ValueError("I/O operation on closed file")

	self.ncl.do_write(self.fh, data, stateid=self.stateid, offset=self.pos)
	self.pos += len(data)

    def writelines(self, list):
        if self.closed:
            raise ValueError("I/O operation on closed file")
    
	for line in list:
	    self.write(line)

if __name__ == "__main__":
    # Demo
    import sys
    if len(sys.argv) < 3:
        print "Usage: %s <protocol> <host>" % sys.argv[0]
        sys.exit(1)
    
    proto = sys.argv[1]
    host = sys.argv[2]
    if proto == "tcp":
        ncl = TCPNFS4Client(host)
    elif proto == "udp":
        ncl = UDPNFS4Client(host)
    else:
        raise RuntimeError, "Wrong protocol"

    # PUTROOT & GETFH
    putrootfhoperation = nfs_argop4(ncl, argop=OP_PUTROOTFH)
    getfhoperation = nfs_argop4(ncl, argop=OP_GETFH)
    res =  ncl.compound([putrootfhoperation, getfhoperation])
    fh = res.resarray[1].arm.arm.object
    print "Root filehandles is", repr(fh)


# Local variables:
# py-indent-offset: 4
# tab-width: 8
# End:
