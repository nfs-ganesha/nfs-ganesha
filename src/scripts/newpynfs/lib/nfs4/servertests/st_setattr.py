from nfs4.nfs4_const import *
from environment import check, checklist, get_invalid_utf8strings
from nfs4.nfs4lib import attrmask2list
from nfs4.nfs4_type import nfstime4, settime4

def _set_mode(t, c, file, stateid=None, msg=" using stateid=0",
              warnlist=[]):
    mode = 0740
    dict = {FATTR4_MODE: mode}
    ops = c.use_obj(file) + [c.setattr(dict, stateid)]
    res = c.compound(ops)
    check(res, msg="Setting mode to 0%o%s" % (mode, msg), warnlist=warnlist)
    check_res(t, c, res, file, dict)

def _set_size(t, c, file, stateid=None, msg=" using stateid=0"):
    startsize = c.do_getattr(FATTR4_SIZE, file)
    newsize = startsize + 10
    dict = {FATTR4_SIZE: newsize}
    ops = c.use_obj(file) + [c.setattr(dict, stateid)]
    res = c.compound(ops)
    check(res, msg="Changing size from %i to %i%s" % (startsize, newsize, msg),
          warnlist=[NFS4ERR_BAD_STATEID])
    check_res(t, c, res, file, dict)
    dict = {FATTR4_SIZE: 0}
    ops = c.use_obj(file) + [c.setattr(dict, stateid)]
    res = c.compound(ops)
    check(res, msg="Changing size from %i to 0" % newsize)
    check_res(t, c, res, file, dict)

def _try_readonly(t, env, path):
    c = env.c1
    baseops = c.use_obj(path)
    supported = c.supportedAttrs(path)
    attrlist = [attr for attr in env.attr_info if attr.readonly]
    for attr in attrlist:
        ops = baseops + [c.setattr({attr.bitnum: attr.sample})]
        res = c.compound(ops)
        if supported & attr.mask:
            check(res, NFS4ERR_INVAL,
                  "SETATTR the supported read-only attribute %s" % attr.name)
        else:
            checklist(res, [NFS4ERR_INVAL, NFS4ERR_ATTRNOTSUPP],
                  "SETATTR the unsupported read-only attribute %s" % attr.name)

def _try_unsupported(t, env, path):
    c = env.c1
    baseops = c.use_obj(path)
    supported = c.supportedAttrs(path)
    attrlist = [ attr for attr in env.attr_info
                 if attr.writable and not supported & attr.mask ]
    for attr in attrlist:
        ops = baseops + [c.setattr({attr.bitnum: attr.sample})]
        res = c.compound(ops)
        check(res, NFS4ERR_ATTRNOTSUPP, 
              "SETATTR with unsupported attr %s" % attr.name)

def check_res(t, c, res, file, dict):
    modified = attrmask2list(res.resarray[-1].arm.attrsset)
    for attr in modified:
        if attr not in dict:
            t.fail("attrsset contained %s, which was not requested" %
                   get_bitnumattr_dict()[attr])
    newdict = c.do_getattrdict(file, dict.keys())
    if newdict != dict:
        t.fail("Set attrs %s not equal to got attrs %s" % (dict, newdict))

########################################

def testMode(t, env):
    """See if FATTR4_MODE is supported

    FLAGS: all
    CODE: MODE
    """
    if not FATTR4_MODE & env.c1.supportedAttrs():
        t.fail_support("Server does not support FATTR4_MODE")


def testFile(t, env):
    """SETATTR(FATTR4_MODE) on regular file

    FLAGS: setattr file all
    DEPEND: MODE MKFILE
    CODE: SATT1r
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    _set_mode(t, c, fh)

def testDir(t, env):
    """SETATTR(FATTR4_MODE) on directory

    FLAGS: setattr dir all
    DEPEND: MODE MKDIR
    CODE: SATT1d
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path)
    _set_mode(t, c, path)

def testLink(t, env):
    """SETATTR(FATTR4_MODE) on symlink

    FLAGS: setattr symlink all
    DEPEND: MODE MKLINK
    CODE: SATT1a
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4LNK)
    _set_mode(t, c, path)

def testBlock(t, env):
    """SETATTR(FATTR4_MODE) on block device

    FLAGS: setattr block all
    DEPEND: MODE MKBLK
    CODE: SATT1b
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4BLK)
    _set_mode(t, c, path)

