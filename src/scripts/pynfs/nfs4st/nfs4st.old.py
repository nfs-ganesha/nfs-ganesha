#!/usr/bin/env python2

# nfs4st.py - NFS4 server tester
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
#
# Extend unittest with warnings.
#
# Handle errors such as NFS4ERR_RESOURCE and NFS4ERR_DELAY.
#
# filehandles are split into eq. classes "valid filehandle" and
# "invalid filehandle". There should probably be a class "no filehandle" as
# well. Currently, "invalid filehandle" are tested by doing operations without
# filehandles.
#
# Add testing of \ to testSlash methods.
#
# More testing of strange attributes and handling of NFS4ERR_ATTRNOTSUPP;
# more fine-grained eqv.part of attribute masks. 
#
# Nomenclature: Each test class is referred to as a "test suite". Each
# test* method is a "test case".

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


class CompoundSuite(NFSSuite):
    """Test COMPOUND procedure

    Equivalence partitioning:

    Input Condition: tag
        Valid equivalence classes:
            no tag (0)
            tag (1)
        Invalid equivalence classes:
            -
    Input Condition: minorversion
        Valid equivalence classes:
            supported minorversions(2)
        Invalid equivalence classes:
            unsupported minorversions(3)
    Input Condition: argarray
        Valid equivalence classes:
            valid operations array(4)
        Invalid equivalence classes:
            invalid operations array(5)

    """

    #
    # Testcases covering valid equivalence classes.
    #
    def testZeroOps(self):
        """Test COMPOUND without operations

        Covered valid equivalence classes: 0, 2, 4
        """
        res = self.do_compound([])
        self.assert_OK(res)

    def testWithTag(self):
        """Simple COMPOUND with tag

        Covered valid equivalence classes: 1, 2, 4
        """
        res = self.do_compound([self.putrootfhop], tag="nfs4st.py test tag")
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testInvalidMinor(self):
        """Test COMPOUND with invalid minor version

        Covered invalid equivalence classes: 3

        Comments: Also verifies that the result array after
        NFS4ERR_MINOR_VERS_MISMATCH is empty. 
        
        """
        res = self.do_compound([self.putrootfhop], minorversion=0xFFFF)
        self.assert_status(res, [NFS4ERR_MINOR_VERS_MISMATCH])
                    
        self.failIf(res.resarray, "expected empty result array after"\
                    "NFS4ERR_MINOR_VERS_MISMATCH")

    def _verify_notsupp(self, opnum, valid_opnum_fn):
        """Verify COMPOUND result for undefined operations"""
        
        # nfs4types.nfs_argop4 does not allow packing invalid operations. 
        class custom_nfs_argop4:
            def __init__(self, ncl, argop):
                self.ncl = ncl
                self.packer = ncl.packer
                self.unpacker = ncl.unpacker
                self.argop = argop
            
            def pack(self, dummy=None):
                self.packer.pack_nfs_opnum4(self.argop)

        op = custom_nfs_argop4(self.ncl, argop=opnum)
        
        try:
            # This *should* raise the BadCompoundRes exception. 
            res =  self.ncl.compound([op])
            # Ouch. This should not happen. 
            opnum = res.resarray[0].resop
            self.fail("Expected BadCompoundRes exception. INTERNAL ERROR.")
        except BadDiscriminant, e:
            # This should happen.
            self.failIf(not valid_opnum_fn(e.value),
                        "Expected result array with opnum %d, got %d" % (opnum, e.value))
            # We have to do things a little by hand here
            pos = self.ncl.unpacker.get_position()
            data = self.ncl.unpacker.get_buffer()[pos:]
            errcode = nfs4lib.opaque2long(data)
            self.failIf(errcode != NFS4ERR_NOTSUPP,
                        "Expected NFS4ERR_NOTSUPP, got %d" % errcode)
        except rpc.RPCException, e:
            self.fail(e)

    def testUndefinedOps(self):
        """COMPOUND with operations 0, 1, 2 and 1000 should return NFS4ERR_NOTSUPP

        Covered invalid equivalence classes: 5

        Comments: The server should return NFS4ERR_NOTSUPP for the
        undefined operations 0, 1 and 2. Although operation 2 may be
        introduced in later minor versions, the server should always
        return NFS4ERR_NOTSUPP if the minorversion is 0.
        """
        self._verify_notsupp(0, lambda x: x == 0)
        self._verify_notsupp(1, lambda x: x == 1)
        self._verify_notsupp(2, lambda x: x == 2)
        # For unknown operations beyound OP_WRITE, the server should return
        # the largest defined operation. It should at least be OP_WRITE!
        self._verify_notsupp(1000, lambda x: x > OP_WRITE)


class AccessSuite(NFSSuite):
    """Test operation 3: ACCESS

    Note: We do not examine if the "access" result actually corresponds to
    the correct rights. This is hard since the rights for a object can
    change at any time.

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:
    
    Input Condition: current filehandle
        Valid equivalence classes:
            link(1)
            block(2)
            char(3)
            socket(4)
            FIFO(5)
            dir(6)
            file(7)
        Invalid equivalence classes:
            invalid filehandle(8)
    Input Condition: accessreq
        Valid equivalence classes:
            valid accessreq(9)
        Invalid equivalence classes:
            invalid accessreq(10)
    """
            
    
    maxval = ACCESS4_DELETE + ACCESS4_EXECUTE + ACCESS4_EXTEND + ACCESS4_LOOKUP \
             + ACCESS4_MODIFY + ACCESS4_READ

    def valid_access_ops(self):
        result = []
        for i in range(AccessSuite.maxval + 1):
            result.append(self.ncl.access_op(i))
        return result

    def invalid_access_ops(self):
        result = []
        for i in [64, 65, 66, 127, 128, 129]:
            result.append(self.ncl.access_op(i))
        return result
    
    #
    # Testcases covering valid equivalence classes.
    #
    def testAllObjects(self):
        """ACCESS on all type of objects

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 9
        
        """
        for lookupops in self.ncl.lookup_all_objects():
            operations = [self.putrootfhop] + lookupops
            operations.append(self.ncl.access_op(ACCESS4_READ))
            res = self.do_compound(operations)
            self.assert_OK(res)
        
    def testDir(self):
        """All valid combinations of ACCESS arguments on directory

        Covered valid equivalence classes: 6, 9

        Comments: The ACCESS operation takes an uint32_t as an
        argument, which is bitwised-or'd with zero or more of all
        ACCESS4* constants. This test case tests all valid
        combinations of these constants. It also verifies that the
        server does not respond with a right in "access" but not in
        "supported".
        """
        
        for accessop in self.valid_access_ops():
            res = self.do_compound([self.putrootfhop, accessop])
            self.assert_OK(res)
            
            supported = res.resarray[1].arm.arm.supported
            access = res.resarray[1].arm.arm.access

            # Server should not return an access bit if this bit is not in supported. 
            self.failIf(access > supported, "access is %d, but supported is %d" % (access, supported))

    def testFile(self):
        """All valid combinations of ACCESS arguments on file

        Covered valid equivalence classes: 7, 9

        Comments: See testDir. 
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        
        for accessop in self.valid_access_ops():
            operations = [self.putrootfhop] + lookupops
            operations.append(accessop)
            res = self.do_compound(operations)
            self.assert_OK(res)

            supported = res.resarray[-1].arm.arm.supported
            access = res.resarray[-1].arm.arm.access

            # Server should not return an access bit if this bit is not in supported. 
            self.failIf(access > supported, "access is %d, but supported is %d" % (access, supported))

    #
    # Testcases covering invalid equivalence classes.
    #
    def testWithoutFh(self):
        """ACCESS should return NFS4ERR_NOFILEHANDLE if called without filehandle.

        Covered invalid equivalence classes: 8
        
        """
        accessop = self.ncl.access_op(ACCESS4_READ)
        res = self.do_compound([accessop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])


    def testInvalids(self):
        """ACCESS should fail on invalid arguments

        Covered invalid equivalence classes: 10

        Comments: ACCESS should return with NFS4ERR_INVAL if called
        with an illegal access request (eg. an integer with bits set
        that does not correspond to any ACCESS4* constant).
        """
        for accessop in self.invalid_access_ops():
            res = self.do_compound([self.putrootfhop, accessop])
            self.assert_status(res, [NFS4ERR_INVAL])
    #
    # Extra tests
    #
    def testNoExecOnDir(self):
        """ACCESS4_EXECUTE should never be returned for directory

        Extra test

        Comments: ACCESS4_EXECUTE has no meaning for directories and
        should not be returned in "access" or "supported".
        """
        for accessop in self.valid_access_ops():
            res = self.do_compound([self.putrootfhop, accessop])
            self.assert_OK(res)
            
            supported = res.resarray[1].arm.arm.supported
            access = res.resarray[1].arm.arm.access

            self.failIf(supported & ACCESS4_EXECUTE,
                        "server returned ACCESS4_EXECUTE for root dir (supported=%d)" % supported)

            self.failIf(access & ACCESS4_EXECUTE,
                        "server returned ACCESS4_EXECUTE for root dir (access=%d)" % access)


## class CloseSuite(NFSSuite):
##     """Test operation 4: CLOSE
##     """
##     # FIXME
##     pass
    

