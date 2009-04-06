from nfs4.nfs4_const import *
from environment import check, get_invalid_utf8strings

def testDir(t, env):
    """REMOVE on existing, removable object

    FLAGS: remove dir all
    DEPEND: MKDIR
    CODE: RM1d
    """
    c = env.c1
    res = c.create_obj(t.code)
    check(res)
    ops = c.use_obj(c.homedir) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, msg="Removing dir %s" % t.code)

def testLink(t, env):
    """REMOVE on existing, removable object

    FLAGS: remove symlink all
    DEPEND: MKLINK
    CODE: RM1a
    """
    c = env.c1
    res = c.create_obj(t.code, NF4LNK)
    check(res)
    ops = c.use_obj(c.homedir) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, msg="Removing symlink %s" % t.code)

def testBlock(t, env):
    """REMOVE on existing, removable object

    FLAGS: remove block all
    DEPEND: MKBLK
    CODE: RM1b
    """
    c = env.c1
    res = c.create_obj(t.code, NF4BLK)
    check(res)
    ops = c.use_obj(c.homedir) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, msg="Removing block device %s" % t.code)

def testChar(t, env):
    """REMOVE on existing, removable object

    FLAGS: remove char all
    DEPEND: MKCHAR
    CODE: RM1c
    """
    c = env.c1
    res = c.create_obj(t.code, NF4CHR)
    check(res)
    ops = c.use_obj(c.homedir) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, msg="Removing character device %s" % t.code)

def testFifo(t, env):
    """REMOVE on existing, removable object

    FLAGS: remove fifo all
    DEPEND: MKFIFO
    CODE: RM1f
    """
    c = env.c1
    res = c.create_obj(t.code, NF4FIFO)
    check(res)
    ops = c.use_obj(c.homedir) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, msg="Removing fifo %s" % t.code)

def testSocket(t, env):
    """REMOVE on existing, removable object

    FLAGS: remove socket all
    DEPEND: MKSOCK
    CODE: RM1s
    """
    c = env.c1
    res = c.create_obj(t.code, NF4SOCK)
    check(res)
    ops = c.use_obj(c.homedir) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, msg="Removing socket %s" % t.code)

def testFile(t, env):
    """REMOVE on existing, removable object

    FLAGS: remove file all
    DEPEND: MKFILE
    CODE: RM1r
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    ops = c.use_obj(c.homedir) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, msg="Removing file %s" % t.code)
    
def testCfhFile(t, env):
    """REMOVE with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: remove file all
    DEPEND: LOOKFILE
    CODE: RM2r
    """
    c = env.c1
    ops = c.use_obj(env.opts.usefile) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "REMOVE with non-dir cfh")

def testCfhLink(t, env):
    """REMOVE with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: remove symlink all
    DEPEND: LOOKLINK
    CODE: RM2a
    """
    c = env.c1
    ops = c.use_obj(env.opts.uselink) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "REMOVE with non-dir cfh")

def testCfhBlock(t, env):
    """REMOVE with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: remove block all
    DEPEND: LOOKBLK
    CODE: RM2b
    """
    c = env.c1
    ops = c.use_obj(env.opts.useblock) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "REMOVE with non-dir cfh")

def testCfhChar(t, env):
    """REMOVE with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: remove char all
    DEPEND: LOOKCHAR
    CODE: RM2c
    """
    c = env.c1
    ops = c.use_obj(env.opts.usechar) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "REMOVE with non-dir cfh")

def testCfhFifo(t, env):
    """REMOVE with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: remove fifo all
    DEPEND: LOOKFIFO
    CODE: RM2f
    """
    c = env.c1
    ops = c.use_obj(env.opts.usefifo) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "REMOVE with non-dir cfh")

def testCfhSocket(t, env):
    """REMOVE with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: remove socket all
    DEPEND: LOOKSOCK
    CODE: RM2s
    """
    c = env.c1
    ops = c.use_obj(env.opts.usesocket) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "REMOVE with non-dir cfh")

def testNoFh(t, env):
    """REMOVE without (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: remove emptyfh all
    CODE: RM3
    """
    c = env.c1
    res = c.compound([c.remove_op(t.code)])
    check(res, NFS4ERR_NOFILEHANDLE, "REMOVE with no <cfh>")

def testZeroLengthTarget(t, env):
    """REMOVE with zero length target should return NFS4ERR_INVAL

    FLAGS: remove all
    CODE: RM4
    """
    c = env.c1
    ops = c.use_obj(c.homedir) + [c.remove_op('')]
    res = c.compound(ops)
    check(res, NFS4ERR_INVAL, "REMOVE with zero length target")

def testNonUTF8(t, env):
    """REMOVE with non-UTF8 components should return NFS4ERR_INVAL

    FLAGS: remove utf8 all
    DEPEND: MKDIR
    CODE: RM5
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    res = c.create_obj(basedir)
    check(res)
    for name in get_invalid_utf8strings():
        ops = c.use_obj(basedir) + [c.remove_op(name)]
        res = c.compound(ops)
        check(res, NFS4ERR_INVAL, "Trying to remove file with invalid utf8 "
                                  "name %s/%s" % (t.code, repr(name)[1:-1]))

def testNonExistent(t, env):
    """REMOVE on non-existing object should return NFS4ERR_NOENT

    FLAGS: remove all
    CODE: RM6
    """
    c = env.c1
    ops = c.use_obj(c.homedir) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOENT, "REMOVE non-existing object %s" % t.code)

