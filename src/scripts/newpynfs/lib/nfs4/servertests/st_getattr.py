from nfs4.nfs4_const import *
from environment import check, checklist
from nfs4.nfs4lib import fattr2dict, get_bitnumattr_dict

def _try_mandatory(t, env, path):
    c = env.c1
    mandatory = [attr.bitnum for attr in env.attr_info if attr.mandatory]
    ops = c.use_obj(path)
    ops += [c.getattr(mandatory)]
    res = c.compound(ops)
    check(res)
    attrs = fattr2dict(res.resarray[-1].arm.arm.obj_attributes)
    unsupp = []
    for attr in mandatory:
        if attr not in attrs:
            attrname = get_bitnumattr_dict()[attr]
            unsupp += [attrname]
    if unsupp:
        t.fail("Mandatory attribute(s) %s not supported" % 'and'.join(unsupp))

def _try_write_only(env, path):
    c = env.c1
    baseops = c.use_obj(path)
    wo = [attr for attr in env.attr_info if attr.writeonly]
    for attr in wo:
        ops = baseops + [c.getattr([FATTR4_SIZE, attr.bitnum])]
        res = c.compound(ops)
        check(res, NFS4ERR_INVAL,
              "GETATTR with write-only attr %s" % attr.name)
    
def _try_unknown(t, c, path):
    ops = c.use_obj(path) + [c.getattr([1000])]
    res = c.compound(ops)
    check(res, msg="GETTATTR with unknown attr")
    attrs = fattr2dict(res.resarray[-1].arm.arm.obj_attributes)
    if attrs:
        t.fail("GETTATTR with unknown attr returned %s" % str(attrs))

def _try_empty(t, c, path):
    ops = c.use_obj(path) + [c.getattr([])]
    res = c.compound(ops)
    check(res, msg="GETTATTR with empty attr list")
    attrs = fattr2dict(res.resarray[-1].arm.arm.obj_attributes)
    if attrs:
        t.fail("GETTATTR with empty attr list returned %s" % str(attrs))
    
def _try_supported(t, env, path):
    c = env.c1
    mandatory = sum([attr.mask for attr in env.attr_info if attr.mandatory])
    supported = c.supportedAttrs(path)
    if mandatory & supported != mandatory:
        t.fail("GETATTR(FATTR4_SUPPORTED_ATTRS) did not return "
               "all mandatory attributes")

def _try_long(env, path):
    c = env.c1
    all = [attr.bitnum for attr in env.attr_info if not attr.writeonly]
    ops = c.use_obj(path) + [c.getattr(all)]
    res = c.compound(ops)
    check(res, msg="Asking for all legal attributes")
    # Force unpack of data to make sure no corruption
    attrs = fattr2dict(res.resarray[-1].arm.arm.obj_attributes)

def testMandFile(t, env):
    """GETATTR should return all mandatory attributes

    FLAGS: getattr file all
    DEPEND: LOOKFILE
    CODE: GATT1r
    """
    _try_mandatory(t, env, env.opts.usefile)

def testMandDir(t, env):
    """GETATTR should return all mandatory attributes

    FLAGS: getattr dir all
    DEPEND: LOOKDIR
    CODE: GATT1d
    """
    _try_mandatory(t, env, env.opts.usedir)

def testMandFifo(t, env):
    """GETATTR should return all mandatory attributes

    FLAGS: getattr fifo all
    DEPEND: LOOKFIFO
    CODE: GATT1f
    """
    _try_mandatory(t, env, env.opts.usefifo)

def testMandLink(t, env):
    """GETATTR should return all mandatory attributes

    FLAGS: getattr symlink all
    DEPEND: LOOKLINK
    CODE: GATT1a
    """
    _try_mandatory(t, env, env.opts.uselink)

def testMandSocket(t, env):
    """GETATTR should return all mandatory attributes

    FLAGS: getattr socket all
    DEPEND: LOOKSOCK
    CODE: GATT1s
    """
    _try_mandatory(t, env, env.opts.usesocket)

def testMandChar(t, env):
    """GETATTR should return all mandatory attributes

    FLAGS: getattr char all
    DEPEND: LOOKCHAR
    CODE: GATT1c
    """
    _try_mandatory(t, env, env.opts.usechar)

def testMandBlock(t, env):
    """GETATTR should return all mandatory attributes

    FLAGS: getattr block all
    DEPEND: LOOKBLK
    CODE: GATT1b
    """
    _try_mandatory(t, env, env.opts.useblock)