class CommitSuite(NFSSuite):
    """Test operation 5: COMMIT

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            file(1)
        Invalid equivalence classes:
            link(2)
            special object(3)
            dir(7)
            invalid filehandle(8)
    Input Condition: offset
        Valid equivalence classes:
            zero(9)
            nonzero(10)
        Invalid equivalence classes:
            -
    Input Condition: count
        Valid equivalence classes:
            zero(11)
            nonzero(12)
        Invalid equivalence classes:
            -
    Note: We do not examine the writeverifier in any way. It's hard
    since it can change at any time.
    """

    #
    # Testcases covering valid equivalence classes.
    #
    def testOffsets(self):
        """Simple COMMIT on file with offset 0, 1 and 2**64 - 1

        Covered valid equivalence classes: 1, 9, 10, 11

        Comments: This test case tests boundary values for the offset
        parameter in the COMMIT operation. All values are
        legal. Tested values are 0, 1 and 2**64 - 1 (selected by BVA)
        """
        lookupops = self.ncl.lookup_path(self.regfile)

        # Offset = 0
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(0, 0))
        res = self.do_compound(operations)
        self.assert_OK(res)

        # offset = 1
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(1, 0))
        res = self.do_compound(operations)
        self.assert_OK(res)
        
        # offset = 2**64 - 1
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(0xffffffffffffffffL, 0))
        res = self.do_compound(operations)
        self.assert_OK(res)

    def _testWithCount(self, count):
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(0, count))
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testCounts(self):
        """COMMIT on file with count 0, 1 and 2**32 - 1

        Covered valid equivalence classes: 1, 9, 11, 12

        This test case tests boundary values for the count parameter
        in the COMMIT operation. All values are legal. Tested values
        are 0, 1 and 2**32 - 1 (selected by BVA)
        """
        # count = 0
        self._testWithCount(0)

        # count = 1
        self._testWithCount(1)
        
        # count = 2**32 - 1
        self._testWithCount(0xffffffffL)

    #
    # Testcases covering invalid equivalence classes
    #
    def _testOnObj(self, obj, expected_response):
        lookupops = self.ncl.lookup_path(obj)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(0, 0))
        res = self.do_compound(operations)
        self.assert_status(res, [expected_response])
    
    def testOnLink(self):
        """COMMIT should fail with NFS4ERR_SYMLINK on links

        Covered invalid equivalence classes: 2
        """
        self._testOnObj(self.linkfile, NFS4ERR_SYMLINK)

    def testOnSpecials(self):
        """COMMIT on special objects should fail with NFS4ERR_INVAL

        Covered invalid equivalence classes: 3
        """
        for obj in self.special_objects:
            self._testOnObj(obj, NFS4ERR_INVAL)

    def testOnDir(self):
        """COMMIT should fail with NFS4ERR_ISDIR on directories

        Covered invalid equivalence classes: 7
        """
        self._testOnObj(self.dirfile, NFS4ERR_ISDIR)
        
    def testWithoutFh(self):
        """COMMIT should return NFS4ERR_NOFILEHANDLE if called without filehandle.

        Covered invalid equivalence classes: 8
        """
        commitop = self.ncl.commit_op(0, 0)
        res = self.do_compound([commitop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    #
    # Extra tests
    #
    def testOverflow(self):
        """COMMIT on file with offset+count >= 2**64 should fail

        Extra test

        Comments: If the COMMIT operation is called with an offset
        plus count that is larger than 2**64, the server should return
        NFS4ERR_INVAL
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.commit_op(-1, -1))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])


class CreateSuite(NFSSuite):
    """Test operation 6: CREATE

    FIXME: Add attribute directory and named attribute testing.

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            not dir(11)
            no filehandle(12)
    Input Condition: type
        Valid equivalence classes:
            link(20)
            blockdev(21)
            chardev(22)
            socket(23)
            FIFO(24)
            directory(25)
        Invalid equivalence classes:
            regular file(26)
    Input Condition: name
        Valid equivalence classes:
            legal name(30)
        Invalid equivalence classes:
            zero length(31)
    Input Condition: fattr.attrmask
        Valid equivalence classes:
            valid attrmask(40)
        Invalid equivalence classes:
            invalid attrmask(41) (FATTR4_*_SET)
    Input Condition: fattr.attr_vals
        Valid equivalence classes:
            valid attribute value(50)
        Invalid equivalence classes:
            valid attribute value(51)

    """
    __pychecker__ = 'no-classattr'
    
    def setUp(self):
        NFSSuite.setUp(self)
        self.obj_name = "object1"

        self.lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)

    #
    # Testcases covering valid equivalence classes.
    #
    def testLink(self):
        """CREATE (symbolic) link

        Covered valid equivalence classes: 10, 20, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] +  self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4LNK, linkdata="/etc/X11")

        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("links not supported")
        self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE])

    def testBlock(self):
        """CREATE a block device

        Covered valid equivalence classes: 10, 21, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        
        devdata = specdata4(self.ncl, 1, 2)
        objtype = createtype4(self.ncl, type=NF4BLK, devdata=devdata)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        # FIXME: Maybe try to create block and character devices as root.
        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("blocks devices not supported")
        elif res.status == NFS4ERR_PERM:
            self.info_message("not permitted")
        else:
            self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE, NFS4ERR_PERM])

    def testChar(self):
        """CREATE a character device

        Covered valid equivalence classes: 10, 22, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] +  self.lookup_dir_ops
        devdata = specdata4(self.ncl, 1, 2)
        objtype = createtype4(self.ncl, type=NF4CHR, devdata=devdata)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("character devices not supported")
        elif res.status == NFS4ERR_PERM:
            self.info_message("not permitted")
        else:
            self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE, NFS4ERR_PERM])

    def testSocket(self):
        """CREATE a socket

        Covered valid equivalence classes: 10, 23, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] +  self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4SOCK)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("sockets not supported")
        self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE])

    def testFIFO(self):
        """CREATE a FIFO

        Covered valid equivalence classes: 10, 24, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4FIFO)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("FIFOs not supported")
        self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE])

    def testDir(self):
        """CREATE a directory

        Covered valid equivalence classes: 10, 25, 30, 40, 50
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4DIR)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        if res.status == NFS4ERR_BADTYPE:
            self.info_message("directories not supported!")
        self.assert_status(res, [NFS4_OK, NFS4ERR_BADTYPE])

    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotDir(self):
        """CREATE should fail with NFS4ERR_NOTDIR if (cfh) is not dir

        Covered invalid equivalence classes: 11
        """
        if not self.remove_object(): return
        lookupops = self.ncl.lookup_path(self.regfile)
        
        operations = [self.putrootfhop] + lookupops
        objtype = createtype4(self.ncl, type=NF4DIR)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNoFh(self):
        """CREATE should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

        Covered invalid equivalence classes: 12
        """
        if not self.remove_object(): return
        objtype = createtype4(self.ncl, type=NF4DIR)
        createop = self.ncl.create(objtype, self.obj_name)

        res = self.do_compound([createop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testZeroLengthName(self):
        """CREATE with zero length name should fail

        Covered invalid equivalence classes: 31
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4DIR)
        createop = self.ncl.create(objtype, "")
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testRegularFile(self):
        """CREATE should fail with NFS4ERR_INVAL for regular files

        Covered invalid equivalence classes: 26
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops

        # nfs4types.createtype4 does not allow packing invalid types
        class custom_createtype4(createtype4):
            def pack(self, dummy=None):
                assert_not_none(self, self.type)
                self.packer.pack_nfs_ftype4(self.type)
            
        objtype = custom_createtype4(self.ncl, type=NF4REG)
        createop = self.ncl.create(objtype, self.obj_name)
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_BADXDR])

    def testInvalidAttrmask(self):
        """CREATE should fail with NFS4ERR_INVAL on invalid attrmask

        Covered invalid equivalence classes: 41

        Comments: We are using a read-only attribute on CREATE, which
        should return NFS4ERR_INVAL. 
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops

        objtype = createtype4(self.ncl, type=NF4DIR)

        attrmask = nfs4lib.list2attrmask([FATTR4_LINK_SUPPORT])
        dummy_ncl = nfs4lib.DummyNcl()
        dummy_ncl.packer.pack_bool(TRUE)
        attr_vals = dummy_ncl.packer.get_buffer()
        createattrs = fattr4(self.ncl, attrmask, attr_vals)
        
        createop = self.ncl.create_op(objtype, self.obj_name, createattrs)
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testInvalidAttributes(self):
        """CREATE should fail with NFS4ERR_XDR on invalid attr_vals

        Covered invalid equivalence classes: 51

        Comments: BADXDR should take precedence over NOTSUPP; BADXDR
        should be returned even if the server does not support the attribute
        """
        if not self.remove_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops

        objtype = createtype4(self.ncl, type=NF4DIR)

        attrmask = nfs4lib.list2attrmask([FATTR4_ARCHIVE])
        # We use a short buffer, to trigger BADXDR. 
        attr_vals = ""
        createattrs = fattr4(self.ncl, attrmask, attr_vals)
        
        createop = self.ncl.create_op(objtype, self.obj_name, createattrs)
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_BADXDR])

    #
    # Extra tests.
    #
    def _do_create(self, name):
        operations = [self.putrootfhop] + self.lookup_dir_ops
        objtype = createtype4(self.ncl, type=NF4DIR)
        createop = self.ncl.create(objtype, name)
        operations.append(createop)

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4_OK, NFS4ERR_INVAL])

    def testDots(self):
        """CREATE with . or .. should succeed or return NFS4ERR_INVAL

        Extra test

        Servers supporting . and .. in file names should return NFS4_OK. Others
        should return NFS4ERR_INVAL. NFS4ERR_EXIST should not be returned.
        """
        testname = "."
        if not self.make_sure_nonexistent(testname): return
        self._do_create(testname)

        testname = ".."
        if not self.make_sure_nonexistent(testname): return
        self._do_create(testname)

    def testSlash(self):
        """CREATE WITH "/" in filename should succeed or return NFS4ERR_INVAL

        Extra test

        Make sure / in file names are not treated as directory
        separator. Servers supporting "/" in file names should return
        NFS4_OK. Others should return NFS4ERR_INVAL. NFS4ERR_EXIST
        should not be returned.
        """
        # Great idea. Try this:
        # tmp
        # |-- "gazonk/foo.c"
        # `-- gazonk
        #     `--foo.c
        #
        # /tmp/gazonk/foo.c is created by test_tree.py. 
        
        testname = "gazonk/foo.c"
        if not self.make_sure_nonexistent(testname): return
        # Try to create "gazonk/foo.c"
        self._do_create(testname)

    def testNamingPolicy(self):
        """CREATE should obey OPEN file name creation policy

        Extra test
        """
        self.init_connection()

        try:
            (x, rejected_names_open) = self.try_file_names(creator=self.create_via_open)
            self.info_message("Rejected file names by OPEN: %s" \
                              % repr(rejected_names_open))
            
            (x, rejected_names_create) = self.try_file_names(creator=self.create_via_create)
            self.info_message("Rejected file names by CREATE: %s" \
                              % repr(rejected_names_create))
            
            
            self.failIf(rejected_names_open != rejected_names_create,
                        "CREATE does not obey OPEN naming policy")
        except SkipException, e:
            print e


## class DelegpurgeSuite(NFSSuite):
##     """Test operation 7: DELEGPURGE
##     """
##     # FIXME
##     pass


## class DelegreturnSuite(NFSSuite):
##     """Test operation 8: DELEGRETURN
##     """
##     # FIXME
##     pass


class GetattrSuite(NFSSuite):
    """Test operation 9: GETATTR

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            file(1)
            link(2)
            block(3)
            char(4)
            socket(5)
            FIFO(6)
            dir(7)
        Invalid equivalence classes:
            invalid filehandle(8)
    Input Condition: attrbits
        Valid equivalence classes:
            all requests without FATTR4_*_SET (9)
        Invalid equivalence classes:
            requests with FATTR4_*_SET (10)
    
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testAllObjects(self):
        """GETATTR(FATTR4_SIZE) on all type of objects

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 9
        """
        for lookupops in self.ncl.lookup_all_objects():
            operations = [self.putrootfhop] + lookupops
            operations.append(self.ncl.getattr([FATTR4_SIZE]))
            res = self.do_compound(operations)
            self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """GETATTR should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

        Covered invalid equivalence classes: 8
        """
        
        getattrop = self.ncl.getattr([FATTR4_SIZE])
        res = self.do_compound([getattrop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testWriteOnlyAttributes(self):
        """GETATTR(FATTR4_*_SET) should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 10

        Comments: Some attributes are write-only (currently
        FATTR4_TIME_ACCESS_SET and FATTR4_TIME_MODIFY_SET). If GETATTR
        is called with any of these, NFS4ERR_INVAL should be returned.
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr([FATTR4_TIME_ACCESS_SET]))
        
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    #
    # Extra tests. 
    #
    def testAllMandatory(self):
        """Assure GETATTR can return all mandatory attributes

        Extra test

        Comments: A server should be able to return all mandatory
        attributes.
        """

        attrbitnum_dict = nfs4lib.get_attrbitnum_dict()
        all_mandatory_names = [
            "supported_attrs", 
            "type",
            "fh_expire_type",
            "change",
            "size",
            "link_support",
            "symlink_support",
            "named_attr",
            "fsid",
            "unique_handles",
            "lease_time",
            "rdattr_error"]
        all_mandatory = []
        
        for attrname in all_mandatory_names:
            all_mandatory.append(attrbitnum_dict[attrname])

        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr(all_mandatory))
        
        res = self.do_compound(operations)
        self.assert_OK(res)
        obj = res.resarray[-1].arm.arm.obj_attributes
        d = nfs4lib.fattr2dict(obj)

        unsupported = []
        keys = d.keys()
        for attrname in all_mandatory_names:
            if not attrname in keys:
                unsupported.append(attrname)

        if unsupported:
            self.fail("mandatory attributes not supported: %s" % str(unsupported))

    def testUnknownAttr(self):
        """GETATTR should not fail on unknown attributes

        Covered valid equivalence classes: 1, 9

        Comments: This test calls GETATTR with request for attribute
        number 1000.  Servers should not fail on unknown attributes.
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr([1000]))

        res = self.do_compound(operations)
        self.assert_OK(res)

    def testEmptyCall(self):
        """GETATTR should accept empty request

        Covered valid equivalence classes: 1, 9

        Comments: GETATTR should accept empty request
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr([]))

        res = self.do_compound(operations)
        self.assert_OK(res)

    def testSupported(self):
        """GETATTR(FATTR4_SUPPORTED_ATTRS) should return all mandatory

        Covered valid equivalence classes: 1, 9
        
        Comments: GETATTR(FATTR4_SUPPORTED_ATTRS) should return at
        least all mandatory attributes
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr([FATTR4_SUPPORTED_ATTRS]))

        res = self.do_compound(operations)
        self.assert_OK(res)

        obj = res.resarray[-1].arm.arm.obj_attributes

        ncl = nfs4lib.DummyNcl(obj.attr_vals)
        intlist = ncl.unpacker.unpack_fattr4_supported_attrs()
        i = nfs4lib.intlist2long(intlist)

        all_mandatory_bits = 2**(FATTR4_RDATTR_ERROR+1) - 1

        returned_mandatories = i & all_mandatory_bits

        self.failIf(not returned_mandatories == all_mandatory_bits,
                    "not all mandatory attributes returned: %s" % \
                    nfs4lib.int2binstring(returned_mandatories)[-12:])

        sys.stdout.flush()


class GetfhSuite(NFSSuite):
    """Test operation 10: GETFH

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            file(1)
            link(2)
            block(3)
            char(4)
            socket(5)
            FIFO(6)
            dir(7)
        Invalid equivalence classes:
            invalid filehandle(8)
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testAllObjects(self):
        """GETFH on all type of objects

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7
        """
        for lookupops in self.ncl.lookup_all_objects():
            operations = [self.putrootfhop] + lookupops
            operations.append(self.ncl.getfh_op())
            res = self.do_compound(operations)
            self.assert_OK(res)            
    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """GETFH should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

        Covered invalid equivalence classes: 8

        Comments: GETFH should fail with NFS4ERR_NOFILEHANDLE if no
        (cfh)
        """
        getfhop = self.ncl.getfh_op()
        res = self.do_compound([getfhop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])