def testChar(t, env):
    """SETATTR(FATTR4_MODE) on character device

    FLAGS: setattr char all
    DEPEND: MODE MKCHAR
    CODE: SATT1c
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4CHR)
    _set_mode(t, c, path)

def testFifo(t, env):
    """SETATTR(FATTR4_MODE) on fifo

    FLAGS: setattr fifo all
    DEPEND: MODE MKFIFO
    CODE: SATT1f
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4FIFO)
    _set_mode(t, c, path)

def testSocket(t, env):
    """SETATTR(FATTR4_MODE) on socket

    FLAGS: setattr socketall
    DEPEND: MODE MKSOCK
    CODE: SATT1s
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4SOCK)
    _set_mode(t, c, path)

def testUselessStateid1(t, env):
    """SETATTR(FATTR4_MODE) on file with stateid = ones

    FLAGS: setattr file all
    DEPEND: MODE MKFILE
    CODE: SATT2a
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    _set_mode(t, c, fh, env.stateid1, " using stateid=1")

def testUselessStateid2(t, env):
    """SETATTR(FATTR4_MODE) on file with openstateid

    FLAGS: setattr file all
    DEPEND: MODE MKFILE
    CODE: SATT2b
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    _set_mode(t, c, fh, stateid, " using openstateid")

def testUselessStateid3(t, env):
    """SETATTR(FATTR4_MODE) on file with different file's openstateid

    FLAGS: setattr file all
    DEPEND: MODE MKFILE MKDIR
    CODE: SATT2c
    """
    c = env.c1
    c.init_connection()
    c.maketree([t.code, 'file'])
    path = c.homedir + [t.code, t.code]
    fh, stateid = c.create_confirm(t.code, path)
    _set_mode(t, c, c.homedir + [t.code, 'file'], stateid,
              " using bad openstateid", [NFS4ERR_BAD_STATEID])

# FRED - redo first 2 tests with _DENY_WRITE
def testResizeFile0(t, env):
    """SETATTR(FATTR4_SIZE) on file with stateid = 0

    FLAGS: setattr file all
    DEPEND: MKFILE
    CODE: SATT3a
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, deny=OPEN4_SHARE_DENY_NONE)
    _set_size(t, c, fh)
    
def testResizeFile1(t, env):
    """SETATTR(FATTR4_SIZE) on file with stateid = 1

    FLAGS: setattr file all
    DEPEND: MKFILE
    CODE: SATT3b
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, deny=OPEN4_SHARE_DENY_NONE)
    _set_size(t, c, fh, env.stateid1, " using stateid=1")
    
def testResizeFile2(t, env):
    """SETATTR(FATTR4_SIZE) on file with openstateid

    FLAGS: setattr file all
    DEPEND: MKFILE
    CODE: SATT3c
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    _set_size(t, c, fh, stateid, " using openstateid")
    
def testResizeFile3(t, env):
    """SETATTR(FATTR4_SIZE) with wrong openstateid should return _BAD_STATEID

    FLAGS: setattr file all
    DEPEND: MKFILE MKDIR
    CODE: SATT3d
    """
    c = env.c1
    c.init_connection()
    c.maketree([t.code, 'file'])
    path = c.homedir + [t.code, t.code]
    fh, stateid = c.create_confirm(t.code, path)
    ops = c.use_obj(c.homedir + [t.code, 'file'])
    ops += [c.setattr({FATTR4_SIZE: 10}, stateid)]
    res = c.compound(ops)
    check(res, NFS4ERR_BAD_STATEID, "SETATTR(_SIZE) with wrong openstateid")

