from nfs4.nfs4_const import *
from environment import check, checklist, get_invalid_utf8strings

def testDir(t, env):
    """LOOKUP testtree dir

    FLAGS: lookup testtree dir all
    CODE: LOOKDIR
    """
    c = env.c1
    path = env.opts.usedir
    res = c.compound(c.use_obj(path))
    check(res, msg="LOOKUP of /%s"%'/'.join(path))

def testFile(t, env):
    """LOOKUP testtree file

    FLAGS: lookup testtree file all
    CODE: LOOKFILE
    """
    c = env.c1
    path = env.opts.usefile
    res = c.compound(c.use_obj(path))
    check(res, msg="LOOKUP of /%s"%'/'.join(path))

def testLink(t, env):
    """LOOKUP testtree symlink

    FLAGS: lookup testtree symlink all
    CODE: LOOKLINK
    """
    c = env.c1
    path = env.opts.uselink
    res = c.compound(c.use_obj(path))
    check(res, msg="LOOKUP of /%s"%'/'.join(path))

def testBlock(t, env):
    """LOOKUP testtree block device

    FLAGS: lookup testtree block all
    CODE: LOOKBLK
    """
    c = env.c1
    path = env.opts.useblock
    res = c.compound(c.use_obj(path))
    check(res, msg="LOOKUP of /%s"%'/'.join(path))

def testChar(t, env):
    """LOOKUP testtree character device

    FLAGS: lookup testtree char all
    CODE: LOOKCHAR
    """
    c = env.c1
    path = env.opts.usechar
    res = c.compound(c.use_obj(path))
    check(res, msg="LOOKUP of /%s"%'/'.join(path))

def testSocket(t, env):
    """LOOKUP testtree socket

    FLAGS: lookup testtree socket all
    CODE: LOOKSOCK
    """
    c = env.c1
    path = env.opts.usesocket
    res = c.compound(c.use_obj(path))
    check(res, msg="LOOKUP of /%s"%'/'.join(path))

def testFifo(t, env):
    """LOOKUP testtree fifo

    FLAGS: lookup testtree fifo all
    CODE: LOOKFIFO
    """
    c = env.c1
    path = env.opts.usefifo
    res = c.compound(c.use_obj(path))
    check(res, msg="LOOKUP of /%s"%'/'.join(path))

def testNoFh(t, env):
    """LOOKUP should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

    FLAGS: lookup emptyfh all
    CODE: LOOK1
    """
    c = env.c1
    ops = [c.lookup_op('foo')]
    res = c.compound(ops)
    check(res, NFS4ERR_NOFILEHANDLE, "LOOKUP with no <cfh>")

def testNonExistent(t, env):
    """LOOKUP with non-existent components should return NFS4ERR_NOENT

    FLAGS: lookup all
    CODE: LOOK2
    """
    c = env.c1
    ops = c.go_home()
    ops += [c.lookup_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOENT,
          "LOOKUP with no non-existant component '%s'" % t.code)

def testZeroLength(t, env):
    """LOOKUP with zero length name should return NFS4ERR_INVAL

    FLAGS: lookup all
    CODE: LOOK3
    """
    c = env.c1
    ops = [c.putrootfh_op(), c.lookup_op('')]
    res = c.compound(ops)
    check(res, NFS4ERR_INVAL, "LOOKUP with no zero-length component")

def testLongName(t, env):
    """LOOKUP should fail with NFS4ERR_NAMETOOLONG with long filenames

    FLAGS: lookup longname all
    CODE: LOOK4
    """
    c = env.c1
    ops = [c.putrootfh_op(), c.lookup_op(env.longname)]
    res = c.compound(ops)
    check(res, NFS4ERR_NAMETOOLONG, "LOOKUP with very long component")

def testFileNotDir(t, env):
    """LOOKUP with file for cfh should give NFS4ERR_NOTDIR

    FLAGS: lookup file all
    DEPEND: LOOKFILE
    CODE: LOOK5r
    """
    c = env.c1
    path = env.opts.usefile + ['foo']
    res = c.compound(c.use_obj(path))
    check(res, NFS4ERR_NOTDIR, "LOOKUP using file for cfh")

def testBlockNotDir(t, env):
    """LOOKUP with block device for cfh should give NFS4ERR_NOTDIR

    FLAGS: lookup block all
    DEPEND: LOOKBLK
    CODE: LOOK5b
    """
    c = env.c1
    path = env.opts.useblock + ['foo']
    res = c.compound(c.use_obj(path))
    check(res, NFS4ERR_NOTDIR, "LOOKUP using block device for cfh")

def testCharNotDir(t, env):
    """LOOKUP with character device for cfh should give NFS4ERR_NOTDIR

    FLAGS: lookup char all
    DEPEND: LOOKCHAR
    CODE: LOOK5c
    """
    c = env.c1
    path = env.opts.usechar + ['foo']
    res = c.compound(c.use_obj(path))
    check(res, NFS4ERR_NOTDIR, "LOOKUP using character device for cfh")

def testSocketNotDir(t, env):
    """LOOKUP with socket for cfh should give NFS4ERR_NOTDIR

    FLAGS: lookup socket all
    DEPEND: LOOKSOCK
    CODE: LOOK5s
    """
    c = env.c1
    path = env.opts.usesocket + ['foo']
    res = c.compound(c.use_obj(path))
    check(res, NFS4ERR_NOTDIR, "LOOKUP using socket for cfh")

