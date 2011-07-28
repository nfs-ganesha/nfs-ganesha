from nfs4.nfs4_const import *
from nfs4.nfs4lib import get_attr_name
from environment import check

def _compare(t, entries, expect, attrlist=[]):
    names = [e.name for e in entries]
    names.sort()
    expect.sort()
    if names != expect:
        t.fail("Expected READDIR to return %s, instead got %s" %
               (expect, names))
    for e in entries:
        for attr in attrlist:
            if attr not in e.attrdict:
                t.fail("Requested attribute %s not returned for %s" %
                       (get_attr_name(attr), repr(e.name)))
        for attr in e.attrdict:
            if attr not in attrlist:
                t.fail("Unrequested attribute %s returned for %s" %
                       (get_attr_name(attr), repr(e.name)))

def _try_notdir(c, path):
    ops = c.use_obj(path)
    ops += [c.readdir()]
    res = c.compound(ops)
    check(res, NFS4ERR_NOTDIR, "READDIR with non-dir <cfh>")
    
###################################################

def testEmptyDir(t, env):
    """
    FLAGS: readdir dots all
    DEPEND: MKDIR
    CODE: RDDR1
    """
    c = env.c1
    c.maketree([t.code])
    entries = c.do_readdir(c.homedir + [t.code])
    _compare(t, entries, [])

def testFirst(t, env):
    """READDIR with cookie=0, maxcount=4096

    FLAGS: readdir all
    DEPEND: MKDIR MKFILE
    CODE: RDDR2
    """
    c = env.c1
    c.init_connection()
    c.maketree([t.code, 'file', ['dir']])
    entries = c.do_readdir(c.homedir + [t.code], maxcount=4096)
    _compare(t, entries, ['file', 'dir'])

def testAttr(t, env):
    """READDIR with attributes

    FLAGS: readdir all
    DEPEND: MKDIR MKFILE
    CODE: RDDR3
    """
    c = env.c1
    c.init_connection()
    c.maketree([t.code, 'file', ['dir']])
    attrlist = [FATTR4_SIZE, FATTR4_FILEHANDLE]
    entries = c.do_readdir(c.homedir + [t.code],
                           attr_request=attrlist)
    _compare(t, entries, ['file', 'dir'], attrlist)

def testSubsequent(t, env):
    """READDIR with cookie from previous call

    FLAGS: readdir all
    DEPEND: MKDIR
    CODE: RDDR4
    """
    c = env.c1
    c.init_connection()
    expected = ["dir%02i"%i for i in range(100)]
    c.maketree([t.code] + [[name] for name in expected])
    split = False
    count = 400
    while not split:
        entries = c.do_readdir(c.homedir + [t.code], maxcount=count)
        count -= 50
        _compare(t, entries, expected)
        # Note: _compare returning guarantees that len(entries)==100
        if entries[0].count != entries[-1].count:
            split = True

def testFhFile(t, env):
    """READDIR with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: readdir file all
    DEPEND: LOOKFILE
    CODE: RDDR5r
    """
    _try_notdir(env.c1, env.opts.usefile)

def testFhLink(t, env):
    """READDIR with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: readdir symlink all
    DEPEND: LOOKLINK
    CODE: RDDR5a
    """
    _try_notdir(env.c1, env.opts.uselink)

def testFhBlock(t, env):
    """READDIR with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: readdir block all
    DEPEND: LOOKBLK
    CODE: RDDR5b
    """
    _try_notdir(env.c1, env.opts.useblock)

def testFhChar(t, env):
    """READDIR with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: readdir char all
    DEPEND: LOOKCHAR
    CODE: RDDR5c
    """
    _try_notdir(env.c1, env.opts.usechar)

def testFhFifo(t, env):
    """READDIR with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: readdir fifo all
    DEPEND: LOOKFIFO
    CODE: RDDR5f
    """
    _try_notdir(env.c1, env.opts.usefifo)

def testFhSocket(t, env):
    """READDIR with non-dir (cfh) should give NFS4ERR_NOTDIR

    FLAGS: readdir socket all
    DEPEND: LOOKSOCK
    CODE: RDDR5s
    """
    _try_notdir(env.c1, env.opts.usesocket)

def testNoFh(t, env):
    """READDIR with no (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: readdir emptyfh all
    CODE: RDDR6
    """
    c = env.c1
    res = c.compound([c.readdir()])
    check(res, NFS4ERR_NOFILEHANDLE, "READDIR with no <cfh>")

# FRED - need bad cookieverf test

