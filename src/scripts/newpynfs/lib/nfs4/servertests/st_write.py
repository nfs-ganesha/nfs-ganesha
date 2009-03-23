from nfs4.nfs4_const import *
from environment import check, compareTimes, makeBadID, makeStaleId

_text = 'write data' # len=10

def _compare(t, res, expect, eof=True):
    check(res, msg="READ after WRITE")
    count = len(res.data)
    if res.data != expect[:count]:
        t.fail("READ returned %s, expected %s" %
               (repr(res.data), repr(expect)))
    if count < len(expect):
        if res.eof:
             t.fail("READ returned EOF after getting %s, expected %s" %
                   (repr(res.data), repr(expect)))
        else:
             t.pass_warn("READ returned %i characters, expected %i" %
                        (count, len(expect)))
    if res.eof != eof:
        if eof:
            t.fail("READ to file end returned without EOF set")
        else:
            t.fail("READ unexpectedly returned EOF")

#############################################

def testSimpleWrite(t, env):
    """WRITE with stateid=zeros and UNSTABLE4

    FLAGS: write all
    DEPEND: MKFILE
    CODE: WRT1
    """
    c = env.c1
    c.init_connection()
    attrs = {FATTR4_SIZE: 32, FATTR4_MODE: 0644}
    fh, stateid = c.create_confirm(t.code, attrs=attrs,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res = c.write_file(fh, _text, how=UNSTABLE4)
    check(res, msg="WRITE with stateid=zeros and UNSTABLE4")
    res = c.read_file(fh, 0, 20)
    _compare(t, res, _text + '\0'*(20-len(_text)), False)

def testSimpleWrite2(t, env):
    """WRITE with stateid=zeros changing size

    FLAGS: write all
    DEPEND: MKFILE
    CODE: WRT1b
    """
    c = env.c1
    c.init_connection()
    attrs = {FATTR4_SIZE: 32, FATTR4_MODE: 0644}
    fh, stateid = c.create_confirm(t.code, attrs=attrs,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res = c.write_file(fh, _text, 30)
    check(res, msg="WRITE with stateid=zeros changing size")
    res = c.read_file(fh, 25, 20)
    _compare(t, res, '\0'*5 + _text, True)

def testStateidOne(t, env):
    """WRITE with stateid=ones and DATA_SYNC4

    FLAGS: write all
    DEPEND: MKFILE
    CODE: WRT2
    """
    c = env.c1
    c.init_connection()
    attrs = {FATTR4_SIZE: 32, FATTR4_MODE: 0644}
    fh, stateid = c.create_confirm(t.code, attrs=attrs,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res = c.write_file(fh, _text, 5, env.stateid1, DATA_SYNC4)
    check(res, msg="WRITE with stateid=ones and DATA_SYNC4")
    if res.committed == UNSTABLE4:
        t.fail("WRITE asked for DATA_SYNC4, got UNSTABLE4")
    res = c.read_file(fh, 0, 20)
    _compare(t, res, '\0'*5 + _text + '\0'*(20-5-len(_text)), False)
    
def testWithOpen(t, env):
    """WRITE with openstateid and FILE_SYNC4

    FLAGS: write all
    DEPEND: MKFILE
    CODE: WRT3
    """
    c = env.c1
    c.init_connection()
    attrs = {FATTR4_SIZE: 32, FATTR4_MODE: 0644}
    fh, stateid = c.create_confirm(t.code, attrs=attrs)
    res = c.write_file(fh, _text, 50, stateid, FILE_SYNC4)
    check(res, msg="WRITE with openstateid and FILE_SYNC4")
    if res.committed != FILE_SYNC4:
        t.fail("WRITE asked for FILE_SYNC4, did not get it")
    res = c.read_file(fh, 0, 100)
    _compare(t, res, '\0'*50 + _text, True)
    
def testNoData(t, env):
    """WRITE with no data

    FLAGS: write all
    DEPEND: MKFILE
    CODE: WRT4
    """
    c = env.c1
    c.init_connection()
    attrs = {FATTR4_SIZE: 32, FATTR4_MODE: 0644}
    fh, stateid = c.create_confirm(t.code, attrs=attrs)
    time_prior = c.do_getattr(FATTR4_TIME_MODIFY, fh)
    env.sleep(1)
    res = c.write_file(fh, '', 5, stateid)
    check(res, msg="WRITE with no data")
    if res.count:
        t.fail("WRITE with no data returned count=%i" % res.count)
    # Now ensure time_modify was unchanged
    time_after = c.do_getattr(FATTR4_TIME_MODIFY, fh)
    if compareTimes(time_prior,time_after) != 0:
        t.fail("WRITE with no data affected time_modify")

def testLargeData(t, env):
    """WRITE with a large amount of data

    FLAGS: write read all
    DEPEND: MKFILE
    CODE: WRT5
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    data = "abcdefghijklmnopq" * 0x10000
    # Write the data
    pos = 0
    while pos < len(data):
        res = c.write_file(fh, data[pos:], pos, stateid)
        check(res, msg="WRITE with a large amount of data")
        pos += res.count
        if res.count == 0:
            t.fail("WRITE with a large amount of data returned count=0")
    # Read the data back in
    eof = False
    newdata = ''
    while not eof:
        res = c.read_file(fh, len(newdata), len(data) - len(newdata), stateid)
        check(res, msg="READ with large amount of data")
        newdata += res.data
        eof = res.eof
    if data != newdata:
        t.fail("READ did not correspond to WRITE with large dataset")

def testDir(t, env):
    """WRITE to a dir should return NFS4ERR_ISDIR

    FLAGS: write dir all
    DEPEND: MKDIR
    CODE: WRT6d
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path)
    check(res)
    res = c.write_file(path, _text)
    check(res, NFS4ERR_ISDIR, "WRITE to a dir")

def testLink(t, env):
    """WRITE to a non-file should return NFS4ERR_INVAL

    FLAGS: write symlink all
    DEPEND: MKLINK
    CODE: WRT6a
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4LNK)
    check(res)
    res = c.write_file(path, _text)
    check(res, NFS4ERR_INVAL, "WRITE to a symlink")

def testBlock(t, env):
    """WRITE to a non-file should return NFS4ERR_INVAL

    FLAGS: write block all
    DEPEND: MKBLK
    CODE: WRT6b
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4BLK)
    check(res)
    res = c.write_file(path, _text)
    check(res, NFS4ERR_INVAL, "WRITE to a block device")

def testChar(t, env):
    """WRITE to a non-file should return NFS4ERR_INVAL

    FLAGS: write char all
    DEPEND: MKCHAR
    CODE: WRT6c
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4CHR)
    check(res)
    res = c.write_file(path, _text)
    check(res, NFS4ERR_INVAL, "WRITE to a character device")

def testFifo(t, env):
    """WRITE to a non-file should return NFS4ERR_INVAL

    FLAGS: write fifo all
    DEPEND: MKFIFO
    CODE: WRT6f
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4FIFO)
    check(res)
    res = c.write_file(path, _text)
    check(res, NFS4ERR_INVAL, "WRITE to a fifo")