def testNoFh(t, env):
     """GETATTR should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

     FLAGS: getattr emptyfh all
     CODE: GATT2
     """
     c = env.c1
     res = c.compound([c.getattr([FATTR4_SIZE])])
     check(res, NFS4ERR_NOFILEHANDLE, "GETATTR with no <cfh>")

def testWriteOnlyFile(t, env):
    """GETATTR with write-only attrs should return NFS4ERR_INVAL

    FLAGS: getattr file all
    DEPEND: LOOKFILE
    CODE: GATT3r
    """
    _try_write_only(env, env.opts.usefile)

def testWriteOnlyDir(t, env):
    """GETATTR with write-only attrs should return NFS4ERR_INVAL

    FLAGS: getattr dir all
    DEPEND: LOOKDIR
    CODE: GATT3d
    """
    _try_write_only(env, env.opts.usedir)

def testWriteOnlyFifo(t, env):
    """GETATTR with write-only attrs should return NFS4ERR_INVAL

    FLAGS: getattr fifo all
    DEPEND: LOOKFIFO
    CODE: GATT3f
    """
    _try_write_only(env, env.opts.usefifo)

def testWriteOnlyLink(t, env):
    """GETATTR with write-only attrs should return NFS4ERR_INVAL

    FLAGS: getattr symlink all
    DEPEND: LOOKLINK
    CODE: GATT3a
    """
    _try_write_only(env, env.opts.uselink)

def testWriteOnlySocket(t, env):
    """GETATTR with write-only attrs should return NFS4ERR_INVAL

    FLAGS: getattr socket all
    DEPEND: LOOKSOCK
    CODE: GATT3s
    """
    _try_write_only(env, env.opts.usesocket)

def testWriteOnlyChar(t, env):
    """GETATTR with write-only attrs should return NFS4ERR_INVAL

    FLAGS: getattr char all
    DEPEND: LOOKCHAR
    CODE: GATT3c
    """
    _try_write_only(env, env.opts.usechar)

def testWriteOnlyBlock(t, env):
    """GETATTR with write-only attrs should return NFS4ERR_INVAL

    FLAGS: getattr block all
    DEPEND: LOOKBLK
    CODE: GATT3b
    """
    _try_write_only(env, env.opts.useblock)

def testUnknownAttrFile(t, env):
    """GETATTR should not fail on unknown attributes

    Comments: This test calls GETATTR with request for attribute
    number 1000.  Servers should not fail on unknown attributes.

    FLAGS: getattr file all
    DEPEND: LOOKFILE
    CODE: GATT4r
    """
    _try_unknown(t, env.c1, env.opts.usefile)

def testUnknownAttrDir(t, env):
    """GETATTR should not fail on unknown attributes

    Comments: This test calls GETATTR with request for attribute
    number 1000.  Servers should not fail on unknown attributes.

    FLAGS: getattr dir all
    DEPEND: LOOKDIR
    CODE: GATT4d
    """
    _try_unknown(t, env.c1, env.opts.usedir)

def testUnknownAttrFifo(t, env):
    """GETATTR should not fail on unknown attributes

    Comments: This test calls GETATTR with request for attribute
    number 1000.  Servers should not fail on unknown attributes.

    FLAGS: getattr fifo all
    DEPEND: LOOKFIFO
    CODE: GATT4f
    """
    _try_unknown(t, env.c1, env.opts.usefifo)

def testUnknownAttrLink(t, env):
    """GETATTR should not fail on unknown attributes

    Comments: This test calls GETATTR with request for attribute
    number 1000.  Servers should not fail on unknown attributes.

    FLAGS: getattr symlink all
    DEPEND: LOOKLINK
    CODE: GATT4a
    """
    _try_unknown(t, env.c1, env.opts.uselink)

def testUnknownAttrSocket(t, env):
    """GETATTR should not fail on unknown attributes

    Comments: This test calls GETATTR with request for attribute
    number 1000.  Servers should not fail on unknown attributes.

    FLAGS: getattr socket all
    DEPEND: LOOKSOCK
    CODE: GATT4s
    """
    _try_unknown(t, env.c1, env.opts.usesocket)