def testMaxcountZero(t, env):
    """READDIR with maxcount=0 should return NFS4ERR_TOOSMALL

    FLAGS: readdir all
    CODE: RDDR7
    """
    c = env.c1
    ops = c.use_obj(c.homedir) + [c.readdir(maxcount=0)]
    res = c.compound(ops)
    check(res, NFS4ERR_TOOSMALL, "READDIR with maxcount=0")

def testMaxcountSmall(t, env):
    """READDIR with maxcount=15 should return NFS4ERR_TOOSMALL

    Note the XDR overhead for returning an empty directory is 16 bytes

    FLAGS: readdir all
    DEPEND: MKDIR
    CODE: RDDR8
    """
    c = env.c1
    c.maketree([t.code])
    path = c.homedir + [t.code]
    ops = c.use_obj(path) + [c.readdir(maxcount=15)]
    res = c.compound(ops)
    check(res, NFS4ERR_TOOSMALL, "READDIR of empty dir with maxcount=15")
    ops = c.use_obj(path) + [c.readdir(maxcount=16)]
    res = c.compound(ops)
    check(res, msg="READDIR of empty dir with maxcount=16")

def testWriteOnlyAttributes(t, env):
    """READDIR with write-only attrs should return NFS4ERR_INVAL

    FLAGS: readdir all
    DEPEND: MKDIR
    CODE: RDDR9
    """
    c = env.c1
    c.maketree([t.code, ['dir']])
    baseops = c.use_obj(c.homedir + [t.code])
    for attr in [attr for attr in env.attr_info if attr.writeonly]:
        ops = baseops + [c.readdir(attr_request=[attr.bitnum])]
        res = c.compound(ops)
        check(res, NFS4ERR_INVAL, "READDIR with write-only attrs")

def testReservedCookies(t, env):
    """READDIR with a reserved cookie should return NFS4ERR_BAD_COOKIE

    FLAGS: readdir all
    CODE: RDDR10
    """
    c = env.c1
    baseops = c.use_obj(c.homedir)
    for cookie in [1,2]:
        ops = baseops + [c.readdir(cookie=cookie)]
        res = c.compound(ops)
        check(res, NFS4ERR_BAD_COOKIE,
              "READDIR with reserved cookie=%i" % cookie)

def testUnaccessibleDir(t, env):
    """READDIR with (cfh) in unaccessible directory 

    FLAGS: readdir all
    DEPEND: MKDIR MODE
    CODE: RDDR11
    """
    c = env.c1
    path = c.homedir + [t.code]
    c.maketree([t.code, ['hidden']])
    ops = c.use_obj(path) + [c.setattr({FATTR4_MODE:0})]
    res = c.compound(ops)
    check(res, msg="Setting mode=0 on directory %s" % t.code)
    ops = c.use_obj(path) + [c.readdir()]
    res = c.compound(ops)
    check(res, NFS4ERR_ACCESS, "READDIR of directory with mode=0")
   
def testUnaccessibleDirAttrs(t, env):
    """READDIR with (cfh) in unaccessible directory requesting attrs

    FLAGS: readdir all
    DEPEND: MKDIR MODE
    CODE: RDDR12
    """
    c = env.c1
    path = c.homedir + [t.code]
    c.maketree([t.code, ['hidden']])
    ops = c.use_obj(path) + [c.setattr({FATTR4_MODE:0})]
    res = c.compound(ops)
    check(res, msg="Setting mode=0 on directory %s" % t.code)
    ops = c.use_obj(path) + \
          [c.readdir(attr_request=[FATTR4_RDATTR_ERROR, FATTR4_TYPE])]
    res = c.compound(ops)
    check(res, NFS4ERR_ACCESS, "READDIR of directory with mode=0")
   
###########################################


    def testStrangeNames(t, env):
        """READDIR should obey OPEN naming policy

        Extra test

        Comments: Verifying that readdir obeys the same naming policy
        as OPEN.
        """
        self.init_connection()
        
        try:
            (accepted_names, rejected_names) = self.try_file_names(remove_files=0)
        except SkipException, e:
            self.skip(e)

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

    def testBug(t, env):
        """READDIR had a strange bug - is it there?

        Extra test

        Comments: Verifying that readdir obeys the same naming policy
        as OPEN.
        """
        self.init_connection()
        
        try:
            (accepted_names, rejected_names) = self.small_try_file_names(remove_files=0)
        except SkipException, e:
            self.skip(e)

        try: self.clean_dir(self.tmp_dir)
        except SkipException, e:
            self.fail(e)

    def small_try_file_names(self, remove_files=1, creator=None):
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
        for i in range(1, 256): #[1,2,3,255]
            if i in [47]: continue # 122
            ustr = "aa" + unichr(i) + "bb"
            try_names.append(ustr.encode("utf8"))

        # C:
        try_names.append("C:".encode("utf8"))

        #try_names.sort()

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