class LinkSuite(NFSSuite):
    """Test operation 11: LINK

    FIXME: Add attribute directory and named attribute testing.
    FIXME: More combinations of invalid filehandle types. 

    Equivalence partitioning:

    Input Condition: saved filehandle
        Valid equivalence classes:
            file(1)
            link(2)
            block(3)
            char(4)
            socket(5)
            FIFO(6)
        Invalid equivalence classes:
            dir(7)
            invalid filehandle(8)
    Input Condition: current filehandle
        Valid equivalence classes:
            dir(9)
        Invalid equivalence classes:
            not dir(10)
            invalid filehandle(11)
    Input Condition: newname
        Valid equivalence classes:
            valid name(12)
        Invalid equivalence classes:
            zerolength(13)
            non-utf8(14)

    Comments: It's not possible to cover eq. class 11, since saving a filehandle
    gives a current filehandle as well. 
    """

    def setUp(self):
        NFSSuite.setUp(self)
        self.obj_name = "link1"

        self.lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)

    def _prepare_operation(self, file):
        # Put root FH
        operations = [self.putrootfhop]

        # Lookup source and save FH
        operations.extend(self.ncl.lookup_path(file))
        operations.append(self.ncl.savefh_op())

        # Lookup target directory
        operations.append(self.putrootfhop)
        operations.extend(self.lookup_dir_ops)

        return operations
    
    #
    # Testcases covering valid equivalence classes.
    #
    def testFile(self):
        """LINK a regular file

        Covered valid equivalence classes: 1, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.regfile)

        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testLink(self):
        """LINK a symbolic link should succeed or return NFS4ERR_NOTSUPP

        Covered valid equivalence classes: 2, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.linkfile)

        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)

        if res.status == NFS4ERR_NOTSUPP:
            self.info_message("LINK a symbolic link is not supported")

        self.assert_status(res, [NFS4_OK, NFS4ERR_NOTSUPP])

    def testBlock(self):
        """LINK a block device

        Covered valid equivalence classes: 3, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.blockfile)

        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testChar(self):
        """LINK a character device

        Covered valid equivalence classes: 4, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.charfile)
        
        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testSocket(self):
        """LINK a socket

        Covered valid equivalence classes: 5, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.socketfile)
        
        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    
    def testFIFO(self):
        """LINK a FIFO

        Covered valid equivalence classes: 6, 9, 12
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.fifofile)
        
        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #

    def testDir(self):
        """LINK a directory should fail with NFS4ERR_ISDIR

        Covered invalid equivalence classes: 7
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.dirfile)
        
        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_ISDIR])

    def testNoSfh(self):
        """LINK should fail with NFS4ERR_NOFILEHANDLE if no (sfh)

        Covered invalid equivalence classes: 8

        Comments: LINK should fail with NFS4ERR_NOFILEHANDLE if no
        saved filehandle exists. 
        """
        linkop = self.ncl.link_op(self.obj_name)
        res = self.do_compound([self.putrootfhop, linkop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testCfhNotDir(self):
        """LINK should fail with NFS4ERR_NOTDIR if cfh is not dir

        Covered invalid equivalence classes: 10
        """
        if not self.remove_object(): return

        # Put root FH
        operations = [self.putrootfhop]

        # Lookup source and save FH
        operations.extend(self.ncl.lookup_path(self.regfile))
        operations.append(self.ncl.savefh_op())

        # Lookup target directory (a file, this time)
        operations.append(self.putrootfhop)
        operations.extend(self.ncl.lookup_path(self.regfile))

        # Link operation
        linkop = self.ncl.link_op(self.obj_name)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testZeroLengthName(self):
        """LINK with zero length new name should fail with NFS4ERR_INVAL

        Covered invalid equivalence classes: 13
        """
        if not self.remove_object(): return
        operations = self._prepare_operation(self.regfile)

        # Link operation
        linkop = self.ncl.link_op("")
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8(self):
        """LINK with non-UTF8 name should return NFS4ERR_INVAL

        Covered valid equivalence classes: 14
        """
        for name in self.get_invalid_utf8strings():
            operations = self._prepare_operation(self.regfile)

            # Link operation
            linkop = self.ncl.link_op(name)
            operations.append(linkop)
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])

    #
    # Extra tests.
    #
    def _do_link(self, newname):
        if not self.remove_object(): return
        operations = self._prepare_operation(self.regfile)

        linkop = self.ncl.link_op(newname)
        operations.append(linkop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4_OK, NFS4ERR_INVAL])
    
    def testDots(self):
        """LINK with newname . or .. should succeed or return NFS4ERR_INVAL

        Extra test

        Comments: Servers supporting . and .. in file names should
        return NFS4_OK. Others should return
        NFS4ERR_INVAL. NFS4ERR_EXIST should not be returned.
        """
        testname = "."
        if not self.make_sure_nonexistent(testname): return
        self._do_link(testname)

        testname = ".."
        if not self.make_sure_nonexistent(testname): return
        self._do_link(testname)

    def testNamingPolicy(self):
        """LINK should obey OPEN file name creation policy

        Extra test
        """
        self.init_connection()

        try:
            (x, rejected_names_open) = self.try_file_names(creator=self.create_via_open)
            self.info_message("Rejected file names by OPEN: %s" \
                              % repr(rejected_names_open))
            
            (x, rejected_names_link) = self.try_file_names(creator=self.create_via_link)
            self.info_message("Rejected file names by LINK: %s" \
                              % repr(rejected_names_link))
            
            
            self.failIf(rejected_names_open != rejected_names_link,
                        "LINK does not obey OPEN naming policy")
        except SkipException, e:
            print e



## class LockSuite(NFSSuite):
##     """Test operation 12: LOCK
##     """
##     # FIXME
##     pass


## class LocktSuite(NFSSuite):
##     """Test operation 13: LOCKT
##     """
##     # FIXME
##     pass


## class LockuSuite(NFSSuite):
##     """Test operation 14: LOCKU
##     """
##     # FIXME
##     pass


class LookupSuite(NFSSuite):
    """Test operation 15: LOOKUP

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            not directory or symlink(11)
            invalid filehandle(12)
            symlink(13)
    Input Condition: objname
        Valid equivalence classes:
            legal name(20)
        Invalid equivalence classes:
            zero length(21)
            non-utf8(22)
            non-existent object(23)
            non-accessible object(24)
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testDir(self):
        """LOOKUP directory

        Covered valid equivalence classes: 10, 20
        """
        operations = [self.putrootfhop] + self.ncl.lookup_path(self.dirfile)
        res = self.do_compound(operations)
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotDir(self):
        """LOOKUP with non-dir (cfh) should give NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 11
        """
        lookupops1 = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops1
        operations.append(self.ncl.lookup_op("porting"))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNoFh(self):
        """LOOKUP without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 12
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        res = self.do_compound(lookupops)
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testSymlinkFh(self):
        """LOOKUP with (cfh) as symlink should return NFS4ERR_SYMLINK

        Covered invalid equivalence classes: 13
        """
        lookupops1 = self.ncl.lookup_path(self.dirsymlinkfile)
        operations = [self.putrootfhop] + lookupops1
        operations.append(self.ncl.lookup_op("porting"))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_SYMLINK])

    def testNonExistent(self):
        """LOOKUP with non-existent components should return NFS4ERR_NOENT

        Covered invalid equivalence classes: 23
        """
        lookupops = self.ncl.lookup_path(self.vaporfile)
        operations = [self.putrootfhop] + lookupops
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT])

    def testNonAccessable(self):
        """LOOKUP with non-accessable components should return NFS4ERR_ACCES

        Covered invalid equivalence classes: 24
        """
        # FIXME: This test is currently broken.
        self.info_message("(TEST DISABLED)")
        return
        lookupops = self.ncl.lookup_path(self.notaccessablefile)
        operations = [self.putrootfhop] + lookupops
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_ACCES])

    def testNonUTF8(self):
        """LOOKUP with non-UTF8 name should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 22
        """
        for name in self.get_invalid_utf8strings():
            operations = [self.putrootfhop]
            operations.append(self.ncl.lookup_op(name))
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])
            
    #
    # Extra tests.
    #
    def _assert_noent(self, pathcomps):
        lookupops = self.ncl.lookup_path(pathcomps)
        operations = [self.putrootfhop] + lookupops
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT])

    def testDots(self):
        """LOOKUP on (nonexistent) . and .. should return NFS4ERR_NOENT 

        Extra test

        Comments: Even if the server does not allow creation of files
        called . and .., LOOKUP should return NFS4ERR_NOENT. 
        """
        testname = "."
        if not self.make_sure_nonexistent(testname): return
        self._assert_noent([testname])
        
        testname = ".."
        if not self.make_sure_nonexistent(testname): return
        self._assert_noent([testname])

        # Try lookup on ["doc", ".", "README"]
        # First, make sure there is no object named "."
        # in the doc directory
        if not self.make_sure_nonexistent(".", self.dirfile): return
        # Of course it wasn't. Try LOOKUP with this strange path.
        # Note: The file doc/./README actually exists on a UNIX server. 
        self._assert_noent(self.dirfile + [".", "README"])
        
        # Same goes for ".."
        # Note: The file doc/porting/../README actually exists on a
        # UNIX server.
        if not self.make_sure_nonexistent("..", self.docporting):
            return
        self._assert_noent(self.docporting + ["..", "README"])

    def testValidNames(self):
        """LOOKUP should succeed on all legal names

        Extra test

        Comments: This test tries LOOKUP on all names returned from try_file_names()
        """
        self.init_connection()

        # Saved files for LOOKUP
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            print e
            return

        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        # Ok, lets try LOOKUP on all accepted names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in accepted_names:
            operations = [self.putrootfhop] + lookup_dir_ops
            operations.append(self.ncl.lookup_op(filename))
            res = self.do_compound(operations)
            self.assert_OK(res)

    def testInvalidNames(self):
        """LOOKUP should fail with NFS4ERR_NOENT on all unexisting, invalid file names

        Extra test

        Comments: Tries LOOKUP on rejected file names from
        try_file_names().  NFS4ERR_INVAL should NOT be returned in this case, although
        the server rejects creation of objects with these names
        """
        self.init_connection()

        try:
            (accepted_names, rejected_names) = self.try_file_names()
        except SkipException, e:
            print e
            return
        
        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        # Ok, lets try LOOKUP on all rejected names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in rejected_names:
            operations = [self.putrootfhop] + lookup_dir_ops
            operations.append(self.ncl.lookup_op(filename))
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_NOENT])


class LookuppSuite(NFSSuite):
    """Test operation 16: LOOKUPP

    Equivalence partitioning:
        
    Input Condition: current filehandle
        Valid equivalence classes:
            directory(10)
            named attribute dir(11)
        Invalid equivalence classes:
            not directory(12)
            invalid filehandle(13)

    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testDir(self):
        """LOOKUPP with directory (cfh)

        Covered valid equivalence classes: 10
        """
        lookupops1 = self.ncl.lookup_path(self.docporting)

        operations = [self.putrootfhop] + lookupops1
        operations.append(self.ncl.lookupp_op())
        operations.append(self.ncl.lookup_op("README"))
        
        res = self.do_compound(operations)
        self.assert_OK(res)

    def testNamedAttrDir(self):
        """LOOKUPP with named attribute directory (cfh)

        Covered valid equivalence classes: 11

        Comments: Not yet implemented. 
        """
        # FIXME: Implement.
        self.info_message("(TEST NOT IMPLEMENTED)")

    #
    # Testcases covering invalid equivalence classes.
    #
    def testInvalidFh(self):
        """LOOKUPP with non-dir (cfh)

        Covered invalid equivalence classes: 12
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops 
        operations.append(self.ncl.lookupp_op())
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    #
    # Extra tests.
    #
    def testAtRoot(self):
        """LOOKUPP with (cfh) at root should return NFS4ERR_NOENT

        Extra test
        """
        lookuppop = self.ncl.lookupp_op()
        res = self.do_compound([self.putrootfhop, lookuppop])
        self.assert_status(res, [NFS4ERR_NOENT])


class NverifySuite(NFSSuite):
    """Test operation 17: NVERIFY

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            link(1)
            block(2)
            char(3)
            socket(4)
            FIFO(5)
            dir(6)
            file(7)
        Invalid equivalence classes:
            invalid filehandle(8)
    Input Condition: obj_attributes.attrmask
        Valid equivalence classes:
            valid attribute(9)
        Invalid equivalence classes:
            invalid attrmask(10) (FATTR4_*_SET)
    Input Condition: obj_attributes.attr_vals
        Valid equivalence classes:
            changed attribute(11)
            same attribute(12)
        Invalid equivalence classes:
            attr with invalid utf8(13)
    """
    
    #
    # Testcases covering valid equivalence classes.
    #
    def testChanged(self):
        """NVERIFY with CHANGED attribute should execute remaining ops

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 9, 11
        """
        # Fetch sizes for all objects
        obj_sizes = self.ncl.lookup_all_objects_and_sizes()
        
        # For each type of object, do nverify with wrong filesize,
        # get new filesize and check if it match previous size. 
        for (lookupops, objsize) in obj_sizes:
            operations = [self.putrootfhop] + lookupops
            
            # Nverify op
            attrmask = nfs4lib.list2attrmask([FATTR4_SIZE])
            # We simulate a changed object by using a wrong filesize
            # Size attribute is 8 bytes. 
            attr_vals = nfs4lib.long2opaque(objsize + 17, 8)
            obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
            operations.append(self.ncl.nverify_op(obj_attributes))

            # New getattr
            operations.append(self.ncl.getattr([FATTR4_SIZE]))

            res = self.do_compound(operations)
            self.assert_OK(res)

            # Assert the new getattr was executed.
            # File sizes should match. 
            obj = res.resarray[-1].arm.arm.obj_attributes
            d =  nfs4lib.fattr2dict(obj)
            new_size = d["size"]
            self.failIf(objsize != new_size,
                        "GETATTR after NVERIFY returned different filesize")


    def testSame(self):
        """NVERIFY with unchanged attribute should return NFS4ERR_SAME

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 9, 12
        """

        # Fetch sizes for all objects
        obj_sizes = self.ncl.lookup_all_objects_and_sizes()
        
        # For each type of object, do nverify with wrong filesize. 
        for (lookupops, objsize) in obj_sizes:
            operations = [self.putrootfhop] + lookupops
            
            # Nverify op
            attrmask = nfs4lib.list2attrmask([FATTR4_SIZE])
            # Size attribute is 8 bytes. 
            attr_vals = nfs4lib.long2opaque(objsize, 8)
            obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
            operations.append(self.ncl.nverify_op(obj_attributes))

            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_SAME])

    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """NVERIFY without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 8
        """
        attrmask = nfs4lib.list2attrmask([FATTR4_SIZE])
        # Size attribute is 8 bytes. 
        attr_vals = nfs4lib.long2opaque(17, 8)
        obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
        nverifyop = self.ncl.nverify_op(obj_attributes)
        res = self.do_compound([nverifyop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testWriteOnlyAttributes(self):
        """NVERIFY with FATTR4_*_SET should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 10

        Comments: See GetattrSuite.testWriteOnlyAttributes. 
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        # Nverify
        attrmask = nfs4lib.list2attrmask([FATTR4_TIME_ACCESS_SET])
        # Size attribute is 8 bytes. 
        attr_vals = nfs4lib.long2opaque(17, 8)
        obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
        nverifyop = self.ncl.nverify_op(obj_attributes)
        operations.append(nverifyop)
        
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8(self):
        """NVERIFY with non-UTF8 FATTR4_OWNER should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 13
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        for name in self.get_invalid_utf8strings():
            operations = [self.putrootfhop] + lookupops
            
            # Nverify op
            attrmask = nfs4lib.list2attrmask([FATTR4_OWNER])
            dummy_ncl = nfs4lib.DummyNcl()
            dummy_ncl.packer.pack_utf8string(name)
            attr_vals = dummy_ncl.packer.get_buffer()
            obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
            operations.append(self.ncl.nverify_op(obj_attributes))

            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])



