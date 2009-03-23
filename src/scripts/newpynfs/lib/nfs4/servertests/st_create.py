from nfs4.nfs4_const import *
from nfs4.nfs4_type import createtype4, specdata4
from nfs4.nfs4lib import dict2fattr
from environment import check, checklist

def getDefaultAttr(c):
    attr = {}
    #attr[FATTR4_OWNER] = c.security.get_owner()
    #attr[FATTR4_OWNER_GROUP] = c.security.get_group()
    attr[FATTR4_MODE] = 0755
    return dict2fattr(attr)

def _test_create(t, env, type, name, **keywords):
    c = env.c1
    ops = c.go_home()
    objtype = createtype4(type, **keywords)
    ops += [c.create_op(objtype, t.code, getDefaultAttr(c))]
    res = c.compound(ops)
    if res.status == NFS4ERR_BADTYPE:
        t.fail_support("CREATE of a %s returns _BADTYPE" % name)
    elif res.status == NFS4ERR_PERM:
        t.fail_support("CREATE of a %s returns _PERM"% name)
    check(res, msg="CREATE in empty dir")

    # Try again, should get NFS4ERR_EXIST
    res = c.compound(ops)
    check(res, NFS4ERR_EXIST)

def _test_notdir(t, env, devpath):
    c = env.c1
    res = c.create_obj(devpath + [t.code])
    check(res, NFS4ERR_NOTDIR)

def testDir(t, env):
    """CREATE a directory

    FLAGS: create dir all
    CODE: MKDIR
    """
    _test_create(t, env, NF4DIR, 'directory')

#FRED - test with existant vs nonexistant file
#       (sec 14.2.25 says shouldn't matter)
def testLink(t, env):
    """CREATE a symbolic link

    FLAGS: create symlink all
    CODE: MKLINK
    """
    _test_create(t, env, NF4LNK, 'link', **{'linkdata':'/etc/X11'})
     
def testBlock(t, env):
    """CREATE a block device
    
    FLAGS: create block all
    CODE: MKBLK
    """
    devdata = specdata4(1, 2)
    _test_create(t, env, NF4BLK, 'block device', **{'devdata':devdata})

def testChar(t, env):
    """CREATE a character device

    FLAGS: create char all
    CODE: MKCHAR
    """
    devdata = specdata4(1, 2)
    _test_create(t, env, NF4CHR, 'char device', **{'devdata':devdata})

def testSocket(t, env):
    """CREATE a socket

    FLAGS: create socket all
    CODE: MKSOCK
    """
    _test_create(t, env, NF4SOCK, 'socket')

def testFIFO(t, env):
    """CREATE a FIFO

    FLAGS: create fifo all
    CODE: MKFIFO
    """
    _test_create(t, env, NF4FIFO, 'fifo')

def testDirOffLink(t, env):
    """CREATE a dir off a symbolic link

    FLAGS: create symlink all
    DEPEND: LOOKLINK
    CODE: CR2
    """
    _test_notdir(t, env, env.opts.uselink)
     
def testDirOffBlock(t, env):
    """CREATE dir off a block device
    
    FLAGS: create block all
    DEPEND: LOOKBLK
    CODE: CR3
    """
    _test_notdir(t, env, env.opts.useblock)

def testDirOffChar(t, env):
    """CREATE dir off a character device

    FLAGS: create char all
    DEPEND: LOOKCHAR
    CODE: CR4
    """
    _test_notdir(t, env, env.opts.usechar)

def testDirOffSocket(t, env):
    """CREATE dir off a socket

    FLAGS: create socket all
    DEPEND: LOOKSOCK
    CODE: CR5
    """
    _test_notdir(t, env, env.opts.usesocket)

def testDirOffFIFO(t, env):
    """CREATE dir off a FIFO

    FLAGS: create fifo all
    DEPEND: LOOKFIFO
    CODE: CR6
    """
    _test_notdir(t, env, env.opts.usefifo)

def testDirOffFile(t, env):
    """Create dir off a file

    FLAGS: create file all
    DEPEND: LOOKFILE
    CODE: CR7
    """
    _test_notdir(t, env, env.opts.usefile)
    
def testNoFh(t, env):
    """CREATE should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

    FLAGS: create emptyfh all
    DEPEND:
    CODE: CR8
    """
    c = env.c1
    objtype = createtype4(NF4DIR)
    ops = [c.create_op(objtype, t.code, getDefaultAttr(c))]
    res = c.compound(ops)
    check(res, NFS4ERR_NOFILEHANDLE, "CREATE with no <cfh>")

