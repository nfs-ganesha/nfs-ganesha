from nfs4.nfs4_const import *
from environment import check, checklist, get_invalid_utf8strings

def _basictest(t, c, path, error=NFS4_OK):
    """Link to path, and make sure FATTR4_NUMLINKS increases by one"""
    d = c.do_getattrdict(path, [FATTR4_NUMLINKS])
    if d:
        oldcount = d[FATTR4_NUMLINKS]
    res = c.link(path, c.homedir + [t.code])
    check(res, error,
          "Creating hard link %s to /%s" % (t.code, '/'.join(path)))
    if d and res.status==NFS4_OK:
        newcount = c.do_getattrdict(path, [FATTR4_NUMLINKS])[FATTR4_NUMLINKS]
        if newcount - 1 != oldcount:
            t.fail("Numlinks went from %i to %i" % (oldcount, newcount)) 
    
# Any code that uses hard links must depend on this test
def testSupported(t, env):
    """LINK test for support

    FLAGS: link all
    CODE: LINKS
    """
    c = env.c1
    d = c.do_getattrdict(c.homedir, [FATTR4_LINK_SUPPORT])
    if not d[FATTR4_LINK_SUPPORT]:
        t.fail_support("Hard links not supported")


def testFile(t, env):
    """LINK a regular file

    FLAGS: link file all
    DEPEND: LINKS LOOKFILE
    CODE: LINK1r
    """
    _basictest(t, env.c1, env.opts.usefile)
    
def testDir(t, env):
    """LINK a dir

    FLAGS: link dir all
    DEPEND: LINKS LOOKDIR
    CODE: LINK1d
    """
    _basictest(t, env.c1, env.opts.usedir, NFS4ERR_ISDIR)
    
def testFifo(t, env):
    """LINK a fifo

    FLAGS: link fifo all
    DEPEND: LINKS LOOKFIFO
    CODE: LINK1f
    """
    _basictest(t, env.c1, env.opts.usefifo)

def testLink(t, env):
    """LINK a symlink

    FLAGS: link symlink all
    DEPEND: LINKS LOOKLINK
    CODE: LINK1a
    """
    _basictest(t, env.c1, env.opts.uselink)

def testBlock(t, env):
    """LINK a block device

    FLAGS: link block all
    DEPEND: LINKS LOOKBLK
    CODE: LINK1b
    """
    _basictest(t, env.c1, env.opts.useblock)

def testChar(t, env):
    """LINK a character device

    FLAGS: link char all
    DEPEND: LINKS LOOKCHAR
    CODE: LINK1c
    """
    _basictest(t, env.c1, env.opts.usechar)

def testSocket(t, env):
    """LINK a socket

    FLAGS: link socket all
    DEPEND: LINKS LOOKSOCK
    CODE: LINK1s
    """
    _basictest(t, env.c1, env.opts.usesocket)

def testNoSfh(t, env):
    """LINK should fail with NFS4ERR_NOFILEHANDLE if no (sfh)

    FLAGS: link emptyfh all
    DEPEND: LINKS
    CODE: LINK2
    """
    c = env.c1
    ops = [c.putrootfh_op(), c.link_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOFILEHANDLE, "LINK with no <sfh>")

def testNoCfh(t, env):
    """LINK should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

    FLAGS: link emptyfh all
    DEPEND: LINKS
    CODE: LINK3
    """
    c = env.c1
    ops = [c.link_op(t.code)]
    res = c.compound(ops)
    check(res, NFS4ERR_NOFILEHANDLE, "LINK with no <cfh>")

def testCfhFile(t, env):
    """LINK should fail with NFS4ERR_NOTDIR if cfh is not dir

    FLAGS: link file all
    DEPEND: LINKS LOOKFILE
    CODE: LINK4r
    """
    res = env.c1.link(env.opts.usefile, env.opts.usefile + [t.code])
    check(res, NFS4ERR_NOTDIR, "LINK with <cfh> not a directory")

def testCfhFifo(t, env):
    """LINK should fail with NFS4ERR_NOTDIR if cfh is not dir

    FLAGS: link fifo all
    DEPEND: LINKS LOOKFILE LOOKFIFO
    CODE: LINK4f
    """
    res = env.c1.link(env.opts.usefile, env.opts.usefifo + [t.code])
    check(res, NFS4ERR_NOTDIR, "LINK with <cfh> not a directory")

def testCfhLink(t, env):
    """LINK should fail with NFS4ERR_NOTDIR if cfh is not dir

    FLAGS: link symlink all
    DEPEND: LINKS LOOKFILE LOOKLINK
    CODE: LINK4a
    """
    res = env.c1.link(env.opts.usefile, env.opts.uselink + [t.code])
    check(res, NFS4ERR_NOTDIR, "LINK with <cfh> not a directory")