## class OpenSuite(NFSSuite):
##     """Test operation 18: OPEN

##     FIXME: Verify that this eqv.part is correct, when updated state
##     description is available.

##     FIXME: Add test for file named "." and ".." in
##     open_claim_delegate_cur4.file, open_claim4.file and
##     open_claim4.file_delegate_prev. 

##     Equivalence partitioning:

##     Input Condition: seqid
##         Valid equivalence classes:
##             correct seqid(10)
##         Invalid equivalence classes:
##             to small seqid(11)
##             to large seqid(12)
##     Input Condition: share_access
##         Valid equivalence classes:
##             valid share_access(20)
##         Invalid equivalence classes:
##             invalid share_access(21)
##     Input Condition: share_deny
##         Valid equivalence classes:
##             valid share_deny(30)
##         Invalid equivalence classes:
##             invalid share_deny(31)
##     Input Condition: owner.clientid
##         Valid equivalence classes:
##             valid clientid(40)
##         Invalid equivalence classes:
##             stale clientid(41)
##     Input Condition: owner.opaque
##         Valid equivalence classes:
##             valid owner(50)
##         Invalid equivalence classes:
##             invalid owner(51)
##     Input Condition: openhow.opentype
##         Valid equivalence classes:
##             OPEN_NOCREATE4(60)
##             OPEN_CREATE4(61)
##         Invalid equivalence classes:
##             invalid openhow.opentype(62)
##     Input Condition: openhow.how
##         Valid equivalence classes:
##             UNCHECKED4(70)
##             GUARDED4(71)
##             EXCLUSIVE4(72)
##         Invalid equivalence classes:
##             invalid openhow.how(73)
##     Input Condition: openhow.how.createattrs
##         Valid equivalence classes:
##             valid createattrs(80)
##         Invalid equivalence classes:
##             invalid createattrs(80)
##     Input Condition: openhow.how.createverf
##         Valid equivalence classes:
##             matching verifier(90):
##             non-matching verifier(91)
##         Invalid equivalence classes:
##             -
##     Input Condition: claim.claim
##         Valid equivalence classes:
##             CLAIM_NULL(100)
##             CLAIM_PREVIOUS(101)
##             CLAIM_DELEGATE_CUR(102)
##             CLAIM_DELEGATE_PREV(103)
##         Invalid equivalence classes:
##             invalid claim.claim(104)
##     Input Condition: claim.file:
##         Valid equivalence classes:
##             valid filename(110)
##         Invalid equivalence classes:
##             non-utf8 filename(111)
##     Input Condition: claim.delegate_type
##         Valid equivalence classes:
##             valid claim.delegate_type(120)
##         Invalid equivalence classes:
##             invalid claim.delegate_type(121)
##     Input Condition: claim.delegate_cur_info.delegate_stateid
##         Valid equivalence classes:
##             valid stateid(130)
##         Invalid equivalence classes:
##             invalid stateid(131)
##     Input Condition: claim.delegate_cur_info.file
##         Valid equivalence classes:
##             valid filename(140)
##         Invalid equivalence classes:
##             invalid filenname(141)
##     Input Condition: claim.file_delegate_prev
##         Valid equivalence classes:
##             valid filename(150)
##         Invalid equivalence classes:
##             invalid filename(151)
##     """
##     # FIXME
##     pass

class OpenattrSuite(NFSSuite):
    """Test operation 19: OPENATTR

    FIXME: Verify that these tests works, as soon as I have access to a server
    that supports named attributes. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            file(1)
            dir(2)
            block(3)
            char(4)
            link(5)
            socket(6)
            FIFO(7)
        Invalid equivalence classes:
            attribute directory(8)
            named attribute(9)
    Input Condition: createdir
        Valid equivalence classes:
            false(20)
            true(21)
        Invalid equivalence classes:
            -
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def _openattr(self, createdir):
        for lookupops in self.ncl.lookup_all_objects():
            operations = [self.putrootfhop] + lookupops
            operations.append(self.ncl.openattr_op(createdir))
            res = self.do_compound(operations)

            if res.status == NFS4ERR_NOTSUPP:
                path = self.ncl.lookuplist2comps(lookupops)
                self.info_message("OPENATTR not supported on " + str(path))

            self.assert_status(res, [NFS4_OK, NFS4ERR_NOTSUPP])

    def testValidNoCreate(self):
        """OPENATTR on all non-attribute objects, createdir=FALSE

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 20
        """
        self._openattr(FALSE)

    def testValidCreate(self):
        """OPENATTR on all non-attribute objects, createdir=TRUE

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 21
        """
        self._openattr(TRUE)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testOnAttrDir(self):
        """OPENATTR on attribute directory should fail with NFS4ERR_INVAL

        Covered invalid equivalence classes: 8
        """
        # Open attribute dir for root dir
        openattrop = self.ncl.openattr_op(FALSE)
        res = self.do_compound([self.putrootfhop, openattrop])
        if res.status == NFS4ERR_NOTSUPP:
            self.info_message("OPENATTR not supported on /, cannot try this test")
            return

        openattrop1 = self.ncl.openattr_op(FALSE)
        openattrop2 = self.ncl.openattr_op(FALSE)
        res = self.do_compound([self.putrootfhop, openattrop1, openattrop2])
        self.assert_status(res, [NFS4ERR_INVAL])

    def testOnAttr(self):
        """OPENATTR on attribute should fail with NFS4ERR_INVAL

        Covered invalid equivalence classes: 9

        Comments: Not yet implemented. 
        """
        # Open attribute dir for doc/README
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.openattr_op(FALSE))
        res = self.do_compound(operations)

        if res.status == NFS4ERR_NOTSUPP:
            self.info_message("OPENATTR not supported on %s, cannot try this test" \
                              % self.regfile)
            return

        # FIXME: Implement rest of testcase.
        self.info_message("(TEST NOT IMPLEMENTED)")


## class OpenconfirmSuite(NFSSuite):
##     """Test operation 20: OPEN_CONFIRM
##     """
##     # FIXME
##     pass

## class OpendowngradeSuite(NFSSuite):
##     """Test operation 21: OPEN_DOWNGRADE
##     """
##     # FIXME
##     pass

class PutfhSuite(NFSSuite):
    """Test operation 22: PUTFH

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:

    Input Condition: supplied filehandle
        Valid equivalence classes:
            file(1)
            dir(2)
            block(3)
            char(4)
            link(5)
            socket(6)
            FIFO(7)
            attribute directory(8)
            named attribute(9)
        Invalid equivalence classes:
            invalid filehandle(10)

    Comments: It's not possible to cover eq. class 10, since a filehandle
    is opaque to the client.
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testAllObjects(self):
        """PUTFH followed by GETFH on all type of objects

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7
        """
        # Fetch filehandles of all types
        # List with (objpath, fh)
        filehandles = []
        for lookupops in self.ncl.lookup_all_objects():
            operations = [self.putrootfhop] + lookupops
            
            operations.append(self.ncl.getfh_op())
            res = self.do_compound(operations)
            self.assert_OK(res)

            objpath = self.ncl.lookuplist2comps(lookupops)
            fh = res.resarray[-1].arm.arm.object
            filehandles.append((objpath, fh))

        # Try PUTFH & GETFH on all these filehandles. 
        for (objpath, fh) in filehandles:
            putfhop = self.ncl.putfh_op(fh)
            getfhop = self.ncl.getfh_op()
            res = self.do_compound([putfhop, getfhop])
            self.assert_OK(res)

            new_fh = res.resarray[-1].arm.arm.object
            self.failIf(new_fh != fh, "GETFH after PUTFH returned different fh "\
                        "for object %s" % objpath)


class PutpubfhSuite(NFSSuite):
    """Test operation 23: PUTPUBFH

    Equivalence partitioning:

    Input Condition: -
        Valid equivalence classes:
            -
        Invalid equivalence classes:
            -
    """
    def testOp(self):
        """Testing PUTPUBFH

        Covered valid equivalence classes: -
        """
        putpubfhop = self.ncl.putpubfh_op()
        res = self.do_compound([putpubfhop])
        self.assert_OK(res)


class PutrootfhSuite(NFSSuite):
    """Test operation 24: PUTROOTFH

    Equivalence partitioning:

    Input Condition: -
        Valid equivalence classes:
            -
        Invalid equivalence classes:
            -
    """
    def testOp(self):
        """Testing PUTROOTFH

        Covered valid equivalence classes: -
        """
        putrootfhop = self.ncl.putrootfh_op()
        res = self.do_compound([putrootfhop])
        self.assert_OK(res)


## class ReadSuite(NFSSuite):
##     """Test operation 25: READ

##     FIXME: Adapt to protocol changes. 

##     FIXME: Add attribute directory and named attribute testing.
##     FIXME: Try reading a locked file. 

##     Equivalence partitioning:

##     Input Condition: current filehandle
##         Valid equivalence classes:
##             file(1)
##             named attribute(2)
##         Invalid equivalence classes:
##             dir(3)
##             special device files(4)
##             invalid filehandle(10)
##     Input Condition: stateid
##         Valid equivalence classes:
##             all bits zero(17)
##             all bits one(18)
##             valid stateid from open(19)
##         Invalid equivalence classes:
##             invalid stateid(20)
##     Input Condition: offset
##         Valid equivalence classes:2
##             zero(11)
##             less than file size(12)
##             greater than or equal to file size(13)
##         Invalid equivalence classes:
##             -
##     Input Condition: count
##         Valid equivalence classes:
##             zero(14)
##             one(15)
##             greater than one(16)
##         Invalid equivalence classes:
##             -
##     """
##     #
##     # Testcases covering valid equivalence classes.
##     #
##     def testSimpleRead(self):
##         """READ from regular file with stateid=zeros

##         Covered valid equivalence classes: 1, 11, 14, 17
##         """
##         lookupops = self.ncl.lookup_path(self.regfile)
##         operations = [self.putrootfhop] + lookupops

##         stateid = stateid4(self.ncl, 0, "")
##         operations.append(self.ncl.read(offset=0, count=0, stateid=stateid))
##         res = self.do_compound(operations)
##         self.assert_OK(res)

##     def testReadAttr(self):
##         """READ from named attribute

##         Covered valid equivalence classes: 2, 12, 16, 18
##         """
##         # FIXME: Implement rest of testcase.
##         self.info_message("(TEST NOT IMPLEMENTED)")

##     def testStateidOne(self):
##         """READ with offset=2, count=1, stateid=ones

##         Covered valid equivalence classes: 1, 12, 15, 18
##         """
##         lookupops = self.ncl.lookup_path(self.regfile)
##         operations = [self.putrootfhop] + lookupops

##         stateid = stateid4(self.ncl, 0, nfs4lib.long2opaque(0xffffffffffffffffffffffffL))
##         readop = self.ncl.read(offset=2, count=1, stateid=stateid)
##         operations.append(readop)
        
##         res = self.do_compound(operations)
##         self.assert_OK(res)

##     def testWithOpen(self):
##         """READ with offset>size, count=5, stateid from OPEN

##         Covered valid equivalence classes: 1, 13, 16, 19
##         """
##         self.init_connection()

        
##         lookupops = self.ncl.lookup_path(self.regfile[:-1])
##         operations = [self.putrootfhop] + lookupops

##         # OPEN
##         operations.append(self.ncl.open(file=self.regfile[-1]))
##         operations.append(self.ncl.getfh_op())
##         res = self.do_compound(operations)
##         self.assert_OK(res)
##         # [-2] is the OPEN operation
##         stateid = res.resarray[-2].arm.arm.stateid
##         # [-1] is the GETFH operation
##         fh = res.resarray[-1].arm.arm.object