def testOpenModeResize(t, env):
    """SETATTR(_SIZE) on file with _ACCESS_READ should return NFS4ERR_OPENMODE

    FLAGS: setattr file all
    DEPEND: MKFILE
    CODE: SATT4
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ)
    ops = c.use_obj(fh) + [c.setattr({FATTR4_SIZE: 10}, stateid)]
    res = c.compound(ops)
    check(res, NFS4ERR_OPENMODE, "SETATTR(_SIZE) on file with _ACCESS_READ")

def testNoFh(t, env):
    """SETATTR with no (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: setattr emptyfh all
    CODE: SATT5
    """
    c = env.c1
    res = c.compound([c.setattr({FATTR4_SIZE:0})])
    check(res, NFS4ERR_NOFILEHANDLE, "SETATTR with no <cfh>")

def testReadonlyFile(t, env):
    """SETATTR on read-only attrs should return NFS4ERR_INVAL

    FLAGS: setattr file all
    DEPEND: MKFILE
    CODE: SATT6r
    """
    c = env.c1
    c.init_connection()
    c.create_confirm(t.code)
    _try_readonly(t, env, c.homedir + [t.code])

def testReadonlyDir(t, env):
    """SETATTR on read-only attrs should return NFS4ERR_INVAL

    FLAGS: setattr dir all
    DEPEND: MKDIR
    CODE: SATT6d
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path)
    check(res)
    _try_readonly(t, env, path)

def testReadonlyLink(t, env):
    """SETATTR on read-only attrs should return NFS4ERR_INVAL

    FLAGS: setattr symlink all
    DEPEND: MKLINK SATT6d
    CODE: SATT6a
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4LNK)
    check(res)
    _try_readonly(t, env, path)

def testReadonlyBlock(t, env):
    """SETATTR on read-only attrs should return NFS4ERR_INVAL

    FLAGS: setattr block all
    DEPEND: MKBLK SATT6d
    CODE: SATT6b
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4BLK)
    check(res)
    _try_readonly(t, env, path)

def testReadonlyChar(t, env):
    """SETATTR on read-only attrs should return NFS4ERR_INVAL

    FLAGS: setattr char all
    DEPEND: MKCHAR SATT6d
    CODE: SATT6c
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4CHR)
    check(res)
    _try_readonly(t, env, path)

def testReadonlyFifo(t, env):
    """SETATTR on read-only attrs should return NFS4ERR_INVAL

    FLAGS: setattr fifo all
    DEPEND: MKFIFO SATT6d
    CODE: SATT6f
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4FIFO)
    check(res)
    _try_readonly(t, env, path)

def testReadonlySocket(t, env):
    """SETATTR on read-only attrs should return NFS4ERR_INVAL

    FLAGS: setattr socket all
    DEPEND: MKSOCK SATT6d
    CODE: SATT6s
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4SOCK)
    check(res)
    _try_readonly(t, env, path)

def testInvalidAttr1(t, env):
    """SETATTR with invalid attribute data should return NFS4ERR_BADXDR

    This testcase try to set FATTR4_MODE but does not send any mode data.

    FLAGS: setattr all
    DEPEND: MODE MKDIR
    CODE: SATT7
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path)
    check(res)
    badop = c.setattr({FATTR4_MODE: 0644})
    badop.opsetattr.obj_attributes.attr_vals = ''
    res = c.compound(c.use_obj(path) + [badop])
    check(res, NFS4ERR_BADXDR, "SETATTR(FATTR4_MODE) with no data")

