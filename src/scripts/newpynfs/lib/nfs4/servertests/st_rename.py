from nfs4.nfs4_const import *
from environment import check, get_invalid_utf8strings

def testValidDir(t, env):
    """RENAME : normal operation

    FLAGS: rename dir all
    DEPEND: MKDIR
    CODE: RNM1d
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir1', ['foo']], ['dir2']])
    res = c.rename_obj(basedir + ['dir1', 'foo'], basedir + ['dir2', 'bar'])
    check(res, msg="RENAME of directory %s/dir1/foo to %s/dir2/bar" %
          (t.code, t.code))

def testValidFile(t, env):
    """RENAME : normal operation

    FLAGS: rename file all
    DEPEND: MKDIR MKFILE
    CODE: RNM1r
    """
    c = env.c1
    c.init_connection()
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir1', 'foo'], ['dir2']])
    res = c.rename_obj(basedir + ['dir1', 'foo'], basedir + ['dir2', 'bar'])
    check(res, msg="RENAME of file %s/dir1/foo to %s/dir2/bar" %
          (t.code, t.code))

def testValidLink(t, env):
    """RENAME : normal operation

    FLAGS: rename symlink all
    DEPEND: MKDIR MKLINK
    CODE: RNM1a
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir1'], ['dir2']])
    res = c.create_obj(basedir + ['dir1', 'foo'], NF4LNK)
    check(res)
    res = c.rename_obj(basedir + ['dir1', 'foo'], basedir + ['dir2', 'bar'])
    check(res, msg="RENAME of symlink %s/dir1/foo to %s/dir2/bar" %
          (t.code, t.code))

def testValidBlock(t, env):
    """RENAME : normal operation

    FLAGS: rename block all
    DEPEND: MKDIR MKBLK
    CODE: RNM1b
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir1'], ['dir2']])
    res = c.create_obj(basedir + ['dir1', 'foo'], NF4BLK)
    check(res)
    res = c.rename_obj(basedir + ['dir1', 'foo'], basedir + ['dir2', 'bar'])
    check(res, msg="RENAME of block device %s/dir1/foo to %s/dir2/bar" %
          (t.code, t.code))

def testValidChar(t, env):
    """RENAME : normal operation

    FLAGS: rename char all
    DEPEND: MKDIR MKCHAR
    CODE: RNM1c
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir1'], ['dir2']])
    res = c.create_obj(basedir + ['dir1', 'foo'], NF4CHR)
    check(res)
    res = c.rename_obj(basedir + ['dir1', 'foo'], basedir + ['dir2', 'bar'])
    check(res, msg="RENAME of character device %s/dir1/foo to %s/dir2/bar" %
          (t.code, t.code))

def testValidFifo(t, env):
    """RENAME : normal operation

    FLAGS: rename fifo all
    DEPEND: MKDIR MKFIFO
    CODE: RNM1f
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir1'], ['dir2']])
    res = c.create_obj(basedir + ['dir1', 'foo'], NF4FIFO)
    check(res)
    res = c.rename_obj(basedir + ['dir1', 'foo'], basedir + ['dir2', 'bar'])
    check(res, msg="RENAME of fifo %s/dir1/foo to %s/dir2/bar" %
          (t.code, t.code))

def testValidSocket(t, env):
    """RENAME : normal operation

    FLAGS: rename socket all
    DEPEND: MKDIR MKSOCK
    CODE: RNM1s
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir1'], ['dir2']])
    res = c.create_obj(basedir + ['dir1', 'foo'], NF4SOCK)
    check(res)
    res = c.rename_obj(basedir + ['dir1', 'foo'], basedir + ['dir2', 'bar'])
    check(res, msg="RENAME of socket %s/dir1/foo to %s/dir2/bar" %
          (t.code, t.code))