##         # README is 36 bytes. Lets use 1000 as offset.
##         putfhop = self.ncl.putfh_op(fh)
##         readop = self.ncl.read(offset=1000, count=5, stateid=stateid)

##         res = self.do_compound([putfhop, readop])
##         self.assert_OK(res)

##     #
##     # Testcases covering invalid equivalence classes.
##     #
##     def testDirFh(self):
##         """READ with (cfh)=directory should return NFS4ERR_ISDIR

##         Covered invalid equivalence classes: 3
##         """
##         lookupops = self.ncl.lookup_path(self.dirfile)
##         operations = [self.putrootfhop] + lookupops
##         stateid = stateid4(self.ncl, 0, "")
##         readop = self.ncl.read(stateid)
##         operations.append(readop)

##         res = self.do_compound(operations)
##         self.assert_status(res, [NFS4ERR_ISDIR])

##     def testSpecials(self):
##         """READ with (cfh)=device files should return NFS4ERR_INVAL

##         Covered invalid equivalence classes: 4
##         """
##         for pathcomps in [self.blockfile,
##                           self.charfile,
##                           self.linkfile,
##                           self.socketfile,
##                           self.fifofile]:
##             lookupop = self.ncl.lookup_op(pathcomps)
##             readop = self.ncl.read()

##             res = self.do_compound([self.putrootfhop, lookupop, readop])

##             if res.status != NFS4ERR_INVAL:
##                 self.info_message("READ on %s dit not return NFS4ERR_INVAL" % name)
            
##             self.assert_status(res, [NFS4ERR_INVAL])

##     def testNoFh(self):
##         """READ without (cfh) should return NFS4ERR_NOFILEHANDLE

##         Covered invalid equivalence classes: 10
##         """
##         readop = self.ncl.read()
##         res = self.do_compound([readop])
##         self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

##     def testInvalidStateid(self):
##         """READ with a (guessed) invalid stateid should return NFS4ERR_STALE_STATEID

##         Covered invalid equivalence classes: 20
##         """
##         # FIXME
##         stateid = stateid4(self.ncl, 0, "")
##         readop = self.ncl.read(stateid=0x123456789L)
##         res = self.do_compound([readop])
##         self.assert_status(res, [NFS4ERR_STALE_STATEID])

        

class ReaddirSuite(NFSSuite):
    """Test operation 26: READDIR

    FIXME: More testing of dircount/maxcount combinations.
    Note: maxcount represents READDIR4resok. Test this.
    fattr4_rdattr_error vs. global error

    Equivalence partitioning:
        
    Input Condition: current filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            not dir(11)
            no filehandle(12)
    Input Condition: cookie
        Valid equivalence classes:
            zero(20)
            nonzero valid cookie(21)
        Invalid equivalence classes:
            invalid cookie(22)
    Input Condition: cookieverf
        Valid equivalence classes:
            zero(30)
            nonzero valid verifier(31)
        Invalid equivalence classes:
            invalid verifier(32)
    Input Condition: dircount
        Valid equivalence classes:
            zero(40)
            nonzero(41)
        Invalid equivalence classes:
            -
    Input Condition: maxcount
        Valid equivalence classes:
            nonzero(50)
        Invalid equivalence classes:
            zero(51)
    Input Condition: attrbits
        Valid equivalence classes:
            all requests without FATTR4_*_SET (60)
        Invalid equivalence classes:
            requests with FATTR4_*_SET (61)

    Comments: It's not possible to cover eq. class 22, since the cookie is
    opaque to the client. 
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testFirst(self):
        """READDIR with cookie=0, maxcount=4096

        Covered valid equivalence classes: 10, 20, 30, 40, 50, 60
        """        
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=0, maxcount=4096,
                                        attr_request=[])
        res = self.do_compound([self.putrootfhop, readdirop])
        self.assert_OK(res)
        
        
    def testSubsequent(self):
        """READDIR with cookie from previus call

        Covered valid equivalence classes: 10, 21, 31, 41, 50, 60
        """
        # FIXME: Implement rest of testcase, as soon as
        # CITI supports dircount/maxcount. 
        self.info_message("(TEST NOT IMPLEMENTED)")

        # Call READDIR with small maxcount, to make sure not all
        # entries are returned. Save cookie. 

        # Call READDIR a second time with saved cookie.

    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotDir(self):
        """READDIR with non-dir (cfh) should give NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 11
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=0, maxcount=4096,
                                        attr_request=[])
        operations.append(readdirop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNoFh(self):
        """READDIR without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 12
        """
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=0, maxcount=4096,
                                        attr_request=[])
        res = self.do_compound([readdirop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])
        
    def testInvalidCookieverf(self):
        """READDIR with invalid cookieverf should return NFS4ERR_BAD_COOKIE

        Covered invalid equivalence classes: 32
        """
        # FIXME: Implement rest of testcase, as soon as
        # CITI supports dircount/maxcount. 
        self.info_message("(TEST NOT IMPLEMENTED)")

    def testMaxcountZero(self):
        """READDIR with maxcount=0 should return NFS4ERR_READDIR_NOSPC
        
        Covered invalid equivalence classes: 51
        """
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=0, maxcount=0,
                                        attr_request=[])
        res = self.do_compound([self.putrootfhop, readdirop])
        self.assert_status(res, [NFS4ERR_READDIR_NOSPC])

    def testWriteOnlyAttributes(self):
        """READDIR with attrs=FATTR4_*_SET should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 61

        Comments: See GetattrSuite.testWriteOnlyAttributes. 
        """
        attrmask = nfs4lib.list2attrmask([FATTR4_TIME_ACCESS_SET])
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=0, maxcount=4096,
                                        attr_request=attrmask)
        res = self.do_compound([self.putrootfhop, readdirop])
        self.assert_status(res, [NFS4ERR_INVAL])
        

    #
    # Extra tests.
    #
    def testUnaccessibleDir(self):
        """READDIR with (cfh) in unaccessible directory

        Extra test
        
        Comments: This test crashes/crashed the Linux server
        """
        # FIXME: This test is currently broken.
        self.info_message("(TEST DISABLED)")
        return
        lookupops = self.ncl.lookup_path(self.notaccessibledir)
        operations = [self.putrootfhop] + lookupops

        attrmask = nfs4lib.list2attrmask([FATTR4_TYPE, FATTR4_SIZE, FATTR4_TIME_MODIFY])
        readdirop = self.ncl.readdir_op(cookie=0, cookieverf="\x00",
                                        dircount=2, maxcount=4096,
                                        attr_request=attrmask)
        operations.append(readdirop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_ACCES])

    def testDots(self):
        """READDIR should not return . and .. in /doc

        Extra test

        Although some servers may actually support files named "." and
        "..", no files named "." or ".." should exist in /doc.
        """
        # Lookup fh for /doc
        fh = self.do_rpc(self.ncl.do_getfh, self.dirfile)

        # Get entries
        entries = self.do_rpc(self.ncl.do_readdir, fh)
        names = [entry.name for entry in entries]

        self.failIf("." in names,
                    "READDIR in /doc returned .-entry")

        self.failIf(".." in names,
                    "READDIR in /doc returned ..-entry")

    def testStrangeNames(self):
        """READDIR should obey OPEN naming policy

        Extra test

        Comments: Verifying that readdir obeys the same naming policy
        as OPEN.
        """
        self.init_connection()
        
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            print e
            return
        
        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        fh = self.do_rpc(self.ncl.do_getfh, self.tmp_dir) 
        entries = self.do_rpc(self.ncl.do_readdir, fh)
        readdir_names = [entry.name for entry in entries]

        # Verify that READDIR returned all accepted_names
        missing_names = []
        for name in accepted_names:
            if name not in readdir_names:
                missing_names.append(name)

        self.failIf(missing_names, "Missing names in READDIR results: %s" \
                    % missing_names)

        # ... and nothing more
        extra_names = []
        for name in readdir_names:
            if not name in accepted_names:
                extra_names.append(name)

        self.failIf(extra_names, "Extra names in READDIR results: %s" \
                    % extra_names)



class ReadlinkSuite(NFSSuite):
    """Test operation 27: READLINK

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            symlink(10)
        Invalid equivalence classes:
            not symlink or directory(11)
            directory(12)
            no filehandle(13)
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testReadlink(self):
        """READLINK on link

        Covered valid equivalence classes: 10
        """
        lookupops = self.ncl.lookup_path(self.linkfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.readlink_op())
        
        res = self.do_compound(operations)
        self.assert_OK(res)
        linkdata = res.resarray[-1].arm.arm.link
        self.failIf(linkdata != "fd0",
                    "link data was %s, should be fd0" % linkdata)
    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotSymlink(self):
        """READLINK on non-symlink objects should return NFS4ERR_INVAL

        Covered valid equivalence classes: 11
        """
        for pathcomps in [self.regfile,
                          self.blockfile,
                          self.charfile,
                          self.socketfile,
                          self.fifofile]:
            lookupops = self.ncl.lookup_path(pathcomps)
            operations = [self.putrootfhop] + lookupops
            operations.append(self.ncl.readlink_op())

            res = self.do_compound(operations)

            if res.status != NFS4ERR_INVAL:
                self.info_message("READLINK on %s did not return NFS4ERR_INVAL" \
                                  % pathcomps)
            
            self.assert_status(res, [NFS4ERR_INVAL])
            
    def testDirFh(self):
        """READLINK on a directory should return NFS4ERR_ISDIR

        Covered valid equivalence classes: 12
        """
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.readlink_op())

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_ISDIR])
        

    def testNoFh(self):
        """READLINK without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 13
        """
        readlinkop = self.ncl.readlink_op()
        res = self.do_compound([readlinkop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])



class RemoveSuite(NFSSuite):
    """Test operation 28: REMOVE

    # FIXME: Test (OPEN, REMOVE, WRITE) sequence. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            not dir(11)
            no filehandle(12)
    Input Condition: filename
        Valid equivalence classes:
            valid, existing name(20)
        Invalid equivalence classes:
            zerolength(21)
            non-utf8(22)
            non-existing name(23)
    """
    def setUp(self):
        NFSSuite.setUp(self)
        self.obj_name = "object1"
        self.lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)

    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """Valid REMOVE on existing object

        Covered valid equivalence classes: 10, 20
        """
        if not self.create_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        operations.append(self.ncl.remove_op(self.obj_name))
        res = self.do_compound(operations)
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotDir(self):
        """REMOVE with non-dir (cfh) should give NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 11
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.remove_op(self.obj_name))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNoFh(self):
        """REMOVE without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 12
        """
        removeop = self.ncl.remove_op(self.obj_name)
        res = self.do_compound([removeop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testZeroLengthTarget(self):
        """REMOVE with zero length target should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 21
        """
        if not self.create_object(): return
        operations = [self.putrootfhop] + self.lookup_dir_ops
        operations.append(self.ncl.remove_op(""))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8(self):
        """REMOVE with non-UTF8 components should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 22

        Comments: There is no need to create the object first; the
        UTF8 check should be done before verifying if the object exists. 
        """
        for name in self.get_invalid_utf8strings():
            operations = [self.putrootfhop] + self.lookup_dir_ops
            operations.append(self.ncl.remove_op(name))
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])

    def testNonExistent(self):
        """REMOVE on non-existing object should return NFS4ERR_NOENT

        Covered invalid equivalence classes: 23
        """
        operations = [self.putrootfhop] + self.lookup_dir_ops
        operations.append(self.ncl.remove_op(self.vaporfilename))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT])

    #
    # Extra tests. 
    #
    def _do_remove(self, name):
        # Lookup /doc
        operations = [self.putrootfhop] + self.ncl.lookup_path(self.dirfile)
        operations.append(self.ncl.remove_op(name))
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT, NFS4ERR_INVAL])
    
    def testDots(self):
        """REMOVE on . or .. should return NFS4ERR_NOENT or NFS4ERR_INVAL
        
        Extra test

        No files named . or .. should exist in doc directory
        """
        # name = .
        self._do_remove(".")

        # name = ..
        self._do_remove("..")

    def testValidNames(self):
        """REMOVE should succeed on all legal names

        Extra test

        Comments: This test tries REMOVE on all names returned from try_file_names()
        """
        # This test testes the lookup part of REMOVE
        self.init_connection()

        # Save files for REMOVE
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            print e
            return
        
        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        # Ok, lets try REMOVE on all accepted names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in accepted_names:
            operations = [self.putrootfhop] + lookup_dir_ops
            operations.append(self.ncl.remove_op(filename))
            res = self.do_compound(operations)
            self.assert_OK(res)

    def testInvalidNames(self):
        """REMOVE should fail with NFS4ERR_NOENT on all unexisting, invalid file names

        Extra test

        Comments: Tries REMOVE on rejected file names from
        try_file_names().  NFS4ERR_INVAL should NOT be returned in this case, although
        the server rejects creation of objects with these names
        """
        self.init_connection()
        try:
            (accepted_names, rejected_names) = self.try_file_names()
        except SkipException, e:
            print e
            return
        
        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        # Ok, lets try REMOVE on all rejected names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in rejected_names:
            operations = [self.putrootfhop] + lookup_dir_ops
            operations.append(self.ncl.remove_op(filename))
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_NOENT])
        