def testZeroLength(t, env):
    """CREATE with zero length name should return NFS4ERR_INVAL

    FLAGS: create all
    CODE: CR9
    """
    c = env.c1
    res = c.create_obj(c.homedir + [''])
    check(res, NFS4ERR_INVAL, "CREATE with zero-length name")

def testRegularFile(t, env):
    """CREATE should fail with NFS4ERR_BADTYPE for regular files

    (see p245 of RPC 3530)

    FLAGS: create all
    CODE: CR10
    """
    c = env.c1
    res = c.create_obj(c.homedir + [t.code], NF4REG)
    check(res, NFS4ERR_BADTYPE, "CREATE with a regular file")

def testInvalidAttrmask(t, env):
    """CREATE should fail with NFS4ERR_INVAL on invalid attrmask

    Comments: We are using a read-only attribute on CREATE, which
    should return NFS4ERR_INVAL.

    FLAGS: create all
    CODE: CR11
    """
    c = env.c1
    res = c.create_obj(c.homedir + [t.code], attrs={FATTR4_LINK_SUPPORT: TRUE})
    check(res, NFS4ERR_INVAL, "Using read-only attr in CREATE")

def testUnsupportedAttributes(t, env):
    """CREATE should fail with NFS4ERR_ATTRNOTSUPP on unsupported attrs

    FLAGS: create all
    DEPEND: LOOKDIR
    CODE: CR12
    """
    c = env.c1
    supported = c.supportedAttrs(env.opts.usedir)
    count = 0
    for attr in env.attr_info:
        if attr.writable and not supported & attr.mask:
            count += 1
            attrs = {attr.bitnum : attr.sample}
            res = c.create_obj(c.homedir + [t.code], attrs=attrs)
            check(res, NFS4ERR_ATTRNOTSUPP,
                      "Using unsupported attr %s in CREATE" % attr.name)
    if count==0:
        t.pass_warn("There were no unsupported writable attributes, "
                    "nothing tested")

def testDots(t, env):
    """CREATE with . or .. should succeed or return NFS4ERR_BADNAME

    FLAGS: create dots all
    CODE: CR13
    """
    c = env.c1
    res = c.create_obj(c.homedir + ['.'])
    checklist(res, [NFS4_OK, NFS4ERR_BADNAME],
                  "Trying to CREATE a dir named '.'")
    res2 = c.create_obj(c.homedir + ['..'])
    checklist(res2, [NFS4_OK, NFS4ERR_BADNAME],
                  "Trying to CREATE a dir named '..'")
    if res.status == NFS4_OK or res2.status == NFS4_OK:
        t.pass_warn("Allowed creation of dir named '.' or '..'")

def testSlash(t, env):
    """CREATE with "/" in filename should return OK or _BADNAME or _BADCHAR

    Make sure / in file names are not treated as directory
    separator. Servers supporting "/" in file names should return
    NFS4_OK. Others should return NFS4ERR_INVAL. NFS4ERR_EXIST
    should not be returned.

    FLAGS: create all
    DEPEND: MKDIR
    CODE: CR14
    """
    # Setup
    c = env.c1
    res = c.create_obj(c.homedir + [t.code])
    check(res)
    res = c.create_obj(c.homedir + [t.code, 'foo'])
    check(res)
    # Test
    res = c.create_obj(c.homedir + [t.code + '/foo'])
    if res.status == NFS4_OK:
        t.pass_warn("Allowed creation of dir named '%s/foo'" % t.code)
    checklist(res, [NFS4ERR_BADNAME, NFS4ERR_BADCHAR],
                  "Creation of dir named '%s/foo'" % t.code)

def testLongName(t, env):
    """CREATE should fail with NFS4ERR_NAMETOOLONG with long filenames

    FLAGS: create longname all
    CODE: CR15
    """
    c = env.c1
    res = c.create_obj(c.homedir + [env.longname])
    check(res, NFS4ERR_NAMETOOLONG, "CREATE with very long component")

 ##############################################

#FRED - need utf8 check
    # FIXME
    def testNamingPolicy(self):
        """CREATE should obey OPEN file name creation policy

        Extra test
        """
        self.init_connection()

        try:
            (x, rejected_names_open) = self.try_file_names(creator=self.create_via_open)
            
            (x, rejected_names_create) = self.try_file_names(creator=self.create_via_create)
            self.failIf(rejected_names_open != rejected_names_create,
                        "CREATE does not obey OPEN naming policy")
        except SkipException, e:
            self.skip(e)