def testSocket(t, env):
    """WRITE to a non-file should return NFS4ERR_INVAL

    FLAGS: write socket all
    DEPEND: MKSOCK
    CODE: WRT6s
    """
    c = env.c1
    path = c.homedir + [t.code]
    res = c.create_obj(path, NF4SOCK)
    check(res)
    res = c.write_file(path, _text)
    check(res, NFS4ERR_INVAL, "WRITE to a socket")

def testNoFh(t, env):
    """WRITE with no (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: write emptyfh all
    CODE: WRT7
    """
    c = env.c1
    res = c.write_file(None, _text)
    check(res, NFS4ERR_NOFILEHANDLE, "WRITE with no <cfh>")

def testOpenMode(t, env):
    """WRITE with file opened in READ mode should return NFS4ERR_OPENMODE

    FLAGS: write all
    DEPEND: MKFILE
    CODE: WRT8
    """
    c = env.c1
    c.init_connection()
    attrs = {FATTR4_SIZE: 32, FATTR4_MODE: 0644}
    fh, stateid = c.create_confirm(t.code, attrs=attrs,
                                   access=OPEN4_SHARE_ACCESS_READ)
    res = c.write_file(fh, _text, 0, stateid)
    check(res, NFS4ERR_OPENMODE, "WRITE with file opened in READ mode")
    
def testShareDeny(t, env):
    """WRITE to file with DENY set should return NFS4ERR_LOCKED

    See 8.1.4, top of third paragraph

    FLAGS: write all
    DEPEND: MKFILE
    CODE: WRT9
    """
    c = env.c1
    c.init_connection()
    attrs = {FATTR4_SIZE: 32, FATTR4_MODE: 0644}
    fh, stateid = c.create_confirm(t.code, attrs=attrs,
                                   deny=OPEN4_SHARE_DENY_WRITE)
    res = c.write_file(fh, _text)
    check(res, NFS4ERR_LOCKED, "WRITE to file with DENY set")
    
def testBadStateid(t, env):
    """WRITE with bad stateid should return NFS4ERR_BAD_STATEID

    FLAGS: write badid all
    DEPEND: MKFILE
    CODE: WRT10
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.write_file(fh, _text, 0, makeBadID(stateid))
    check(res, NFS4ERR_BAD_STATEID, "WRITE with bad stateid")
    
def testStaleStateid(t, env):
    """WRITE with stale stateid should return NFS4ERR_STALE_STATEID

    FLAGS: write staleid all
    DEPEND: MKFILE
    CODE: WRT11
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    res = c.write_file(fh, _text, 0, makeStaleId(stateid))
    check(res, NFS4ERR_STALE_STATEID, "WRITE with stale stateid")

def testOldStateid(t, env):
    """WRITE with old stateid should return NFS4ERR_OLD_STATEID

    FLAGS: write oldid all
    DEPEND: MKFILE
    CODE: WRT12
    """
    c = env.c1
    c.init_connection()
    res = c.create_file(t.code)
    check(res, msg="Creating file %s" % t.code)
    oldstateid = res.resarray[-2].arm.arm.stateid
    fh, stateid = c.confirm(t.code, res)
    res = c.write_file(fh, _text, 0, oldstateid)
    check(res, NFS4ERR_OLD_STATEID, "WRITE with old stateid")