class RenameSuite(NFSSuite):
    """Test operation 29: RENAME

    FIXME: Test renaming of a named attribute
    to be a regular file and vice versa.

    Equivalence partitioning:

    Input Condition: saved filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            non-dir(11)
            no filehandle(12)
            invalid filehandle(13)
    Input Condition: oldname
        Valid equivalence classes:
            valid name(20)
        Invalid equivalence classes:
            non-existent name(21)
            zerolength(22)
            non-utf8(23)
    Input Condition: current filehandle
        Valid equivalence classes:
            dir(30)
        Invalid equivalence classes:
            non-dir(31)
            no filehandle(32)
            invalid filehandle(33)
    Input Condition: newname
        Valid equivalence classes:
            valid name(40)
        Invalid equivalence classes:
            zerolength(41)
            non-utf8(42)

    Comments: It's not possible to cover eq. class 32, since saving a filehandle
    gives a current filehandle as well. 
    """
    def setUp(self):
        NFSSuite.setUp(self)
        self.oldname = "object1"
        self.obj_name = self.oldname # Easier call of create_object()
        self.newname = "object2"

        self.lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)

    def _prepare_operation(self):
        operations = [self.putrootfhop]
        
        # Lookup source and save FH
        operations.extend(self.lookup_dir_ops)
        operations.append(self.ncl.savefh_op())

        # Lookup target directory
        operations.append(self.putrootfhop)
        operations.extend(self.lookup_dir_ops)

        return operations

    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """Test valid RENAME operation

        Covered valid equivalence classes: 10, 20, 30, 40
        """
        if not self.create_object(): return

        operations = self._prepare_operation()

        # Rename
        renameop = self.ncl.rename_op(self.oldname, self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testSfhNotDir(self):
        """RENAME with non-dir (sfh) should return NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 11
        """
        operations = [self.putrootfhop]
        
        # Lookup source and save FH
        operations.extend(self.ncl.lookup_path(self.regfile))
        operations.append(self.ncl.savefh_op())

        # Lookup target directory
        operations.append(self.putrootfhop)
        operations.extend(self.lookup_dir_ops)

        # Rename
        renameop = self.ncl.rename_op(self.oldname, self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNoSfh(self):
        """RENAME without (sfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 12
        """
        # Lookup target directory
        operations = [self.putrootfhop]
        operations.extend(self.lookup_dir_ops)
        
        # Rename
        renameop = self.ncl.rename_op(self.oldname, self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    # FIXME: Cover eq. class 13.

    def testNonExistent(self):
        """RENAME on non-existing object should return NFS4ERR_NOENT

        Covered invalid equivalence classes: 21
        """
        if not self.create_object(): return
        operations = self._prepare_operation()

        # Rename
        renameop = self.ncl.rename_op("vapor_object", self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT])

    def testZeroLengthOldname(self):
        """RENAME with zero length oldname should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 22
        """
        if not self.create_object(): return
        operations = self._prepare_operation()

        # Rename
        renameop = self.ncl.rename_op("", self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8Oldname(self):
        """RENAME with non-UTF8 oldname should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 23

        Comments: There is no need to create the object first; the
        UTF8 check should be done before verifying if the object exists. 
        """
        for name in self.get_invalid_utf8strings():
            operations = self._prepare_operation()

            # Rename
            renameop = self.ncl.rename_op(name, self.newname)
            operations.append(renameop)
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])
        
    def testCfhNotDir(self):
        """RENAME with non-dir (cfh) should return NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 31
        """
        operations = [self.putrootfhop]
        
        # Lookup source and save FH
        operations.extend(self.lookup_dir_ops)
        operations.append(self.ncl.savefh_op())

        # Lookup target directory
        operations.append(self.putrootfhop)
        operations.extend(self.ncl.lookup_path(self.regfile))

        # Rename
        renameop = self.ncl.rename_op(self.oldname, self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    # FIXME: Cover eq. class 33.
    
    def testZeroLengthNewname(self):
        """RENAME with zero length newname should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 41
        """
        if not self.create_object(): return
        operations = self._prepare_operation()

        # Rename
        renameop = self.ncl.rename_op(self.oldname, "")
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])
    
    def testNonUTF8Newname(self):
        """RENAME with non-UTF8 newname should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 42
        """
        for name in self.get_invalid_utf8strings():
            # Create the object to rename 
            if not self.create_object(): return
            operations = self._prepare_operation()

            # Rename
            renameop = self.ncl.rename_op(self.oldname, name)
            operations.append(renameop)
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])

    #
    # Extra tests. 
    #
    def _do_test_oldname(self, oldname):
        operations = self._prepare_operation()
        renameop = self.ncl.rename_op(oldname, self.newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT, NFS4ERR_INVAL])

    def _do_test_newname(self, newname):
        operations = self._prepare_operation()
        renameop = self.ncl.rename_op(self.oldname, newname)
        operations.append(renameop)
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4_OK, NFS4ERR_INVAL])
    
    def testDotsOldname(self):
        """RENAME with oldname containing . or .. should return NOENT/INVAL

        Extra test
        
        No files named . or .. should exist in /doc directory. RENAME should
        return NFS4ERR_NOENT (if server supports "." and ".." as file names)
        or NFS4ERR_INVAL. 
        """
        self._do_test_oldname(".")
        self._do_test_oldname("..")

    def testDotsNewname(self):
        """RENAME with newname . or .. should succeed or return OK/INVAL

        Extra test
        """
        # Create dummy object
        if not self.create_object(): return
        # Try to rename it to "."
        self._do_test_newname(".")

        # Create dummy object
        if not self.create_object(): return
        # Try to rename it to ".."
        self._do_test_newname("..")

    def testNamingPolicy(self):
        """RENAME should obey OPEN file name creation policy

        Extra test
        """
        # This test testes the create part of RENAME. 
        self.init_connection()

        try:
            (x, rejected_names_open) = self.try_file_names(creator=self.create_via_open)
            self.info_message("Rejected file names by OPEN: %s" \
                              % repr(rejected_names_open))
            
            (x, rejected_names_rename) = self.try_file_names(creator=self.create_via_rename)
            self.info_message("Rejected file names by RENAME: %s" \
                              % repr(rejected_names_rename))
            
            
            self.failIf(rejected_names_open != rejected_names_rename,
                        "RENAME does not obey OPEN naming policy")
        except SkipException, e:
            print e

    def testValidNames(self):
        """RENAME should succeed on all legal names

        Extra test

        Comments: This test tries RENAME on all names returned from try_file_names()
        """
        # This test testes the lookup part of RENAME. 
        self.init_connection()

        # Saved files for 
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            print e
            return
        
        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        # Ok, lets try RENAME on all accepted names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in accepted_names:
            operations = self._prepare_operation()
            renameop = self.ncl.rename_op(filename, self.newname)
            operations.append(renameop)
            res = self.do_compound(operations)
            self.assert_OK(res)
            # Remove file. 
            self.ncl.do_rpc(self.ncl.do_remove, self.tmp_dir + [self.newname])

    def testInvalidNames(self):
        """RENAME should fail with NFS4ERR_NOENT on all unexisting, invalid file names

        Extra test

        Comments: Tries RENAME on rejected file names from
        try_file_names().  NFS4ERR_INVAL should NOT be returned in this case, although
        the server rejects creation of objects with these names
        """
        self.init_connection()

        try:
            (accepted_names, rejected_names) = self.try_file_names()
        except SkipException, e:
            print e
            return
        
        self.info_message("Rejected file names by OPEN: %s" % repr(rejected_names))

        # Ok, lets try RENAME on all rejected names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in rejected_names:
            operations = self._prepare_operation()
            renameop = self.ncl.rename_op(filename, self.newname)
            operations.append(renameop)
            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_NOENT])

## class RenewSuite(NFSSuite):
##     """Test operation 30: RENEW
##     """
##     # FIXME
##     pass