def testInvalidAttr2(t, env):
    """SETATTR with extraneous attribute data should return NFS4ERR_BADXDR

    This testcase try to set FATTR4_MODE with extraneous attribute data
    appended

    FLAGS: setattr all
    DEPEND: MODE MKDIR
    CODE: SATT8
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path)
    check(res)
    badop = c.setattr({FATTR4_MODE: 0644})
    badop.opsetattr.obj_attributes.attr_vals += 'Garbage data'
    res = c.compound(c.use_obj(path) + [badop])
    check(res, NFS4ERR_BADXDR,
          "SETATTR(FATTR4_MODE) with extraneous attribute data appended")

def testNonUTF8(t, env):
    """SETATTR(_MIMETYPE) with non-utf8 string should return NFS4ERR_INVAL

    The only attributes that use utf8 are MIMETYPE, OWNER, GROUP, and ACL.
    OWNER and GROUP are subject to too many restrictions to use.
    Similarly for ACL.

    FLAGS: setattr utf8 all
    DEPEND: MKFILE
    CODE: SATT9
    """
    c = env.c1
    c.create_confirm(t.code)
    supported = c.supportedAttrs()
    if not (supported & 2**FATTR4_MIMETYPE):
        t.fail_support("FATTR4_MIMETYPE not supported")
    baseops = c.use_obj(c.homedir + [t.code])
    for name in get_invalid_utf8strings():
        ops = baseops + [c.setattr({FATTR4_MIMETYPE: name})]
        res = c.compound(ops)
        check(res, NFS4ERR_INVAL,
              "SETATTR(_MIMETYPE) with non-utf8 string %s" % repr(name))

def testInvalidTime(t, env):
    """SETATTR(FATTR4_TIME_MODIFY_SET) with invalid nseconds

    nseconds larger than 999999999 are considered invalid.
    SETATTR(FATTR4_TIME_MODIFY_SET) should return NFS4ERR_INVAL on
    such values. 

    FLAGS: setattr all
    DEPEND: MKDIR
    CODE: SATT10
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path)
    check(res)
    supported = c.supportedAttrs()
    if not (supported & 2**FATTR4_TIME_MODIFY_SET):
        t.fail_support("FATTR4_TIME_MODIFY_SET not supported")
    time = nfstime4(seconds=500000000, nseconds=int(1E9))
    settime = settime4(set_it=SET_TO_CLIENT_TIME4, time=time)
    ops = c.use_obj(path) + [c.setattr({FATTR4_TIME_MODIFY_SET: settime})]
    res = c.compound(ops)
    check(res, NFS4ERR_INVAL,
          "SETATTR(FATTR4_TIME_MODIFY_SET) with nseconds=1E9")

def testUnsupportedFile(t, env):
    """SETATTR with unsupported attr should return NFS4ERR_ATTRNOTSUPP

    FLAGS: setattr file all
    DEPEND: MKFILE
    CODE: SATT11r
    """
    c = env.c1
    c.init_connection()
    c.create_confirm(t.code)
    _try_unsupported(t, env, c.homedir + [t.code])

def testUnsupportedDir(t, env):
    """SETATTR with unsupported attr should return NFS4ERR_ATTRNOTSUPP

    FLAGS: setattr dir all
    DEPEND: MKDIR
    CODE: SATT11d
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path)
    check(res)
    _try_unsupported(t, env, path)

def testUnsupportedLink(t, env):
    """SETATTR with unsupported attr should return NFS4ERR_ATTRNOTSUPP

    FLAGS: setattr symlink all
    DEPEND: MKLINK
    CODE: SATT11a
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4LNK)
    check(res)
    _try_unsupported(t, env, path)

def testUnsupportedBlock(t, env):
    """SETATTR with unsupported attr should return NFS4ERR_ATTRNOTSUPP

    FLAGS: setattr block all
    DEPEND: MKBLK
    CODE: SATT11b
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4BLK)
    check(res)
    _try_unsupported(t, env, path)

def testUnsupportedChar(t, env):
    """SETATTR with unsupported attr should return NFS4ERR_ATTRNOTSUPP

    FLAGS: setattr char all
    DEPEND: MKCHAR
    CODE: SATT11c
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4CHR)
    check(res)
    _try_unsupported(t, env, path)

def testUnsupportedFifo(t, env):
    """SETATTR with unsupported attr should return NFS4ERR_ATTRNOTSUPP

    FLAGS: setattr fifo all
    DEPEND: MKFIFO
    CODE: SATT11f
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4FIFO)
    check(res)
    _try_unsupported(t, env, path)

def testUnsupportedSocket(t, env):
    """SETATTR with unsupported attr should return NFS4ERR_ATTRNOTSUPP

    FLAGS: setattr socket all
    DEPEND: MKSOCK
    CODE: SATT11s
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4SOCK)
    check(res)
    _try_unsupported(t, env, path)