def testUnknownAttrChar(t, env):
    """GETATTR should not fail on unknown attributes

    Comments: This test calls GETATTR with request for attribute
    number 1000.  Servers should not fail on unknown attributes.

    FLAGS: getattr char all
    DEPEND: LOOKCHAR
    CODE: GATT4c
    """
    _try_unknown(t, env.c1, env.opts.usechar)

def testUnknownAttrBlock(t, env):
    """GETATTR should not fail on unknown attributes

    Comments: This test calls GETATTR with request for attribute
    number 1000.  Servers should not fail on unknown attributes.

    FLAGS: getattr block all
    DEPEND: LOOKBLK
    CODE: GATT4b
    """
    _try_unknown(t, env.c1, env.opts.useblock)

def testEmptyFile(t, env):
    """GETATTR should accept empty request

    FLAGS: getattr file all
    DEPEND: LOOKFILE
    CODE: GATT5r
    """
    _try_empty(t, env.c1, env.opts.usefile)

def testEmptyDir(t, env):
    """GETATTR should accept empty request

    FLAGS: getattr dir all
    DEPEND: LOOKDIR
    CODE: GATT5d
    """
    _try_empty(t, env.c1, env.opts.usedir)

def testEmptyFifo(t, env):
    """GETATTR should accept empty request

    FLAGS: getattr fifo all
    DEPEND: LOOKFIFO
    CODE: GATT5f
    """
    _try_empty(t, env.c1, env.opts.usefifo)

def testEmptyLink(t, env):
    """GETATTR should accept empty request

    FLAGS: getattr symlink all
    DEPEND: LOOKLINK
    CODE: GATT5a
    """
    _try_empty(t, env.c1, env.opts.uselink)

def testEmptySocket(t, env):
    """GETATTR should accept empty request

    FLAGS: getattr socket all
    DEPEND: LOOKSOCK
    CODE: GATT5s
    """
    _try_empty(t, env.c1, env.opts.usesocket)

def testEmptyChar(t, env):
    """GETATTR should accept empty request

    FLAGS: getattr char all
    DEPEND: LOOKCHAR
    CODE: GATT5c
    """
    _try_empty(t, env.c1, env.opts.usechar)

def testEmptyBlock(t, env):
    """GETATTR should accept empty request

    FLAGS: getattr block all
    DEPEND: LOOKBLK
    CODE: GATT5b
    """
    _try_empty(t, env.c1, env.opts.useblock)

def testSupportedFile(t, env):
    """GETATTR(FATTR4_SUPPORTED_ATTRS) should return all mandatory attrs

    FLAGS: getattr file all
    DEPEND: LOOKFILE
    CODE: GATT6r
    """
    _try_supported(t, env, env.opts.usefile)

def testSupportedDir(t, env):
    """GETATTR(FATTR4_SUPPORTED_ATTRS) should return all mandatory attrs

    FLAGS: getattr dir all
    DEPEND: LOOKDIR
    CODE: GATT6d
    """
    _try_supported(t, env, env.opts.usedir)

def testSupportedFifo(t, env):
    """GETATTR(FATTR4_SUPPORTED_ATTRS) should return all mandatory attrs

    FLAGS: getattr fifo all
    DEPEND: LOOKFIFO
    CODE: GATT6f
    """
    _try_supported(t, env, env.opts.usefifo)

def testSupportedLink(t, env):
    """GETATTR(FATTR4_SUPPORTED_ATTRS) should return all mandatory attrs

    FLAGS: getattr symlink all
    DEPEND: LOOKLINK
    CODE: GATT6a
    """
    _try_supported(t, env, env.opts.uselink)

def testSupportedSocket(t, env):
    """GETATTR(FATTR4_SUPPORTED_ATTRS) should return all mandatory attrs

    FLAGS: getattr socket all
    DEPEND: LOOKSOCK
    CODE: GATT6s
    """
    _try_supported(t, env, env.opts.usesocket)

def testSupportedChar(t, env):
    """GETATTR(FATTR4_SUPPORTED_ATTRS) should return all mandatory attrs

    FLAGS: getattr char all
    DEPEND: LOOKCHAR
    CODE: GATT6c
    """
    _try_supported(t, env, env.opts.usechar)

def testSupportedBlock(t, env):
    """GETATTR(FATTR4_SUPPORTED_ATTRS) should return all mandatory attrs

    FLAGS: getattr block all
    DEPEND: LOOKBLK
    CODE: GATT6b
    """
    _try_supported(t, env, env.opts.useblock)