class RestorefhSuite(NFSSuite):
    """Test operation 31: RESTOREFH

    Equivalence partitioning:

    Input Condition: saved filehandle
        Valid equivalence classes:
            valid filehandle(10)
        Invalid equivalence classes:
            no filehandle(11)

    Comments: We do not test restoration of a invalid filehandle,
    since it's hard to save one. It's not possible to PUTFH an invalid
    filehandle, for example.
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """SAVEFH and RESTOREFH

        Covered valid equivalence classes: 10

        Comments: Also tests SAVEFH operation. 
        """
        # The idea is to use a sequence of operations like this:
        # PUTROOTFH, LOOKUP, GETFH#1, SAVEFH, LOOKUP, RESTOREFH, GETH#2.
        # If this procedure succeeds and result from GETFH#1 and GETFH#2 match,
        # things should be OK.

        # Lookup a file, get and save FH. 
        operations = [self.putrootfhop]
        operations.extend(self.ncl.lookup_path(self.regfile))
        operations.append(self.ncl.getfh_op())
        operations.append(self.ncl.savefh_op())

        # Lookup another file.
        operations.append(self.putrootfhop)
        operations.extend(self.ncl.lookup_path(self.hello_c))

        # Restore saved fh and get fh. 
        operations.append(self.ncl.restorefh_op())
        operations.append(self.ncl.getfh_op())
        
        res = self.do_compound(operations)
        self.assert_OK(res)

        # putrootfh + #lookups
        getfh1index = 1 + len(self.regfile) 
        fh1 = res.resarray[getfh1index].arm.arm.object
        # getfh1index + savefh + putrootfh + #lookups + restorefh + getfh
        getfh2index = getfh1index + 2 + len(self.hello_c) + 2
        fh2 = res.resarray[getfh2index].arm.arm.object 
        self.failIf(fh1 != fh2, "restored FH does not match saved FH")

    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """RESTOREFH without (sfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 11
        """
        res = self.do_compound([self.ncl.restorefh_op()])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])
        

class SavefhSuite(NFSSuite):
    """Test operation 32: SAVEFH

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            valid filehandle(10)
        Invalid equivalence classes:
            no filehandle(11)

    Comments: Equivalence class 10 is covered by
    RestorefhSuite.testValid.
    """
    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """SAVEFH without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 11
        """
        res = self.do_compound([self.ncl.savefh_op()])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])
    

class SecinfoSuite(NFSSuite):
    """Test operation 33: SECINFO

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            dir(10)
        Invalid equivalence classes:
            not dir(11)
            invalid filehandle(12)
    Input Condition: name
        Valid equivalence classes:
            valid name(20)
        Invalid equivalence classes:
            non-existent object(21)
            zerolength(22)
            non-utf8(23)

    Comments: It's hard to cover eq. class 12, since it's not possible
    PUTFH an invalid filehandle. 
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """SECINFO on existing file

        Covered valid equivalence classes: 10, 20
        """
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op("README"))

        res = self.do_compound(operations)
        self.assert_OK(res)

        # Make sure at least one security mechanisms is returned.
        mechanisms = res.resarray[-1].arm.arm
        self.failIf(len(mechanisms) < 1,
                    "SECINFO returned no security mechanisms")

    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotDir(self):
        """SECINFO with non-dir (cfh) should give NFS4ERR_NOTDIR

        Covered invalid equivalence classes: 11
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op("README"))

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOTDIR])

    def testNonExistent(self):
        """SECINFO on non-existing object should return NFS4ERR_NOENT

        Covered invalid equivalence classes: 21
        """
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op(self.vaporfilename))

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT])

    def testZeroLengthName(self):
        """SECINFO with zero length name should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 22
        """
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op(""))

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8(self):
        """SECINFO with non-UTF8 name should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 23

        Comments: It does'nt matter that the files does not exist; the UTF8
        check should return NFS4ERR_INVAL anyway. 
        """
        for name in self.get_invalid_utf8strings():
            operations = [self.putrootfhop] 
            operations.append(self.ncl.secinfo_op(name))

            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])

    #
    # Extra tests. 
    #
    def testRPCSEC_GSS(self):
        """SECINFO must return at least RPCSEC_GSS

        Extra test
        """
        # FIXME: Since the Linux server always returns NFS4ERR_NOTSUPP right
        # know, this is untested code.
        # FIXME: Also verify that all Kerberos and LIPKEY security triples
        # listed in section 3.2.1.1 and 3.2.1.2 are supported.
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op("README"))

        res = self.do_compound(operations)
        self.assert_OK(res)
        mechanisms = res.resarray[-1].arm.arm
        found_rpcsec_gss = 0

        for mech in mechanisms:
            if mech.flavor == RPCSEC_GSS:
                found_rpcsec_gss = 1

        self.failIf(not found_rpcsec_gss,
                    "SECINFO did not return (mandatory) flavor RPCSEC_GSS")


    def _assert_secinfo_response(self, name):
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.secinfo_op(name))
        
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOENT, NFS4ERR_INVAL])

    def testDots(self):
        """SECINFO on . and .. should return NOENT/INVAL in /doc

        Extra test
        """
        # . should not exist in doc dir
        if not self.make_sure_nonexistent(".", self.dirfile): return
        self._assert_secinfo_response(".")

        # .. should not exist in doc dir
        if not self.make_sure_nonexistent("..", self.dirfile): return
        self._assert_secinfo_response("..")

    # FIXME: Add file name policy tests: testValidNames/testInvalidNames
    # (like with LOOKUP, REMOVE and RENAME)

        
class SetattrSuite(NFSSuite):
    """Test operation 34: SETATTR

    FIXME: Test invalid filehandle. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            file(10)
            dir(11)
            block(12)
            char(13)
            link(14)
            socket(15)
            FIFO(16)
            attribute directory(17)
            named attribute(18)
        Invalid equivalence classes:
            no filehandle(19)
    Input Condition: stateid
        Valid equivalence classes:
            all bits zero(20)
            all bits one(21)
            valid stateid from open(22)
        Invalid equivalence classes:
            invalid stateid(23)
    Input Condition: obj_attributes.attrmask
        Valid equivalence classes:
            writeable attributes without object_size(30)
            writeable attributes with object_size(31)
        Invalid equivalence classes:
            non-writeable attributes(32)
    Input Condition: obj_attributes.attr_vals
        Valid equivalence classes:
            valid attributes(40)
        Invalid equivalence classes:
            invalid attributes(41)
            attr with invalid utf8(42)
    """

    def setUp(self):
        NFSSuite.setUp(self)
        self.new_mode = 0775

    def _setattr_op(self, stateid):
        attrmask = nfs4lib.list2attrmask([FATTR4_MODE])
        dummy_ncl = nfs4lib.DummyNcl()
        dummy_ncl.packer.pack_uint(self.new_mode)
        attr_vals = dummy_ncl.packer.get_buffer()
        obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
        return self.ncl.setattr_op(stateid, obj_attributes)

    def _check_notsupp(self, res):
        if res.status == NFS4ERR_ATTRNOTSUPP:
            self.info_message("SETATTR(FATTR4_MODE) not supported on %s" \
                              % self.regfile)
            return TRUE
        
        return FALSE

    def _getattr_check(self, lookupops):
        # Ok, SETATTR succeeded. Check that GETATTR matches.
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.getattr([FATTR4_MODE]))
        res = self.do_compound(operations)
        if res.status != NFS4_OK:
            self.info_message("GETATTR failed, cannot verify if SETATTR was done right")
            return

        obj = res.resarray[-1].arm.arm.obj_attributes
        d =  nfs4lib.fattr2dict(obj)
        mode = d.get("mode") 
        self.failIf(mode != self.new_mode,
                    "GETATTR after SETATTR(FATTR4_MODE) returned different mode")

    def _valid_setattr(self, file, stateval):
        lookupops = self.ncl.lookup_path(file)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, stateval)
        operations.append(self._setattr_op(stateid))

        res = self.do_compound(operations)
        if self._check_notsupp(res): return

        self.assert_status(res, [NFS4_OK, NFS4ERR_ATTRNOTSUPP])
        self._getattr_check(lookupops)

    def _invalid_setattr(self, file, stateval):
        lookupops = self.ncl.lookup_path(file)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, stateval)
        operations.append(self._setattr_op(stateid))

        res = self.do_compound(operations)
        if self._check_notsupp(res): return
        self.assert_status(res, [NFS4_OK, NFS4ERR_INVAL])

    #
    # Testcases covering valid equivalence classes.
    #
    def testStateidOnes(self):
        """SETATTR(FATTR4_MODE) on regular file with stateid=ones
        
        Covered valid equivalence classes: 10, 21, 30, 40
        """
        stateval = nfs4lib.long2opaque(0xffffffffffffffffffffffffL)
        self._valid_setattr(self.regfile, stateval)
        
    def testDir(self):
        """SETATTR(FATTR4_MODE) on directory

        Covered valid equivalence classes: 11, 20, 30, 40
        """
        self._valid_setattr(self.dirfile, "")

    def testBlock(self):
        """SETATTR(FATTR4_MODE) on block device

        Covered valid equivalence classes: 12, 20, 30, 40
        """
        self._valid_setattr(self.blockfile, "")
        

    def testChar(self):
        """SETATTR(FATTR4_MODE) on char device

        Covered valid equivalence classes: 13, 20, 30, 40
        """
        self._valid_setattr(self.charfile, "")

    def testLink(self):
        """SETATTR(FATTR4_MODE) on symbolic link

        Covered valid equivalence classes: 14, 20, 30, 40

        Comments: The response to mode setting on a symbolic link is
        server dependent; about any response is valid. We just test
        and print the result. 
        """
        lookupops = self.ncl.lookup_path(self.linkfile)
        operations = [self.putrootfhop] + lookupops
        stateid = stateid4(self.ncl, 0, "")
        operations.append(self._setattr_op(stateid))
        res = self.do_compound(operations)
        self.info_message("SETATTR(FATTR4_MODE) on symlink returned %s" \
                          % nfsstat4_id[res.status])
        
    def testSocket(self):
        """SETATTR(FATTR4_MODE) on socket

        Covered valid equivalence classes: 15, 20, 30, 40
        """
        self._valid_setattr(self.socketfile, "")
        
    def testFIFO(self):
        """SETATTR(FATTR4_MODE) on FIFO

        Covered valid equivalence classes: 16, 20, 30, 40
        """
        self._valid_setattr(self.fifofile, "")
        
    def testNamedattrdir(self):
        """SETATTR(FATTR4_MODE) on named attribute directory

        Covered valid equivalence classes: 17, 20, 30, 40
        """
        # FIXME: Implement.
        self.info_message("(TEST NOT IMPLEMENTED)")

    def testNamedattr(self):
        """SETATTR(FATTR4_MODE) on named attribute 

        Covered valid equivalence classes: 18, 20, 30, 40
        """
        # FIXME: Implement.
        self.info_message("(TEST NOT IMPLEMENTED)")

    def testChangeSize(self):
        """SETATTR(FATTR4_MODE) with changes to file size and valid stateid

        Covered valid equivalence classes: 10, 22, 31, 40
        """
        # FIXME: Implement.
        self.info_message("(TEST NOT IMPLEMENTED)")

    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """SETATTR without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 19
        """
        stateid = stateid4(self.ncl, 0, "")
        operations = [self._setattr_op(stateid)]

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testInvalidStateid(self):
        """SETATTR with invalid stateid should return NFS4ERR_BAD_STATEID

        Covered invalid equivalence classes: 23
        """
        # FIXME: Implement.
        self.info_message("(TEST NOT IMPLEMENTED)")
        # FIXME: Move to common method. Check correctness. 
        #stateval = nfs4lib.long2opaque(0x123456)
        #self._valid_setattr(self.regfile, stateval)

    def testNonWriteable(self):
        """SETATTR(FATTR4_LINK_SUPPORT) should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 32

        Comments: FATTR4_LINK_SUPPORT is a read-only attribute and cannot be
        changed via SETATTR. 
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, "")

        attrmask = nfs4lib.list2attrmask([FATTR4_LINK_SUPPORT])
        dummy_ncl = nfs4lib.DummyNcl()
        dummy_ncl.packer.pack_bool(FALSE)
        attr_vals = dummy_ncl.packer.get_buffer()
        obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
        operations.append(self.ncl.setattr_op(stateid, obj_attributes))

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testInvalidAttr(self):
        """SETATTR with invalid attribute data should return NFS4ERR_BADXDR

        Covered invalid equivalence classes: 41

        Comments: This testcase try to set FATTR4_MODE but does not send any
        mode data. The server should return NFS4ERR_BADXDR. 
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, "")
        attrmask = nfs4lib.list2attrmask([FATTR4_MODE])
        attr_vals = ""
        obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
        operations.append(self.ncl.setattr_op(stateid, obj_attributes))

        res = self.do_compound(operations)

        self.assert_status(res, [NFS4ERR_BADXDR])

    def testNonUTF8(self):
        """SETATTR(FATTR4_MIMETYPE) with non-utf8 string should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 42

        """
        for name in self.get_invalid_utf8strings():
            lookupops = self.ncl.lookup_path(self.regfile)
            operations = [self.putrootfhop] + lookupops
            
            stateid = stateid4(self.ncl, 0, "")
            # Create attribute
            attrmask = nfs4lib.list2attrmask([FATTR4_MIMETYPE])
            dummy_ncl = nfs4lib.DummyNcl()
            dummy_ncl.packer.pack_utf8string(name)
            attr_vals = dummy_ncl.packer.get_buffer()
            obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
            # Setattr operation
            operations.append(self.ncl.setattr_op(stateid, obj_attributes))

            res = self.do_compound(operations)
            
            if self._check_notsupp(res): return

            self.assert_status(res, [NFS4ERR_INVAL])

    #
    # Extra tests. 
    #
    def _settime(self, dummy_ncl, time):
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        stateid = stateid4(self.ncl, 0, "")
        
        attrmask = nfs4lib.list2attrmask([FATTR4_TIME_MODIFY_SET])
        settime = settime4(dummy_ncl, set_it=SET_TO_CLIENT_TIME4, time=time)
        settime.pack()
        attr_vals = dummy_ncl.packer.get_buffer()
        obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
        operations.append(self.ncl.setattr_op(stateid, obj_attributes))

        res = self.do_compound(operations)
        return res
    
    def testInvalidTime(self):
        """SETATTR(FATTR4_TIME_MODIFY_SET) with invalid nseconds

        Extra test

        nseconds larger than 999999999 are considered invalid.
        SETATTR(FATTR4_TIME_MODIFY_SET) should return NFS4ERR_INVAL on
        such values. 
        """
        dummy_ncl = nfs4lib.DummyNcl()

        # First, try to set the date to 900 000 000 = 1998-07-09
        # to check if setting time_modify is possible at all. 
        time = nfstime4(dummy_ncl, seconds=500000000, nseconds=0)
        res = self._settime(dummy_ncl, time)
        if res.status == NFS4ERR_NOTSUPP:
            self.info_message("Attribute time_modify_set is not supported, "
                              "skipping test")
            return

        # If servers supports the attribute but does not accept the 
        # date 1998-07-09, consider it broken. 
        self.assert_OK(res)

        # Ok, lets try nseconds = 1 000 000 000
        dummy_ncl = nfs4lib.DummyNcl()
        time = nfstime4(dummy_ncl, seconds=500000000, nseconds=int(1E9))
        res = self._settime(dummy_ncl, time)
        self.assert_status(res, [NFS4ERR_INVAL])
        

class SetclientidSuite(NFSSuite):
    """Test operation 35: SETCLIENTID

    FIXME: Test cases that trigger NFS4ERR_CLID_INUSE. 

    Equivalence partitioning:

    Input Condition: client.verifier
        Valid equivalence classes:
            all input(10)
        Invalid equivalence classes:
            -
    Input Condition: client.id
        Valid equivalence classes:
            all input(20)
        Invalid equivalence classes:
            -
    Input Condition: callback.cb_program
            all input(30)
        Invalid equivalence classes:
            -
    Input Condition: callback.cb_location
            all input(40)
        Invalid equivalence classes:
            -

    Comments: If client has never connected to the server, every
    client.verifier and client.id is valid. All callback data is also
    allowed as input, but failing to provide the correct addres means
    callbacks will not be used. 
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """Simple SETCLIENTID

        Covered valid equivalence classes: 10, 20, 30, 40
        """
        # client
        verifier = self.ncl.gen_random_64()
        id = self.ncl.gen_uniq_id()
        client = nfs_client_id4(self.ncl, verifier, id)

        # callback
        cb_program = 0
        r_netid = "udp"
        # FIXME
        # Internet, Port number, IP, RFU
        r_addr = "0002" + "0000" + "00112233" + "00" 
        cb_location = clientaddr4(self.ncl, r_netid, r_addr)
        callback = cb_client4(self.ncl, cb_program, cb_location)
        
        setclientidop = self.ncl.setclientid_op(client, callback)
        res = self.do_compound([setclientidop])
        self.assert_OK(res)

    #
    # Extra tests.
    #
    def _set(self, ncl, id):
        setclientidop = ncl.setclientid(id=id)
        res = self.do_compound([setclientidop], ncl=ncl)
        return res

    def _confirm(self, ncl, clientid):
        setclientid_confirmop = ncl.setclientid_confirm_op(clientid)
        res = self.do_compound([setclientid_confirmop], ncl=ncl)
        return res
    
    def testInUse(self):
        """SETCLIENTID with same nfs_client_id.id should return NFS4ERR_CLID_INUSE

        Extra test
        """
        id = self.ncl.gen_uniq_id()

        # 1st SETCLIENTID + SETCLIENTID_CONFIRM
        #self._set_and_confirm(self.ncl, id)
        res = self._set(self.ncl, id)
        self.assert_OK(res)
        clientid = res.resarray[0].arm.arm.clientid
        res = self._confirm(self.ncl, clientid)
        self.assert_OK(res)
        

        # 2nd SETCLIENTID 
        ncl2 = self.create_client(UID+1, GID+1)
        res = self._set(ncl2, id)
        self.assert_status(res, [NFS4ERR_CLID_INUSE])
        # FIXME: Should NFS4ERR_CLID_INUSE be returned on SETCLIENTID
        # or SETCLIENTID_CONFIRM?
        #clientid = res.resarray[0].arm.arm.clientid
        #res = self._confirm(self.ncl, clientid)
        #self.assert_OK(res)
        
    