def testSizeDir(t, env):
    """SETATTR(_SIZE) of a directory should return NFS4ERR_ISDIR

    FLAGS: setattr dir all
    DEPEND: MKDIR
    CODE: SATT12d
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path)
    check(res)
    ops = c.use_obj(path) + [c.setattr({FATTR4_SIZE: 0})]
    res = c.compound(ops)
    check(res, NFS4ERR_ISDIR, "SETATTR(_SIZE) of a directory")
    
def testSizeLink(t, env):
    """SETATTR(FATTR4_SIZE) of a non-file object should return NFS4ERR_INVAL

    FLAGS: setattr symlink all
    DEPEND: MKLINK
    CODE: SATT12a
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4LNK)
    check(res)
    ops = c.use_obj(path) + [c.setattr({FATTR4_SIZE: 0})]
    res = c.compound(ops)
    check(res, NFS4ERR_INVAL, "SETATTR(FATTR4_SIZE) of a symlink")
    
def testSizeBlock(t, env):
    """SETATTR(FATTR4_SIZE) of a non-file object should return NFS4ERR_INVAL

    FLAGS: setattr block all
    DEPEND: MKBLK
    CODE: SATT12b
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4BLK)
    check(res)
    ops = c.use_obj(path) + [c.setattr({FATTR4_SIZE: 0})]
    res = c.compound(ops)
    check(res, NFS4ERR_INVAL, "SETATTR(FATTR4_SIZE) of a block device")
    
def testSizeChar(t, env):
    """SETATTR(FATTR4_SIZE) of a non-file object should return NFS4ERR_INVAL

    FLAGS: setattr char all
    DEPEND: MKCHAR
    CODE: SATT12c
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4CHR)
    check(res)
    ops = c.use_obj(path) + [c.setattr({FATTR4_SIZE: 0})]
    res = c.compound(ops)
    check(res, NFS4ERR_INVAL, "SETATTR(FATTR4_SIZE) of a character device")
    
def testSizeFifo(t, env):
    """SETATTR(FATTR4_SIZE) of a non-file object should return NFS4ERR_INVAL

    FLAGS: setattr fifo all
    DEPEND: MKFIFO
    CODE: SATT12f
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4FIFO)
    check(res)
    ops = c.use_obj(path) + [c.setattr({FATTR4_SIZE: 0})]
    res = c.compound(ops)
    check(res, NFS4ERR_INVAL, "SETATTR(FATTR4_SIZE) of a fifo")

def testSizeSocket(t, env):
    """SETATTR(FATTR4_SIZE) of a non-file object should return NFS4ERR_INVAL

    FLAGS: setattr socket all
    DEPEND: MKSOCK
    CODE: SATT12s
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4SOCK)
    check(res)
    ops = c.use_obj(path) + [c.setattr({FATTR4_SIZE: 0})]
    res = c.compound(ops)
    check(res, NFS4ERR_INVAL, "SETATTR(FATTR4_SIZE) of a socket")

def testInodeLocking(t, env):
    """SETATTR: This causes printk message due to inode locking bug

    log shows - nfsd: inode locked twice during operation.
    Sporadic system crashes can occur after running this test

    FLAGS: setattr all
    DEPEND: MODE MKDIR MKFILE
    CODE: SATT13
    """
    #t.fail("Test set to fail without running.  Currently causes "
    #       "inode corruption leading to sporadic system crashes.")
    c = env.c1
    c.init_connection()
    basedir = c.homedir + [t.code]
    res = c.create_obj(basedir)
    check(res)
    fh, stateid = c.create_confirm(t.code, basedir + ['file'])
    
    # In a single compound statement, setattr on dir and then
    # do a state operation on a file in dir (like write or remove)
    ops = c.use_obj(basedir) + [c.setattr({FATTR4_MODE:0754})]
    ops += [c.lookup_op('file'), c.write_op(stateid, 0, 0, 'blahblah')]
    res = c.compound(ops)
    check(res, msg="SETATTR on dir and state operation on file in dir")

def testChange(t, env):
    """SETATTR(MODE) should change changeattr

    FLAGS: setattr all
    DEPEND: MODE MKFILE
    CODE: SATT14
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    change = c.do_getattr(FATTR4_CHANGE, fh)
    ops = c.use_obj(fh) + [c.setattr({FATTR4_MODE: 0740})]
    res = c.compound(ops)
    check(res)
    change2 = c.do_getattr(FATTR4_CHANGE, fh)
    if change == change2:
        t.fail("change attribute not affected by SETATTR(mode)")