def testLongFile(t, env):
    """GETATTR should not fail when ask for all legal attributes

    FLAGS: getattr file all
    DEPEND: LOOKFILE
    CODE: GATT7r
    """
    _try_long(env, env.opts.usefile)

def testLongDir(t, env):
    """GETATTR should not fail when ask for all legal attributes

    FLAGS: getattr dir all
    DEPEND: LOOKDIR
    CODE: GATT7d
    """
    _try_long(env, env.opts.usedir)

def testLongFifo(t, env):
    """GETATTR should not fail when ask for all legal attributes

    FLAGS: getattr fifo all
    DEPEND: LOOKFIFO
    CODE: GATT7f
    """
    _try_long(env, env.opts.usefifo)

def testLongLink(t, env):
    """GETATTR should not fail when ask for all legal attributes

    FLAGS: getattr symlink all
    DEPEND: LOOKLINK
    CODE: GATT7a
    """
    _try_long(env, env.opts.uselink)

def testLongSocket(t, env):
    """GETATTR should not fail when ask for all legal attributes

    FLAGS: getattr socket all
    DEPEND: LOOKSOCK
    CODE: GATT7s
    """
    _try_long(env, env.opts.usesocket)

def testLongChar(t, env):
    """GETATTR should not fail when ask for all legal attributes

    FLAGS: getattr char all
    DEPEND: LOOKCHAR
    CODE: GATT7c
    """
    _try_long(env, env.opts.usechar)

def testLongBlock(t, env):
    """GETATTR should not fail when ask for all legal attributes

    FLAGS: getattr block all
    DEPEND: LOOKBLK
    CODE: GATT7b
    """
    _try_long(env, env.opts.useblock)

def testFSLocations(t, env):
    """GETATTR on FSLocations

    FLAGS: getattr fslocations all
    DEPEND: LOOKFILE
    CODE: GATT8
    """
    c = env.c1
    ops = c.use_obj(env.opts.usefile)
    ops += [c.getattr([FATTR4_FS_LOCATIONS])]
    res = c.compound(ops)
    checklist(res, [NFS4_OK, NFS4ERR_ATTRNOTSUPP], "GETATTR(fs_locations)")
    if res.status == NFS4ERR_ATTRNOTSUPP:
        t.fail_support("fs_locations not a supported attribute")
    raw_attrs = res.resarray[-1].arm.arm.obj_attributes
    d = fattr2dict(raw_attrs)
    print d
    
####################################################

    def xxxtestMountedOnFileid(self):
        """GETATTR(FATTR4_MOUNTED_ON_FILEID)

	This DOES NOT work on standard test tree.  It assumes that pseudofs
	root / and pseudo fs node /unix exist, and that /unix is a mountpoint
	of an exported file system. The fsid of "/" should differ from the
	fsid of "/unix", and the mounted_on_fileid should != the filed with
	both the Getattr of "/unix" and the Readdir of "/".
        """

        request = [FATTR4_MOUNTED_ON_FILEID, FATTR4_FILEID, FATTR4_FSID]
        lookupops = [self.ncl.lookup_op("unix")]
        ops = [self.ncl.putrootfh_op()]
        ops.append(self.ncl.getattr(request))
	ops += lookupops
        ops.append(self.ncl.getattr(request))
        res = self.ncl.do_ops(ops)
        self.assert_OK(res)
        obj = res.resarray[-3].arm.arm.obj_attributes
        d = nfs4lib.fattr2dict(obj)
        print
        print "From Getattr / - ",d
        obj = res.resarray[-1].arm.arm.obj_attributes
        d = nfs4lib.fattr2dict(obj)
        print
        print "From Getattr /unix - ",d

        ops = [self.ncl.putrootfh_op()]
        attrmask = nfs4lib.list2attrmask(request)
        ops.append(self.ncl.readdir(attr_request=attrmask))
        res = self.ncl.do_ops(ops)
        self.assert_OK(res)

        # Find 'user' (note this assumes dir listing is small enough that
        # the whole listing was returned)
        reply = res.resarray[-1].arm.arm.reply
        entry = reply.entries[0]
        while 1:
            if entry.name=="unix":
                break
            if not entry.nextentry:
                self.fail("Could not find mountpoint /unix")
            entry = entry.nextentry[0]
        d2 = nfs4lib.fattr2dict(entry.attrs)
        print "From Readdir / - ",d2
        