class SetclientidconfirmSuite(NFSSuite):
    """Test operation 36: SETCLIENTID_CONFIRM

    Equivalence partitioning:

    Input Condition: clientid
        Valid equivalence classes:
            valid clientid(10)
        Invalid equivalence classes:
            stale clientid(11)
    """
    # Override setUp. Just connect, don't do SETCLIENTID etc. 
    def setUp(self):
        self.connect()

    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """SETCLIENTID_CONFIRM on valid verifier
        
        Covered valid equivalence classes: 10
        """
        
        # SETCLIENTID
        setclientidop = self.ncl.setclientid()
        res =  self.do_compound([setclientidop])
        self.assert_OK(res)
        clientid = res.resarray[0].arm.arm.clientid
        
        # SETCLIENTID_CONFIRM
        setclientid_confirmop = self.ncl.setclientid_confirm_op(clientid)
        res =  self.do_compound([setclientid_confirmop])
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testStale(self):
        """SETCLIENTID_CONFIRM on stale vrf should return NFS4ERR_STALE_CLIENTID

        Covered invalid equivalence classes: 11
        """
        clientid = self.get_invalid_clientid()
        setclientid_confirmop = self.ncl.setclientid_confirm_op(clientid)
        res =  self.do_compound([setclientid_confirmop])
        self.assert_status(res, [NFS4ERR_STALE_CLIENTID])


class VerifySuite(NFSSuite):
    """Test operation 37: VERIFY

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            link(1)
            block(2)
            char(3)
            socket(4)
            FIFO(5)
            dir(6)
            file(7)
        Invalid equivalence classes:
            invalid filehandle(8)
    Input Condition: fattr.attrmask
        Valid equivalence classes:
            valid attribute(9)
        Invalid equivalence classes:
            invalid attrmask(10) (FATTR4_*_SET)
    Input Condition: fattr.attr_vals
        Valid equivalence classes:
            same attributes(11)
            not same attributes(12)
        Invalid equivalence classes:
            attr with invalid utf8(13)

    """
    
    #
    # Testcases covering valid equivalence classes.
    #
    def testSame(self):
        """VERIFY with same attributes should execute remaining ops

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 9, 11
        """
        # Fetch sizes for all objects
        obj_sizes = self.ncl.lookup_all_objects_and_sizes()
        
        # For each type of object, do verify with same filesize
        # get filesize again, and check if it match previous size.
        for (lookupops, objsize) in obj_sizes:
            operations = [self.putrootfhop] + lookupops
            
            # Verify op
            attrmask = nfs4lib.list2attrmask([FATTR4_SIZE])
            # Size attribute is 8 bytes. 
            attr_vals = nfs4lib.long2opaque(objsize, 8)
            obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
            operations.append(self.ncl.verify_op(obj_attributes))

            # New getattr
            operations.append(self.ncl.getattr([FATTR4_SIZE]))

            res = self.do_compound(operations)

            self.assert_OK(res)

            # Assert the new getattr was executed.
            # File sizes should match. 
            obj = res.resarray[-1].arm.arm.obj_attributes
            d =  nfs4lib.fattr2dict(obj)
            new_size = d["size"]
            self.failIf(objsize != new_size,
                        "GETATTR after VERIFY returned different filesize")

    def testNotSame(self):
        """VERIFY with not same attributes should return NFS4ERR_NOT_SAME

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7, 9, 12
        """
        # Fetch sizes for all objects
        obj_sizes = self.ncl.lookup_all_objects_and_sizes()
        
        # For each type of object, do verify with wrong filesize. 
        for (lookupops, objsize) in obj_sizes:
            operations = [self.putrootfhop] + lookupops
            
            # Verify op
            attrmask = nfs4lib.list2attrmask([FATTR4_SIZE])
            # Size attribute is 8 bytes. 
            attr_vals = nfs4lib.long2opaque(objsize + 17, 8)
            obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
            operations.append(self.ncl.verify_op(obj_attributes))

            res = self.do_compound(operations)

            self.assert_status(res, [NFS4ERR_NOT_SAME])

    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """VERIFY without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 8
        """
        attrmask = nfs4lib.list2attrmask([FATTR4_SIZE])
        # Size attribute is 8 bytes. 
        attr_vals = nfs4lib.long2opaque(17, 8)
        obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
        verifyop = self.ncl.verify_op(obj_attributes)
        res = self.do_compound([verifyop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])

    def testWriteOnlyAttributes(self):
        """VERIFY with FATTR4_*_SET should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 10

        Comments: See GetattrSuite.testWriteOnlyAttributes. 
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        operations = [self.putrootfhop] + lookupops

        # Verify
        attrmask = nfs4lib.list2attrmask([FATTR4_TIME_ACCESS_SET])
        # Size attribute is 8 bytes. 
        attr_vals = nfs4lib.long2opaque(17, 8)
        obj_attributes = nfs4lib.fattr4(self.ncl, attrmask, attr_vals)
        operations.append(self.ncl.verify_op(obj_attributes))
        
        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_INVAL])

    def testNonUTF8(self):
        """VERIFY with non-UTF8 FATTR4_OWNER should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 13
        """
        lookupops = self.ncl.lookup_path(self.regfile)
        for name in self.get_invalid_utf8strings():
            operations = [self.putrootfhop] + lookupops
            
            # Verify op
            attrmask = nfs4lib.list2attrmask([FATTR4_OWNER])
            dummy_ncl = nfs4lib.DummyNcl()
            dummy_ncl.packer.pack_utf8string(name)
            attr_vals = dummy_ncl.packer.get_buffer()
            obj_attributes = fattr4(self.ncl, attrmask, attr_vals)
            operations.append(self.ncl.verify_op(obj_attributes))

            res = self.do_compound(operations)
            self.assert_status(res, [NFS4ERR_INVAL])


## class WriteSuite(NFSSuite):
##     """Test operation 38: WRITE

##     FIXME: Complete. 
##     FIXME: Write to named attribute. 

##     Equivalence partitioning:

##     Input Condition: current filehandle
##         Valid equivalence classes:
##             file(10)
##             named attribute(11)
##         Invalid equivalence classes:
##             dir(12)
##             special device files(13)
##             invalid filehandle(14)
##     Input Condition: stateid
##         Valid equivalence classes:
##             all bits zero(20)
##             all bits one(21)
##             valid stateid from open(22)
##         Invalid equivalence classes:
##             invalid stateid(23)
##     Input Condition: offset
##         Valid equivalence classes:
##             zero(30)
##             nonzero(31)
##         Invalid equivalence classes:
##             -
##     Input Condition: stable
##         Valid equivalence classes:
##             UNSTABLE4(40)
##             DATA_SYNC4(41)
##             FILE_SYNC4(42)
##         Invalid equivalence classes:
##             invalid constant(43)
##     Input Condition: data
##         Valid equivalence classes:
##             no data(50)
##             some data(51)
##         Invalid equivalence classes:
##             -
##     """
##     #
##     # Testcases covering valid equivalence classes.
##     #
##     def testSimpleWrite(self):
##         """WRITE with stateid=zeros, no data and UNSTABLE4

##         Covered valid equivalence classes: 10, 20, 30, 40, 50
##         """
##         self.info_message("(TEST NOT IMPLEMENTED)")

##     def testStateidOne(self):
##         """WRITE with stateid=ones and DATA_SYNC4

##         Covered valid equivalence classes: 10, 21, 31, 41, 51
##         """
##         self.info_message("(TEST NOT IMPLEMENTED)")

##     def testWithOpen(self):
##         """WRITE with open and FILE_SYNC4

##         Covered valid equivalence classes: 10, 22, 30, 42, 51
##         """
##         self.info_message("(TEST NOT IMPLEMENTED)")
##     #
##     # Testcases covering invalid equivalence classes.
##     #
## ##     def testInvalid(self):
## ##         pass


class FilehandleSuite(NFSSuite):
    """Test different aspects of file handle management
    """
    def setUp(self):
        NFSSuite.setUp(self)
        self.obj_name = "object1"
        self.lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        self.lookup_obj = self.ncl.lookup_op(self.obj_name)
    
    def _verify_persistent(self):
        operations = [self.putrootfhop] + self.lookup_dir_ops + [self.lookup_obj]
        operations.append(self.ncl.getattr([FATTR4_FH_EXPIRE_TYPE]))
        res = self.do_compound(operations)
        self.assert_OK(res)

        obj = res.resarray[-1].arm.arm.obj_attributes
        d =  nfs4lib.fattr2dict(obj)
        fhtype = d["fh_expire_type"]
        self.failUnless(self._verify_fhtype(fhtype), "Invalid fh_expire_type")
        return (fhtype == FH4_PERSISTENT)
            
    def _verify_fhtype(self, fhtype):
        """Verify consistency for filehandle expire type"""
        if fhtype == FH4_PERSISTENT:
            return 1

        if (fhtype & FH4_VOLATILE_ANY) and (fhtype & (FH4_VOL_MIGRATION | FH4_VOL_RENAME)):
            # FH4_VOL_MIGRATION and FH4_VOL_RENAME may not be combined 
            # with FH4_VOLATILE_ANY
            return 0

        return 1
    
    def testStale(self):
        """Presenting persistent fh repr. deleted object should yield NFS4ERR_STALE

        Extra test

        See section 4.2.2 in specification. 
        """
        if not self.create_object(): return
        # This test requires persistent a filehandle
        if not self._verify_persistent():
            self.info_message("Directory fh is not persistent, which is required")
            self.info_message("by this test. Skipping")
            return

        # FIXME: Rest of test not implemented. Fix when I have access to a
        # server that provides persistent fh:s. 
        self.info_message("(TEST NOT IMPLEMENTED)")

#
# End of test suites
#
class MyTextTestResult(unittest._TextTestResult):
    def __init__(self, print_tracebacks, stream, descriptions, verbosity):
        unittest._TextTestResult.__init__(self, stream, descriptions, verbosity)
        if not print_tracebacks:
            self.printErrors = lambda: 0

    def getID(self, test):
        id = test.id()
        if id.startswith("__main__"):
            return id[9:]
        else:
            return id
    
    def startTest(self, test):
        unittest.TestResult.startTest(self, test)
        if self.showAll:
            self.stream.write(self.getID(test) + ":\n")
            self.stream.write(" " + self.getDescription(test))
            self.stream.write(" ... ")

class MyTextTestRunner(unittest.TextTestRunner):
    def __init__(self, print_tracebacks, stream=None, descriptions=None, verbosity=None):
        kwargs = {}
        if stream: kwargs["stream"] = stream
        if descriptions: kwargs["descriptions"] = descriptions
        if verbosity: kwargs["verbosity"] = verbosity
        unittest.TextTestRunner.__init__(self, **kwargs)
        self.print_tracebacks = print_tracebacks
    
    def _makeResult(self):
        ttr = MyTextTestResult(self.print_tracebacks, self.stream, self.descriptions, self.verbosity)
        return ttr
    

class TestProgram(unittest.TestProgram):
    USAGE = """\
Usage: %(progName)s [nfs://]host[:port]<prefix> [options] [test] [...]

<prefix> defaults to /. Use same prefix as for test_tree_net.py 

Options:
  -u, --udp        use UDP as transport (default)
  -t, --tcp        use TCP as transport
  -h, --help       Show this message
  -q, --quiet      Minimal output
  -v, --verbose    Verbose output, display tracebacks

Examples:
  %(progName)s                               - run default set of tests
  %(progName)s MyTestSuite                   - run suite 'MyTestSuite'
  %(progName)s MyTestSuite.testSomething     - run MyTestCase.testSomething
"""
    def parseArgs(self, argv):
        import getopt
        import re
        global host, port, transport, prefix

        self.verbosity = 2
        self.print_tracebacks = 0

        # Reorder arguments, so we can add options at the end 
        ordered_args = []
        for arg in argv[1:]:
            if arg.startswith("-"):
                ordered_args.insert(0, arg)
            else:
                ordered_args.append(arg)
        
        try:
            options, args = getopt.getopt(ordered_args, 'uthqv',
                                          ['help', 'quiet', 'udp', 'tcp', 'verbose'])
        except getopt.error, msg:
            self.usageExit(msg)
            
        for opt, value in options:
            if opt in ("-u", "--udp"):
                transport = "udp"
            if opt in ("-t", "--tcp"):
                transport = "tcp"
            if opt in ('-h','--help'):
                self.usageExit()
            if opt in ('-q','--quiet'):
                self.verbosity = 0
            if opt in ('-v','--verbose'):
                self.print_tracebacks = 1

        if len(args) < 1:
            self.usageExit()

        parse_result = nfs4lib.parse_nfs_url(args[0])
        if not parse_result:
            self.usageExit()

        (host, portstring, directory) = parse_result

        if not directory:
            directory = "/"
            
        prefix = os.path.join(directory, "nfs4st")

        if portstring:
            port = int(portstring)
        else:
            port = nfs4lib.NFS_PORT

        args = args[1:]
                    
        if len(args) == 0 and self.defaultTest is None:
            self.test = self.testLoader.loadTestsFromModule(self.module)
            return
        if len(args) > 0:
            self.testNames = args
        else:
            self.testNames = (self.defaultTest,)

        self.createTests()

    def runTests(self):
        self.testRunner = MyTextTestRunner(self.print_tracebacks, verbosity=self.verbosity)
        result = self.testRunner.run(self.test)
        sys.exit(not result.wasSuccessful())


main = TestProgram

if __name__ == "__main__":
    main()
