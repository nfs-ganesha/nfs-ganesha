__pychecker__ = 'no-shadow'

import unittest
import time
import sys
import os

import rpc
from nfs4constants import *
from nfs4types import *
import nfs4lib
UID = GID = 0

# Global variables
host = None
port = None
transport = "udp"
prefix = None


#
# nfs4st base classes
#

class SkipException(Exception):
    def __init__(self, msg):
	self.msg = msg

    def __str__(self):
	return "Skipping test: %s" % self.msg

class PartialTestClient:
    __pychecker__ = 'no-classattr'      # this class is a mix-in
    
    def lookup_all_objects(self):
        """Generate a list of lists with lookup operations for all types of objects"""
        return self.lookup_objects(self.nfssuite.all_objects)

    def lookup_objects(self, objects):
        """Generate a list of lists with lookup operations for objects"""
        result = []

        for object in objects:
            result.append(self.lookup_path(object))

        return result

    def lookup_all_objects_and_sizes(self):
        return self.lookup_objects_and_sizes(self.nfssuite.all_objects)

    def lookup_objects_and_sizes(self, objects):
        """Generate a list of lists of lookup operations and object sizes"""
        obj_sizes = []
        for object in objects:
            lookupops = self.lookup_path(object)
            getattrop = self.getattr([FATTR4_SIZE])
            operations = [self.nfssuite.putrootfhop] + lookupops
            operations.append(getattrop)
            res = self.nfssuite.do_compound(operations)
            self.nfssuite.assert_OK(res)
            obj = res.resarray[-1].arm.arm.obj_attributes
            d =  nfs4lib.fattr2dict(obj)
            obj_sizes.append((lookupops, d["size"]))

        return obj_sizes

    def lookuplist2comps(self, list):
        result = []
        for op in list:
            result.append(op.arm.objname)
        return result


class UDPTestClient(PartialTestClient, nfs4lib.UDPNFS4Client):
    def __init__(self, nfssuite, host, port=None, uid=None, gid=None):
        self.nfssuite = nfssuite
        kwargs = {}
        if port: kwargs["port"] = port
        if uid != None: kwargs["uid"] = uid
        if gid != None: kwargs["gid"] = gid
        nfs4lib.UDPNFS4Client.__init__(self, host, **kwargs)


class TCPTestClient(PartialTestClient, nfs4lib.TCPNFS4Client):
    def __init__(self, nfssuite, host, port=None, uid=None, gid=None):
        self.nfssuite = nfssuite
        kwargs = {}
        if port: kwargs["port"] = port
        if uid != None: kwargs["uid"] = uid
        if gid != None: kwargs["gid"] = gid
        nfs4lib.TCPNFS4Client.__init__(self, host, **kwargs)