def testCfhBlock(t, env):
    """LINK should fail with NFS4ERR_NOTDIR if cfh is not dir

    FLAGS: link block all
    DEPEND: LINKS LOOKFILE LOOKBLK
    CODE: LINK4b
    """
    res = env.c1.link(env.opts.usefile, env.opts.useblock + [t.code])
    check(res, NFS4ERR_NOTDIR, "LINK with <cfh> not a directory")

def testCfhChar(t, env):
    """LINK should fail with NFS4ERR_NOTDIR if cfh is not dir

    FLAGS: link char all
    DEPEND: LINKS LOOKFILE LOOKCHAR
    CODE: LINK4c
    """
    res = env.c1.link(env.opts.usefile, env.opts.usechar + [t.code])
    check(res, NFS4ERR_NOTDIR, "LINK with <cfh> not a directory")

def testCfhSocket(t, env):
    """LINK should fail with NFS4ERR_NOTDIR if cfh is not dir

    FLAGS: link socket all
    DEPEND: LINKS LOOKFILE LOOKSOCK
    CODE: LINK4s
    """
    res = env.c1.link(env.opts.usefile, env.opts.usesocket + [t.code])
    check(res, NFS4ERR_NOTDIR, "LINK with <cfh> not a directory")

def testExists(t, env):
    """LINK should fail with NFS4ERR_EXIST if file exists

    FLAGS: link all
    DEPEND: LINKS LOOKFILE
    CODE: LINK5
    """
    c = env.c1
    res = c.link(env.opts.usefile, c.homedir + [t.code])
    check(res, msg="Creating hardlink %s" % t.code)
    res = c.link(env.opts.usefile, c.homedir + [t.code])
    check(res, NFS4ERR_EXIST, "LINK target already exists")

def testZeroLenName(t, env):
    """LINK with zero length name should return NFS4ERR_INVAL

    FLAGS: link all
    DEPEND: LINKS LOOKFILE
    CODE: LINK6
    """
    c = env.c1
    res = c.link(env.opts.usefile, c.homedir + [''])
    check(res, NFS4ERR_INVAL, "LINK with zero length name")

def testLongName(t, env):
    """LINK should fail with NFS4ERR_NAMETOOLONG with long filename

    FLAGS: link longname all
    DEPEND: LINKS LOOKFILE
    CODE: LINK7
    """
    c = env.c1
    res = c.link(env.opts.usefile, c.homedir + [env.longname])
    check(res, NFS4ERR_NAMETOOLONG, "LINK with very long name")

def testInvalidUtf8(t, env):
    """LINK with bad UTF-8 name strings should return NFS4ERR_INVAL

    FLAGS: link utf8 all
    DEPEND: LINKS LOOKFILE MKDIR
    CODE: LINK8
    """
    c = env.c1
    res = c.create_obj(c.homedir + [t.code])
    check(res)
    for name in get_invalid_utf8strings():
        res = c.link(env.opts.usefile, c.homedir + [t.code, name])
        check(res, NFS4ERR_INVAL,
              "LINK with invalid utf8 name %s/%s" % (t.code, repr(name)[1:-1]))

def testDots(t, env):
    """LINK with . or .. should succeed or return NFSERR_BADNAME

    FLAGS: link dots all
    DEPEND: LINKS LOOKFILE MKDIR
    CODE: LINK9
    """
    c = env.c1
    dir = c.homedir + [t.code]
    res = c.create_obj(dir)
    check(res)
    res1 = c.link(env.opts.usefile, dir + ['.'])
    checklist(res1, [NFS4_OK, NFS4ERR_BADNAME],
                  "Trying to make a hardlink named '.'")
    res2 = c.link(env.opts.usefile, dir + ['..'])
    checklist(res2, [NFS4_OK, NFS4ERR_BADNAME],
                  "Trying to make a hardlink named '..'")
    if res1.status == NFS4_OK or res2.status == NFS4_OK:
        t.pass_warn("Allowed creation of hardlink named '.' or '..'")
    
###################################################

# FRED - make test to invoke _MLINK

    def testNamingPolicy(self):
        """LINK should obey OPEN file name creation policy

        Extra test
        """
        self.init_connection()

        try:
            (x, rejected_names_open) = self.try_file_names(creator=self.create_via_open)
            (x, rejected_names_link) = self.try_file_names(creator=self.create_via_link)
            self.failIf(rejected_names_open != rejected_names_link,
                        "LINK does not obey OPEN naming policy")
        except SkipException, e:
            self.skip(e)