def testDots(t, env):
    """REMOVE on . or .. should return NFS4ERR_NOENT or NFS4ERR_BADNAME

    FLAGS: remove dots all
    DEPEND: MKDIR
    CODE: RM7
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    res = c.create_obj(basedir)
    check(res)
    ops =  c.use_obj(basedir) + [c.remove_op('.')]
    res = c.compound(ops)
    check(res, NFS4ERR_BADNAME, "REMOVE nonexistant '.'", [NFS4ERR_NOENT])
    ops =  c.use_obj(basedir) + [c.remove_op('..')]
    res = c.compound(ops)
    check(res, NFS4ERR_BADNAME, "REMOVE nonexistant '..'", [NFS4ERR_NOENT])
    
def testNotEmpty(t, env):
    """REMOVE called on nonempty directory should return NFS4ERR_NOTEMPTY

    FLAGS: remove all
    DEPEND: MKDIR
    CODE: RM8
    """
    c = env.c1
    # Create non-empty dir
    basedir = c.homedir + [t.code]
    res = c.create_obj(basedir)
    check(res)
    res = c.create_obj(basedir + [t.code])
    check(res)
    # Now try to remove it
    ops =  c.use_obj(c.homedir) + [c.remove_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTEMPTY, "REMOVE called on nonempty directory")

#######################################

    def testValidNames(t, env):
        """REMOVE should succeed on all legal names

        Extra test

        Comments: This test tries REMOVE on all names returned from try_file_names()
        """
        # This test tests the lookup part of REMOVE
        self.init_connection()

        # Save files for REMOVE
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            self.skip(e)

        # Ok, lets try REMOVE on all accepted names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in accepted_names:
            ops = [self.ncl.putrootfh_op()] + lookup_dir_ops
            ops.append(self.ncl.remove_op(filename))
            res = self.ncl.do_ops(ops)
            self.assert_OK(res)

    def testInvalidNames(t, env):
        """REMOVE should fail with NFS4ERR_NOENT on all unexisting, invalid file names

        Extra test

        Comments: Tries REMOVE on rejected file names from
        try_file_names().  NFS4ERR_INVAL should NOT be returned in this case,
        although the server rejects creation of objects with these names

        (FRED) - see same test in st_lookup
        """
        self.init_connection()
        try:
            (accepted_names, rejected_names) = self.try_file_names()
        except SkipException, e:
            self.skip(e)

        # Ok, lets try REMOVE on all rejected names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in rejected_names:
            ops = [self.ncl.putrootfh_op()] + lookup_dir_ops
            ops.append(self.ncl.remove_op(filename))
            res = self.ncl.do_ops(ops)
            self.assert_status(res, [NFS4ERR_NOENT,NFS4ERR_INVAL])


