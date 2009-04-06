from nfs4.nfs4_const import *
from environment import check, makeBadID, makeStaleId

def _compare(t, res, expect, eof=True):
    def shorten(str):
        if len(str)<64:
            return repr(str)
        else:
            return "%s..." % repr(str[0:64])
    count = len(res.data)
    if res.data != expect[:count]:
        t.fail("READ returned '%s', expected '%s'" % (res.data, expect))
    if count < len(expect):
        if res.eof:
             t.fail("READ returned EOF after getting %s, expected %s" %
                   (shorten(res.data), shorten(expect)))
        else:
             t.pass_warn("READ returned %i characters, expected %i" %
                        (count, len(expect)))
    if res.eof != eof:
        if eof:
            t.fail("READ to file end returned without EOF set")
        else:
            t.fail("READ unexpectedly returned EOF")

##########################################

def testSimpleRead(t, env):
    """READ from regular file with stateid=zeros

    FLAGS: read all
    DEPEND: LOOKFILE
    CODE: RD1
    """
    c = env.c1
    res = c.read_file(env.opts.usefile)
    check(res, msg="Reading file /%s" % '/'.join(env.opts.usefile))
    _compare(t, res, env.filedata, True)

def testStateidOnes(t, env):
    """READ with offset=2, count=2, stateid=ones

    FLAGS: read all
    DEPEND: LOOKFILE
    CODE: RD2
    """
    c = env.c1
    res = c.read_file(env.opts.usefile, 2, 2, env.stateid1)
    check(res, msg="Reading file /%s" % '/'.join(env.opts.usefile))
    _compare(t, res, env.filedata[2:4], False)

def testWithOpen(t, env):
    """READ with offset=5, count=1000, stateid from OPEN

    FLAGS: read all
    DEPEND: LOOKFILE
    CODE: RD3
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.open_confirm(t.code, env.opts.usefile)
    res = c.read_file(fh, 5, 1000, stateid)
    check(res, msg="Reading file /%s" % '/'.join(env.opts.usefile))
    _compare(t, res, env.filedata[5:1005], True)
    
def testLargeCount(t, env):
    """READ a large dataset

    FLAGS: read all
    DEPEND: MKFILE
    CODE: RD4
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, attrs={FATTR4_SIZE: 10000000,
                                                  FATTR4_MODE: 0644})
    res = c.read_file(fh, 0, 9000000, stateid)
    check(res, msg="Reading file %s" % t.code)
    _compare(t, res, '\x00'*9000000, False)

def testLargeOffset(t, env):
    """READ with offset outside file

    FLAGS: read all
    DEPEND: LOOKFILE
    CODE: RD5
    """
    c = env.c1
    res = c.read_file(env.opts.usefile, 1000, 10)
    check(res, msg="Reading file /%s" % '/'.join(env.opts.usefile))
    _compare(t, res, '', True)

def testZeroCount(t, env):
    """READ with count=0

    FLAGS: read all
    DEPEND: LOOKFILE
    CODE: RD6
    """
    c = env.c1
    res = c.read_file(env.opts.usefile, 5, 0)
    check(res, msg="Reading file /%s" % '/'.join(env.opts.usefile))
    _compare(t, res, '', False)

def testDir(t, env):
    """READ with non-file objects

    FLAGS: read dir all 
    DEPEND: LOOKDIR
    CODE: RD7d
    """
    c = env.c1
    res = c.read_file(env.opts.usedir)
    check(res, NFS4ERR_ISDIR, "Read of a directory")
    
def testLink(t, env):
    """READ with non-file objects

    FLAGS: read symlink all 
    DEPEND: LOOKLINK
    CODE: RD7a
    """
    c = env.c1
    res = c.read_file(env.opts.uselink)
    check(res, NFS4ERR_INVAL, "Read of a non-file object")

def testBlock(t, env):
    """READ with non-file objects

    FLAGS: read block all 
    DEPEND: LOOKBLK
    CODE: RD7b
    """
    c = env.c1
    res = c.read_file(env.opts.useblock)
    check(res, NFS4ERR_INVAL, "Read of a non-file object")

def testChar(t, env):
    """READ with non-file objects

    FLAGS: read char all 
    DEPEND: LOOKCHAR
    CODE: RD7c
    """
    c = env.c1
    res = c.read_file(env.opts.usechar)
    check(res, NFS4ERR_INVAL, "Read of a non-file object")

def testFifo(t, env):
    """READ with non-file objects

    FLAGS: read fifo all 
    DEPEND: LOOKFIFO
    CODE: RD7f
    """
    c = env.c1
    res = c.read_file(env.opts.usefifo)
    check(res, NFS4ERR_INVAL, "Read of a non-file object")

def testSocket(t, env):
    """READ with non-file objects

    FLAGS: read socket all 
    DEPEND: LOOKSOCK
    CODE: RD7s
    """
    c = env.c1
    res = c.read_file(env.opts.usesocket)
    check(res, NFS4ERR_INVAL, "Read of a non-file object")

def testNoFh(t, env):
    """READ without (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: read emptyfh all 
    CODE: RD8
    """
    c = env.c1
    res = c.read_file(None)
    check(res, NFS4ERR_NOFILEHANDLE, "READ with no <cfh>")

def testBadStateid(t, env):
    """READ with bad stateid should return NFS4ERR_BAD_STATEID

    FLAGS: read badid all
    DEPEND: MKFILE
    CODE: RD9
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.read_file(fh, stateid=makeBadID(stateid))
    check(res, NFS4ERR_BAD_STATEID, "READ with bad stateid")

def testStaleStateid(t, env):
    """READ with stale stateid should return NFS4ERR_STALE_STATEID

    FLAGS: read staleid all
    DEPEND: MKFILE
    CODE: RD10
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.read_file(fh, stateid=makeStaleId(stateid))
    check(res, NFS4ERR_STALE_STATEID, "READ with stale stateid")

def testOldStateid(t, env):
    """READ with old stateid should return NFS4ERR_OLD_STATEID

    FLAGS: read oldid all
    DEPEND: MKFILE
    CODE: RD11
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res, msg="Creating file %s" % t.code)
    oldstateid = res.resarray[-2].arm.arm.stateid
    fh, stateid = c.confirm(t.code, res)
    res = c.read_file(fh, stateid=oldstateid)
    check(res, NFS4ERR_OLD_STATEID, "READ with old stateid")

def testOpenMode(t, env):
    """READ with file opened in WRITE mode should return NFS4_OK or NFS4ERR_OPENMODE

    FLAGS: read all
    DEPEND: MKFILE
    CODE: RD12
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_WRITE)
    res = c.read_file(fh, stateid=stateid)
    check(res, NFS4ERR_OPENMODE, "READ with file opened in WRITE mode",
          [NFS4_OK])