def testSfhFile(t, env):
    """RENAME with non-dir (sfh) should return NFS4ERR_NOTDIR

    FLAGS: rename file all
    DEPEND: LOOKFILE
    CODE: RNM2r
    """
    c = env.c1
    res = c.rename_obj(env.opts.usefile + [t.code], c.homedir + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <sfh>")

def testSfhLink(t, env):
    """RENAME with non-dir (sfh) should return NFS4ERR_NOTDIR

    FLAGS: rename symlink all
    DEPEND: LOOKLINK
    CODE: RNM2a
    """
    c = env.c1
    res = c.rename_obj(env.opts.uselink + [t.code], c.homedir + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <sfh>")

def testSfhBlock(t, env):
    """RENAME with non-dir (sfh) should return NFS4ERR_NOTDIR

    FLAGS: rename block all
    DEPEND: LOOKBLK
    CODE: RNM2b
    """
    c = env.c1
    res = c.rename_obj(env.opts.useblock + [t.code], c.homedir + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <sfh>")

def testSfhChar(t, env):
    """RENAME with non-dir (sfh) should return NFS4ERR_NOTDIR

    FLAGS: rename char all
    DEPEND: LOOKCHAR
    CODE: RNM2c
    """
    c = env.c1
    res = c.rename_obj(env.opts.usechar + [t.code], c.homedir + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <sfh>")

def testSfhFifo(t, env):
    """RENAME with non-dir (sfh) should return NFS4ERR_NOTDIR

    FLAGS: rename fifo all
    DEPEND: LOOKFIFO
    CODE: RNM2f
    """
    c = env.c1
    res = c.rename_obj(env.opts.usefifo + [t.code], c.homedir + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <sfh>")

def testSfhSocket(t, env):
    """RENAME with non-dir (sfh) should return NFS4ERR_NOTDIR

    FLAGS: rename socket all
    DEPEND: LOOKSOCK
    CODE: RNM2s
    """
    c = env.c1
    res = c.rename_obj(env.opts.usesocket + [t.code], c.homedir + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <sfh>")

def testCfhFile(t, env):
    """RENAME with non-dir (cfh) should return NFS4ERR_NOTDIR

    FLAGS: rename file all
    DEPEND: MKDIR LOOKFILE
    CODE: RNM3r
    """
    c = env.c1
    res = c.create_obj(t.code)
    check(res)
    res = c.rename_obj(c.homedir + [t.code], env.opts.usefile + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <cfh>")

def testCfhLink(t, env):
    """RENAME with non-dir (cfh) should return NFS4ERR_NOTDIR

    FLAGS: rename symlink all
    DEPEND: MKDIR LOOKLINK
    CODE: RNM3a
    """
    c = env.c1
    res = c.create_obj(t.code)
    check(res)
    res = c.rename_obj(c.homedir + [t.code], env.opts.uselink + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <cfh>")

def testCfhBlock(t, env):
    """RENAME with non-dir (cfh) should return NFS4ERR_NOTDIR

    FLAGS: rename block all
    DEPEND: MKDIR LOOKBLK
    CODE: RNM3b
    """
    c = env.c1
    res = c.create_obj(t.code)
    check(res)
    res = c.rename_obj(c.homedir + [t.code], env.opts.useblock + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <cfh>")

def testCfhChar(t, env):
    """RENAME with non-dir (cfh) should return NFS4ERR_NOTDIR

    FLAGS: rename char all
    DEPEND: MKDIR LOOKCHAR
    CODE: RNM3c
    """
    c = env.c1
    res = c.create_obj(t.code)
    check(res)
    res = c.rename_obj(c.homedir + [t.code], env.opts.usechar + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <cfh>")

def testCfhFifo(t, env):
    """RENAME with non-dir (cfh) should return NFS4ERR_NOTDIR

    FLAGS: rename fifo all
    DEPEND: MKDIR LOOKFIFO
    CODE: RNM3f
    """
    c = env.c1
    res = c.create_obj(t.code)
    check(res)
    res = c.rename_obj(c.homedir + [t.code], env.opts.usefifo + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <cfh>")

def testCfhSocket(t, env):
    """RENAME with non-dir (cfh) should return NFS4ERR_NOTDIR

    FLAGS: rename socket all
    DEPEND: MKDIR LOOKSOCK
    CODE: RNM3s
    """
    c = env.c1
    res = c.create_obj(t.code)
    check(res)
    res = c.rename_obj(c.homedir + [t.code], env.opts.usesocket + [t.code])
    check(res, NFS4ERR_NOTDIR, "RENAME with non-dir <cfh>")

def testNoSfh(t, env):
    """RENAME without (sfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: rename emptyfh all
    CODE: RNM4
    """
    c = env.c1
    res = c.rename_obj([None, t.code], c.homedir + [t.code])
    check(res, NFS4ERR_NOFILEHANDLE, "RENAME with no <sfh>")

# FRED - can't test No Cfh, with a Sfh

def testNonExistent(t, env):
    """RENAME on non-existing object should return NFS4ERR_NOENT

    FLAGS: rename all
    DEPEND: MKDIR
    CODE: RNM5
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    res = c.create_obj(basedir)
    check(res)
    res = c.rename_obj(basedir + ['foo'], basedir + ['bar'])
    check(res, NFS4ERR_NOENT, "RENAME on non-existing object %s/foo" % t.code)

def testZeroLengthOldname(t, env):
    """RENAME with zero length oldname should return NFS4ERR_INVAL

    FLAGS: rename all
    DEPEND: MKDIR
    CODE: RNM6
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    res = c.create_obj(basedir)
    check(res)
    res = c.rename_obj(basedir + [''], basedir + ['bar'])
    check(res, NFS4ERR_INVAL, "RENAME with zero length oldname")

def testZeroLengthNewname(t, env):
    """RENAME with zero length newname should return NFS4ERR_INVAL

    FLAGS: rename all
    DEPEND: MKDIR
    CODE: RNM7
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    res = c.create_obj(basedir)
    check(res)
    res = c.rename_obj(basedir + ['foo'], basedir + [''])
    check(res, NFS4ERR_INVAL, "RENAME with zero length newname")

def testBadutf8Oldname(t, env):
    """RENAME with non-UTF8 oldname should return NFS4ERR_INVAL

    FLAGS: rename utf8 all
    DEPEND: MKDIR
    CODE: RNM8
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    res = c.create_obj(basedir)
    check(res)
    for name in get_invalid_utf8strings():
        res = c.rename_obj(basedir + [name], basedir + [t.code])
        check(res, NFS4ERR_INVAL, "RENAME with non-UTF8 oldname %s/%s" %
                                   (t.code, repr(name)[1:-1]))

def testBadutf8Newname(t, env):
    """RENAME with non-UTF8 newname should return NFS4ERR_INVAL

    FLAGS: rename utf8 all
    DEPEND: MKDIR
    CODE: RNM9
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, [t.code]])
    for name in get_invalid_utf8strings():
        res = c.rename_obj(basedir + [t.code], basedir + [name])
        check(res, NFS4ERR_INVAL, "RENAME with non-UTF8 newname %s/%s" %
                                   (t.code, repr(name)[1:-1]))

def testDotsOldname(t, env):
    """RENAME from nonexistant . or .. should return _NOENT/_BADNAME

    FLAGS: rename dots all
    DEPEND: MKDIR
    CODE: RNM10
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, [t.code]])
    res = c.rename_obj(basedir + ['.'], basedir + [t.code])
    check(res, NFS4ERR_BADNAME, "RENAME from nonexistant '.'",
          [NFS4ERR_NOENT])
    res = c.rename_obj(basedir + ['..'], basedir + [t.code])
    check(res, NFS4ERR_BADNAME, "RENAME from nonexistant '..'",
          [NFS4ERR_NOENT])
    
def testDotsNewname(t, env):
    """RENAME into . or .. should return _BADNAME or NFS4_OK

    FLAGS: rename dots all
    DEPEND: MKDIR
    CODE: RNM11
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, [t.code]])
    res = c.rename_obj(basedir + [t.code], basedir + ['.'])
    check(res, NFS4ERR_BADNAME, "RENAME from nonexistant '.'",
          [NFS4_OK])
    res = c.rename_obj(basedir + ['..'], basedir + [t.code])
    check(res, NFS4ERR_BADNAME, "RENAME from nonexistant '..'",
          [NFS4_OK])

def testDirToObj(t, env):
    """RENAME dir into existing nondir should return NFS4ERR_EXIST

    FLAGS: rename all
    DEPEND: MKDIR MKFILE
    CODE: RNM12
    """
    c = env.c1
    c.init_connection()
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir'], 'file'])
    res = c.rename_obj(basedir + ['dir'], basedir + ['file'])
    check(res, NFS4ERR_EXIST, "RENAME dir into existing file")

def testDirToDir(t, env):
    """RENAME dir into existing, empty dir should retrun NFS4_OK

    FLAGS: rename all
    DEPEND: MKDIR
    CODE: RNM13
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir1', ['foo']], ['dir2']])
    res = c.rename_obj(basedir + ['dir1'], basedir + ['dir2'])
    check(res, msg="RENAME dir1 into existing, empty dir2")

def testFileToDir(t, env):
    """RENAME file into existing dir should return NFS4ERR_EXIST

    FLAGS: rename all
    DEPEND: MKDIR MKFILE
    CODE: RNM14
    """
    c = env.c1
    c.init_connection()
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir'], 'file'])
    res = c.rename_obj(basedir + ['file'], basedir + ['dir'])
    check(res, NFS4ERR_EXIST, "RENAME file into existing dir")

def testFileToFile(t, env):
    """RENAME file into existing file should return NFS4_OK

    FLAGS: rename all
    DEPEND: MKDIR MKFILE
    CODE: RNM15
    """
    c = env.c1
    c.init_connection()
    basedir = c.homedir + [t.code]
    c.maketree([t.code, 'file1', 'file2'])
    res = c.rename_obj(basedir + ['file1'], basedir + ['file2'])
    check(res, msg="RENAME file1 into existing file2")

def testDirToFullDir(t, env):
    """RENAME dir into existing, nonempty dir should return NFS4ERR_EXIST

    FLAGS: rename all
    DEPEND: MKDIR
    CODE: RNM16
    """
    c = env.c1
    basedir = c.homedir + [t.code]
    c.maketree([t.code, ['dir1'], ['dir2', ['foo']]])
    res = c.rename_obj(basedir + ['dir1'], basedir + ['dir2'])
    check(res, NFS4ERR_EXIST, "RENAME dir1 into existing, nonempty dir2")

def testFileToFullDir(t, env):
    """RENAME file into existing, nonempty dir should return NFS4ERR_EXIST

    FLAGS: rename all
    DEPEND: MKDIR MKFILE
    CODE: RNM17
    """
    c = env.c1
    c.init_connection()
    basedir = c.homedir + [t.code]
    c.maketree([t.code, 'file', ['dir', ['foo']]])
    res = c.rename_obj(basedir + ['file'], basedir + ['dir'])
    check(res, NFS4ERR_EXIST, "RENAME file into existing, nonempty dir")

def testSelfRenameDir(t, env):
    """RENAME that does nothing

    FLAGS: rename all
    DEPEND: MKDIR
    CODE: RNM18
    """
    c = env.c1
    c.maketree([t.code])
    basedir = c.homedir + [t.code]
    res = c.rename_obj(basedir, basedir)
    check(res)
    scinfo = res.resarray[-1].arm.arm.source_cinfo
    tcinfo = res.resarray[-1].arm.arm.target_cinfo
    if scinfo.before != scinfo.after or tcinfo.before != tcinfo.after:
        t.fail("RENAME of dir %s into itself should do nothing, "
               "but cinfo was changed" % t.code)

def testSelfRenameFile(t, env):
    """RENAME that does nothing

    FLAGS: rename all
    DEPEND: MKFILE
    CODE: RNM19
    """
    c = env.c1
    c.init_connection()
    fh, stateid = c.create_confirm(t.code)
    basedir = c.homedir + [t.code]
    res = c.rename_obj(basedir, basedir)
    check(res)
    scinfo = res.resarray[-1].arm.arm.source_cinfo
    tcinfo = res.resarray[-1].arm.arm.target_cinfo
    if scinfo.before != scinfo.after or tcinfo.before != tcinfo.after:
        t.fail("RENAME of file %s into itself should do nothing, "
               "but cinfo was changed" % t.code)


def testLinkRename(t, env):
    """RENAME of file into its hard link should do nothing

    FLAGS: rename all
    DEPEND: LINKS MKFILE
    CODE: RNM20
    """
    c = env.c1
    c.init_connection()
    c.maketree([t.code, 'file'])
    basedir = c.homedir + [t.code]
    res = c.link(basedir + ['file'], basedir + ['link'])
    check(res)
    res = c.rename_obj(basedir + ['file'], basedir + ['link'])
    check(res, msg="RENAME of file into its hard link")
    scinfo = res.resarray[-1].arm.arm.source_cinfo
    tcinfo = res.resarray[-1].arm.arm.target_cinfo
    if scinfo.before != scinfo.after or tcinfo.before != tcinfo.after:
        t.fail("RENAME of file into its hard link should do nothing, "
               "but cinfo was changed")

###########################################

    def testNamingPolicy(t, env):
        """RENAME should obey OPEN file name creation policy

        Extra test
        """
        # This test tests the create part of RENAME. 
        self.init_connection()

        try:
            (x, rejected_names_open) = self.try_file_names(creator=self.create_via_open)
            (x, rejected_names_rename) = self.try_file_names(creator=self.create_via_rename)
            self.failIf(rejected_names_open != rejected_names_rename,
                        "RENAME does not obey OPEN naming policy")
        except SkipException, e:
            self.skip(e)

    def testValidNames(t, env):
        """RENAME should succeed on all legal names

        Extra test

        Comments: This test tries RENAME on all names returned from try_file_names()
        """
        # This test tests the lookup part of RENAME. 
        self.init_connection()

        # Saved files for 
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            self.skip(e)

        # Ok, lets try RENAME on all accepted names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in accepted_names:
            self._rename(oldname=filename)

    def testInvalidNames(t, env):
        """RENAME should fail with NFS4ERR_NOENT on all unexisting, invalid file names

        Extra test

        (FRED) - see comment on same test in st_lookup
        
        Comments: Tries RENAME on rejected file names from try_file_names().
        NFS4ERR_INVAL should NOT be returned in this case, although
        the server rejects creation of objects with these names
        """
        self.init_connection()

        try:
            (accepted_names, rejected_names) = self.try_file_names()
        except SkipException, e:
            self.skip(e)

        # Ok, lets try RENAME on all rejected names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in rejected_names:
            self._rename(oldname=filename, error=[NFS4ERR_NOENT,NFS4ERR_INVAL])