def testSymlinkNotDir(t, env):
    """LOOKUP with symlink for cfh should give NFS4ERR_SYMLINK

    FLAGS: lookup symlink all
    DEPEND: LOOKLINK
    CODE: LOOK5a
    """
    c = env.c1
    path = env.opts.uselink + ['foo']
    res = c.compound(c.use_obj(path))
    check(res, NFS4ERR_SYMLINK, "LOOKUP using symlink for cfh")

def testFifoNotDir(t, env):
    """LOOKUP with fifo for cfh should give NFS4ERR_NOTDIR

    FLAGS: lookup fifo all
    DEPEND: LOOKFIFO
    CODE: LOOK5f
    """
    c = env.c1
    path = env.opts.usefifo + ['foo']
    res = c.compound(c.use_obj(path))
    check(res, NFS4ERR_NOTDIR, "LOOKUP using fifo for cfh")

def testNonAccessable(t, env):
    """LOOKUP with non-accessable components should return NFS4ERR_ACCESS

    FLAGS: lookup all
    DEPEND: MKDIR
    CODE: LOOK6
    """
    # Create dir/foo, and set mode of dir to 000
    c = env.c1
    dir = c.homedir + [t.code]
    res = c.create_obj(dir)
    check(res)
    res = c.create_obj(dir + ['foo'])
    check(res)
    res = c.compound(c.use_obj(dir) + [c.setattr({FATTR4_MODE:0})])
    check(res)
    # Now try to lookup foo
    res = c.compound(c.use_obj(dir))
    check(res)
    res = c.compound(c.use_obj(dir + ['foo']))
    check(res, NFS4ERR_ACCESS, "LOOKUP object in a dir with mode=000")

def testInvalidUtf8(t, env):
    """LOOKUP with bad UTF-8 name strings should return NFS4ERR_INVAL

    FLAGS: lookup all
    DEPEND:
    CODE: LOOK7
    """
    c = env.c1
    for name in get_invalid_utf8strings():
        res = c.compound(c.use_obj(c.homedir + [name]))
        check(res, NFS4ERR_INVAL,
              "LOOKUP object with invalid utf-8 name %s" % repr(name)[1:-1])

def testDots(t, env):
    """LOOKUP on  . and ..

    OK or _NOENT - WARN
    _BADNAME - PASS

    FLAGS: lookup dots all
    DEPEND: MKDIR
    CODE: LOOK8
    """
    # Create dir/foo
    c = env.c1
    dir = c.homedir + [t.code]
    res = c.create_obj(dir)
    check(res)
    res = c.create_obj(dir + ['foo'])
    check(res)
    # Run tests
    res1 = c.compound(c.use_obj(dir + ['.']))
    checklist(res1, [NFS4ERR_NOENT, NFS4ERR_BADNAME],
              "LOOKUP a nonexistant '.'")
    res2 = c.compound(c.use_obj(dir + ['..']))
    checklist(res2, [NFS4ERR_NOENT, NFS4ERR_BADNAME],
              "LOOKUP a nonexistant '..'")
    res1 = c.compound(c.use_obj(dir + ['.', 'foo']))
    checklist(res1, [NFS4ERR_NOENT, NFS4ERR_BADNAME],
              "LOOKUP a nonexistant '.'")
    res2 = c.compound(c.use_obj(dir + ['..', t.code]))
    checklist(res2, [NFS4ERR_NOENT, NFS4ERR_BADNAME],
              "LOOKUP a nonexistant '..'")

def testUnaccessibleDir(t, env):
    """READDIR with (cfh) in unaccessible directory 

    FLAGS: lookup all
    DEPEND: MKDIR MODE
    CODE: LOOK9
    """
    c = env.c1
    path = c.homedir + [t.code]
    c.maketree([t.code, ['hidden']])
    ops = c.use_obj(path) + [c.setattr({FATTR4_MODE:0})]
    res = c.compound(ops)
    check(res, msg="Setting mode=0 on directory %s" % t.code)
    res = c.compound(c.use_obj(path + ['hidden']))
    check(res, NFS4ERR_ACCESS, "LOOKUP off of dir with mode=0")
                     
####################################################


    def testValidNames(self):
        """LOOKUP should succeed on all legal names

        Extra test

        Comments: This test tries LOOKUP on all names returned from try_file_names()
        """
        self.init_connection()

        # Saved files for LOOKUP
        try:
            (accepted_names, rejected_names) = self.try_file_names(0)
        except SkipException, e:
            self.skip(e)

        # Ok, lets try LOOKUP on all accepted names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in accepted_names:
            ops = [self.ncl.putrootfh_op()] + lookup_dir_ops
            ops.append(self.ncl.lookup_op(filename))
            res = self.ncl.do_ops(ops)
            self.assert_OK(res)
            
    def testInvalidNames(self):
        """LOOKUP should fail with NFS4ERR_NOENT on all unexisting, invalid file names

        Extra test

        (FRED) - Below is Peter's comment, but I disagree, and have changed
        test accordingly.
        
        Comments: Tries LOOKUP on rejected file names from
        try_file_names().  NFS4ERR_INVAL should NOT be returned in this case,
        although the server rejects creation of objects with these names
        """
        self.init_connection()

        try:
            (accepted_names, rejected_names) = self.try_file_names()
        except SkipException, e:
            self.skip(e)

        # Ok, lets try LOOKUP on all rejected names
        lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        for filename in rejected_names:
            ops = [self.ncl.putrootfh_op()] + lookup_dir_ops
            ops.append(self.ncl.lookup_op(filename))
            res = self.ncl.do_ops(ops)
            self.assert_status(res, [NFS4ERR_INVAL,NFS4ERR_NOENT])