class NFSSuite(unittest.TestCase):
    def __init__(self, methodName='runTest'):
        unittest.TestCase.__init__(self, methodName)
        self.obj_name = None

        # Filename constants. Same order as in nfs_ftype4 enum. 
        self.regfile = nfs4lib.unixpath2comps(prefix + "/doc/README") # NF4REG
        self.dirfile = nfs4lib.unixpath2comps(prefix + "/doc") # NF4DIR
        self.blockfile = nfs4lib.unixpath2comps(prefix + "/dev/fd0") # NF4BLK
        self.charfile = nfs4lib.unixpath2comps(prefix + "/dev/ttyS0") # NF4CHR
        self.linkfile = nfs4lib.unixpath2comps(prefix + "/dev/floppy") # NF4LNK
        self.socketfile = nfs4lib.unixpath2comps(prefix + "/dev/log") # NF4SOCK
        self.fifofile = nfs4lib.unixpath2comps(prefix + "/dev/initctl") # NF4FIFO

        # FIXME: Add NF4ATTRDIR and NF4NAMEDATTR types.
        self.special_objects = [self.blockfile, self.charfile, self.socketfile, self.fifofile]
        # Note named attribute dir or named attributes are not included in all_objects. 
        self.all_objects = self.special_objects + [self.regfile, self.dirfile, self.linkfile]

        # FIXME: Add sample named attribute.
        self.tmp_dir = nfs4lib.unixpath2comps(prefix + "/tmp")

        # Some more filenames
        self.hello_c = nfs4lib.unixpath2comps(prefix + "/src/hello.c")
        self.dirsymlinkfile = nfs4lib.unixpath2comps(prefix + "/src/doc")
        self.docporting = nfs4lib.unixpath2comps(prefix + "/doc/porting")
        
        # Should not exist
        self.vaporfilename = "vapor_object"
        self.vaporfile = nfs4lib.unixpath2comps(self.vaporfilename)
        # Not accessable
        self.notaccessibledir = nfs4lib.unixpath2comps(prefix + "/private")
        self.notaccessablefile = nfs4lib.unixpath2comps(prefix + "/private/info.txt")

    def create_client(self, uid, gid):
        if transport == "tcp":
            ncl = TCPTestClient(self, host, port, uid, gid)
        elif transport == "udp":
            ncl = UDPTestClient(self, host, port, uid, gid)
        else:
            raise RuntimeError, "Invalid protocol"
        return ncl

    def connect(self):
        self.ncl = self.create_client(UID, GID)
    
    def failIfRaises(self, excClass, callableObj, *args, **kwargs):
        """Fail if exception of excClass or EOFError is raised"""
        try:
            return apply(callableObj, args, kwargs)
        except excClass, e:
            self.fail(e)
        except EOFError, e:
            self.fail("EOFError: short response")
        return None

    def assert_OK(self, res):
        """Assert result from compound call is NFS4_OK"""
        self.assert_status(res, [NFS4_OK])

    def assert_status(self, res, errors):
        """Assert result from compound call is any of the values in errors"""
        if res.status in errors:
            return

        if res.resarray:
            lastop = res.resarray[-1]
            # res.status should be equal to lastop.arm.status
            if res.status != lastop.arm.status:
                e = nfs4lib.InvalidCompoundRes()
                self.fail(e)
            else:
                e = nfs4lib.BadCompoundRes(lastop.resop, res.status)
                self.fail(e)
        else:
            e = nfs4lib.EmptyBadCompoundRes(res.status)
            self.fail(e)

    def info_message(self, msg):
        print >> sys.stderr, "\n  " + msg + ", ",

    def do_rpc(self, method, *args, **kwargs):
        """Call method. Handle all rpc.RPCExceptions as failures"""
        return self.failIfRaises(rpc.RPCException, method, *args, **kwargs)

    def do_compound(self, *args, **kwargs):
        """Call ncl.compound. Handle all rpc.RPCExceptions as failures

        Uses self.ncl as default client, or other client specified via keyword
        argument "ncl". 
        """
        ncl = kwargs.get("ncl")
        if ncl:
            del kwargs["ncl"]
        else:
            ncl = self.ncl
        return self.do_rpc(ncl.compound, *args, **kwargs)

    def setUp(self):
        # Note: no network communication should be done in this method. 
        self.connect()
        self.putrootfhop = self.ncl.putrootfh_op()

    def init_connection(self, ncl=None):
        if not ncl: ncl=self.ncl
        self.failIfRaises(rpc.RPCException, ncl.init_connection)

    def get_invalid_fh(self):
        """Return a (guessed) invalid filehandle"""
        return nfs4lib.long2opaque(123456780, NFS4_FHSIZE/8)

    def get_invalid_verifier(self):
        """Return a (guessed) invalid verifier"""
        return nfs4lib.long2opaque(123456780, NFS4_VERIFIER_SIZE/8)

    def get_invalid_clientid(self):
        """Return a (guessed) invalid clientid"""
        return 0x1234567890L

    def get_invalid_utf8strings(self):
        """Return a list of invalid ISO10646-UTF-8 strings"""
        # FIXME: More invalid strings. Do equivalence partitioning on ISO10646-UTF8 definition
        return ["\xc0\xc1",
                "\xe0\x8a"]

    def remove_object(self, name=None, directory=None):
        """Remove object in /tmp, if it exists. Return false on failure.
        object defaults to self.obj_name
        """
        if not name:
            name = self.obj_name
        if not directory:
            directory = self.tmp_dir
        lookup_dir_ops = self.ncl.lookup_path(directory)
        operations = [self.ncl.putrootfh_op()] + lookup_dir_ops
        operations.append(self.ncl.remove_op(name))

        res = self.do_compound(operations)
        status = res.status in [NFS4_OK, NFS4ERR_NOENT]
        if not status:
            self.info_message("Cannot prepare by removing object, skipping test")
        return status

    def clean_dir(self, directory):
        """Clean directory. Raises SkipException on failure"""
        fh = self.do_rpc(self.ncl.do_getfh, directory)
        entries = self.do_rpc(self.ncl.do_readdir, fh)
        
        names = [entry.name for entry in entries]

        for name in names:
            lookup_dir_ops = self.ncl.lookup_path(directory)
            operations = [self.ncl.putrootfh_op()] + lookup_dir_ops
            operations.append(self.ncl.remove_op(name))
            res = self.do_compound(operations)

            if res.status == NFS4ERR_NOTEMPTY:
                # Recursive deletion
                self.clean_dir(directory + [name])
            elif not res.status == NFS4_OK:
                raise SkipException("Cannot clean directory %s" % directory)

        # Verify that all files were removed
        entries = self.do_rpc(self.ncl.do_readdir, fh)
        if entries:
            raise SkipException("Cannot clean directory %s" % directory)

    def create_object(self, name=None, directory=None):
        """Create (dir) object in /tmp, if it does not exist. Return false on failure.
        object defaults to self.obj_name
        """
        # We create a directory, because it's simple.
        if not name:
            name = self.obj_name
        if not directory:
            directory = self.tmp_dir
        lookup_dir_ops = self.ncl.lookup_path(directory)
        operations = [self.putrootfhop] + lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4DIR)
        operations.append(self.ncl.create(objtype, name))
        
        res = self.do_compound(operations)
        status = res.status in [NFS4_OK, NFS4ERR_EXIST]
        if not status:
            self.info_message("Cannot prepare by creating object, skipping test")
        return status

    def make_sure_nonexistent(self, name=None, directory=None):
        """Make sure file does not exist. A READDIR is done. If file is not
        found in READDIR, return. If file is found in READDIR results, try to
        remove it.

        Returns true if file did not exist or it was possible to remove it.
        Returns false if file exists and it was not possible to remove it.
        """
        if not name:
            name = self.obj_name
        if not directory:
            directory = self.tmp_dir

        fh = self.do_rpc(self.ncl.do_getfh, directory)
        
        entries = self.do_rpc(self.ncl.do_readdir, fh)
        names = [entry.name for entry in entries]
        if name in names:
            # Strange, this file already exists
            # Try to remove it.
            return self.remove_object(name, directory)
        else:
            return 1

    def create_via_create(self, dstdir, filename):
        operations = [self.putrootfhop] + self.ncl.lookup_path(self.tmp_dir)
        objtype = createtype4(self.ncl, type=NF4DIR)
        operations.append(self.ncl.create(objtype, filename))
        return operations

    def create_via_link(self, dstdir, filename):
        operations = [self.putrootfhop]

        # Lookup source and save FH
        operations.extend(self.ncl.lookup_path(self.regfile))
        operations.append(self.ncl.savefh_op())

        # Lookup target directory
        operations.append(self.putrootfhop)
        operations.extend(self.ncl.lookup_path(self.tmp_dir))

        operations.append(self.ncl.link_op(filename))
        return operations

    def create_via_open(self, dstdir, filename):
        operations = [self.putrootfhop] + self.ncl.lookup_path(self.tmp_dir)
        operations.append(self.ncl.open(file=filename,
                                        share_access=OPEN4_SHARE_ACCESS_WRITE,
                                        opentype=OPEN4_CREATE))
        
        return operations

    def create_via_rename(self, dstdir, filename):
        # Create a directory, so we have something to rename
        oldname = "object1"
        self.create_object(name=oldname)
        operations = [self.putrootfhop]

        # Lookup source and save FH
        operations.extend(self.ncl.lookup_path(self.tmp_dir))
        operations.append(self.ncl.savefh_op())

        # Lookup target directory
        operations.append(self.putrootfhop)
        operations.extend(self.ncl.lookup_path(self.tmp_dir))

        operations.append(self.ncl.rename_op(oldname, filename))

        return operations

    def accepted_filename(self, filename, remove_file=1, creator=None):
        """Returns true if file name could be created via creator.
        creator defaults to create_via_open. Files are created in /tmp
        """
        if not creator:
            creator = self.create_via_open
        
        operations = creator(self.tmp_dir, filename)
        
        res = self.do_compound(operations)
        # FIXME: Change to NFS4ERR_INVALIDNAME or what the error code will be called. 
        self.assert_status(res, [NFS4_OK, NFS4ERR_INVAL])
        creatable = (res.status == NFS4_OK)

        if creatable and remove_file:
            # Ok, the file could be created. 
            # Be nice and remove created object
            operations = [self.putrootfhop] + self.ncl.lookup_path(self.tmp_dir)
            operations.append(self.ncl.remove_op(filename))
            res = self.do_compound(operations)
            # Don't care about return status

        return creatable

    def try_file_names(self, remove_files=1, creator=None):
        """ Try all kinds of interesting file names and check if they are accepted
        (via accepted_filename). Creates files in self.tmp_dir. The optional argument
        remove_files can be unset to save created files. 

        Returns a tuple of (accepted_names, rejected_names)
        Raises SkipException if temporary directory cannot be cleared. You
        should catch it. 
        """
        if not creator:
            creator = self.create_via_open
            
        try_names = []
        rejected_names = []

        # aa<char>bb, with char 0-255
        for i in range(0, 256):
            ustr = "aa" + unichr(i) + "bb"
            try_names.append(ustr.encode("utf8"))

        # . and ..
        try_names.append(".".encode("utf8"))
        try_names.append("..".encode("utf8"))

        # C:
        try_names.append("C:".encode("utf8"))
        
        # Ok, lets try all these names.
        # Begin with cleaning /tmp
        self.clean_dir(self.tmp_dir)

        for filename in try_names[:]:
            # Do not remove the file after creation
            if not self.accepted_filename(filename, remove_file=remove_files,
                                           creator=creator):
                try_names.remove(filename)
                rejected_names.append(filename)

        return (try_names, rejected_names)
