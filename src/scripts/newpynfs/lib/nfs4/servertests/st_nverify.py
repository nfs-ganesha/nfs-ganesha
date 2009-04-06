from nfs4.nfs4_const import *
from environment import check, checklist, get_invalid_clientid, makeStaleId

def _try_mand(env, path):
    c = env.c1
    mand_bits = [attr.bitnum for attr in env.attr_info \
                 if attr.mandatory and attr.name != 'rdattr_error']
    dict = c.do_getattrdict(path, mand_bits)
    ops = c.use_obj(path) + [c.nverify(dict)] + c.use_obj(path)
    res = c.compound(ops)
    check(res, NFS4ERR_SAME, "NVerifying mandatory attributes against getattr")

def _try_type(env, path, type):
    c = env.c1
    ops = c.use_obj(path) + [c.nverify({FATTR4_TYPE:type})] + c.use_obj(path)
    res = c.compound(ops)
    check(res, NFS4ERR_SAME, "NVerifying type of /%s" % '/'.join(path))

def _try_changed_size(env, path):
    c = env.c1
    dict = c.do_getattrdict(path, [FATTR4_SIZE])
    dict[FATTR4_SIZE] += 1
    ops = c.use_obj(path) + [c.nverify(dict)] + c.use_obj(path)
    res = c.compound(ops)
    check(res, msg="NVerifying incorrect size")

def _try_write_only(env, path):
    c = env.c1
    baseops = c.use_obj(path)
    wo = [attr for attr in env.attr_info \
          if attr.writeonly or attr.name=='rdattr_error']
    for attr in wo:
        ops = baseops + [c.nverify({attr.bitnum: attr.sample})]
        res = c.compound(ops)
        check(res, NFS4ERR_INVAL, "NVERIFY with attr %s" % attr.name)

def _try_unsupported(env, path):
    c = env.c1
    baseops = c.use_obj(path)
    supp_mask = c.supportedAttrs(path)
    unsupp = [attr for attr in env.attr_info if not (attr.mask & supp_mask)]
    for attr in unsupp:
        ops = baseops + [c.nverify({attr.bitnum: attr.sample})]
        res = c.compound(ops)
        if attr.writeonly:
            checklist(res, [NFS4ERR_ATTRNOTSUPP, NFS4ERR_INVAL],
                      "VERIFY with unsupported attr %s" % attr.name)
        else:
            check(res, NFS4ERR_ATTRNOTSUPP,
                  "NVERIFY with unsupported attr %s" % attr.name)

####################################################

def testMandFile(t, env):
    """NVERIFY mandatory attributes against getattr

    FLAGS: nverify file all
    DEPEND: LOOKFILE
    CODE: NVF1r
    """
    _try_mand(env, env.opts.usefile)

def testMandDir(t, env):
    """NVERIFY mandatory attributes against getattr

    FLAGS: nverify dir all
    DEPEND: LOOKDIR
    CODE: NVF1d
    """
    _try_mand(env, env.opts.usedir)

def testMandLink(t, env):
    """NVERIFY mandatory attributes against getattr

    FLAGS: nverify symlink all
    DEPEND: LOOKLINK
    CODE: NVF1a
    """
    _try_mand(env, env.opts.uselink)

def testMandBlock(t, env):
    """NVERIFY mandatory attributes against getattr

    FLAGS: nverify block all
    DEPEND: LOOKBLK
    CODE: NVF1b
    """
    _try_mand(env, env.opts.useblock)

def testMandChar(t, env):
    """NVERIFY mandatory attributes against getattr

    FLAGS: nverify char all
    DEPEND: LOOKCHAR
    CODE: NVF1c
    """
    _try_mand(env, env.opts.usechar)

def testMandFifo(t, env):
    """NVERIFY mandatory attributes against getattr

    FLAGS: nverify fifo all
    DEPEND: LOOKFIFO
    CODE: NVF1f
    """
    _try_mand(env, env.opts.usefifo)

def testMandSocket(t, env):
    """NVERIFY mandatory attributes against getattr

    FLAGS: nverify socket all
    DEPEND: LOOKSOCK
    CODE: NVF1s
    """
    _try_mand(env, env.opts.usesocket)

def testTypeFile(t, env):
    """NVERIFY type of testtree files

    FLAGS: nverify file all
    DEPEND: LOOKFILE
    CODE: NVF2r
    """
    _try_type(env, env.opts.usefile, NF4REG)

def testTypeDir(t, env):
    """NVERIFY type of testtree files

    FLAGS: nverify dir all
    DEPEND: LOOKDIR
    CODE: NVF2d
    """
    _try_type(env, env.opts.usedir, NF4DIR)

def testTypeLink(t, env):
    """NVERIFY type of testtree files

    FLAGS: nverify symlink all
    DEPEND: LOOKLINK
    CODE: NVF2a
    """
    _try_type(env, env.opts.uselink, NF4LNK)

def testTypeBlock(t, env):
    """NVERIFY type of testtree files

    FLAGS: nverify block all
    DEPEND: LOOKBLK
    CODE: NVF2b
    """
    _try_type(env, env.opts.useblock, NF4BLK)

def testTypeChar(t, env):
    """NVERIFY type of testtree files

    FLAGS: nverify char all
    DEPEND: LOOKCHAR
    CODE: NVF2c
    """
    _try_type(env, env.opts.usechar, NF4CHR)

def testTypeFifo(t, env):
    """NVERIFY type of testtree files

    FLAGS: nverify fifo all
    DEPEND: LOOKFIFO
    CODE: NVF2f
    """
    _try_type(env, env.opts.usefifo, NF4FIFO)

def testTypeSocket(t, env):
    """NVERIFY type of testtree files

    FLAGS: nverify socket all
    DEPEND: LOOKSOCK
    CODE: NVF2s
    """
    _try_type(env, env.opts.usesocket, NF4SOCK)

def testBadSizeFile(t, env):
    """NVERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: nverify file all
    DEPEND: LOOKFILE
    CODE: NVF3r
    """
    _try_changed_size(env, env.opts.usefile)

def testBadSizeDir(t, env):
    """NVERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: nverify dir all
    DEPEND: LOOKDIR
    CODE: NVF3d
    """
    _try_changed_size(env, env.opts.usedir)

def testBadSizeLink(t, env):
    """NVERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: nverify symlink all
    DEPEND: LOOKLINK
    CODE: NVF3a
    """
    _try_changed_size(env, env.opts.uselink)

def testBadSizeBlock(t, env):
    """NVERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: nverify block all
    DEPEND: LOOKBLK
    CODE: NVF3b
    """
    _try_changed_size(env, env.opts.useblock)

def testBadSizeChar(t, env):
    """NVERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: nverify char all
    DEPEND: LOOKCHAR
    CODE: NVF3c
    """
    _try_changed_size(env, env.opts.usechar)

def testBadSizeFifo(t, env):
    """NVERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: nverify fifo all
    DEPEND: LOOKFIFO
    CODE: NVF3f
    """
    _try_changed_size(env, env.opts.usefifo)

def testBadSizeSocket(t, env):
    """NVERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: nverify socket all
    DEPEND: LOOKSOCK
    CODE: NVF3s
    """
    _try_changed_size(env, env.opts.usesocket)

def testNoFh(t, env):
    """NVERIFY without (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: nverify emptyfh all
    CODE: NVF4
    """
    c = env.c1
    res = c.compound([c.nverify({FATTR4_SIZE:17})])
    check(res, NFS4ERR_NOFILEHANDLE, "NVERIFY with no <cfh>")
                     
def testWriteOnlyFile(t, env):
    """NVERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: nverify file all
    DEPEND: LOOKFILE
    CODE: NVF5r
    """
    _try_write_only(env, env.opts.usefile)

def testWriteOnlyDir(t, env):
    """NVERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: nverify dir all
    DEPEND: LOOKDIR
    CODE: NVF5d
    """
    _try_write_only(env, env.opts.usedir)

def testWriteOnlyLink(t, env):
    """NVERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: nverify symlink all
    DEPEND: LOOKLINK
    CODE: NVF5a
    """
    _try_write_only(env, env.opts.uselink)

def testWriteOnlyBlock(t, env):
    """NVERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: nverify block all
    DEPEND: LOOKBLK
    CODE: NVF5b
    """
    _try_write_only(env, env.opts.useblock)

def testWriteOnlyChar(t, env):
    """NVERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: nverify char all
    DEPEND: LOOKCHAR
    CODE: NVF5c
    """
    _try_write_only(env, env.opts.usechar)

def testWriteOnlyFifo(t, env):
    """NVERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: nverify fifo all
    DEPEND: LOOKFIFO
    CODE: NVF5f
    """
    _try_write_only(env, env.opts.usefifo)

def testWriteOnlySocket(t, env):
    """NVERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: nverify socket all
    DEPEND: LOOKSOCK
    CODE: NVF5s
    """
    _try_write_only(env, env.opts.usesocket)

def testUnsupportedFile(t, env):
    """NVERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: nverify file all
    DEPEND: LOOKFILE
    CODE: NVF7r
    """
    _try_unsupported(env, env.opts.usefile)

def testUnsupportedDir(t, env):
    """NVERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: nverify dir all
    DEPEND: LOOKDIR
    CODE: NVF7d
    """
    _try_unsupported(env, env.opts.usedir)

def testUnsupportedLink(t, env):
    """NVERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: nverify symlink all
    DEPEND: LOOKLINK
    CODE: NVF7a
    """
    _try_unsupported(env, env.opts.uselink)

def testUnsupportedBlock(t, env):
    """NVERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: nverify block all
    DEPEND: LOOKBLK
    CODE: NVF7b
    """
    _try_unsupported(env, env.opts.useblock)

def testUnsupportedChar(t, env):
    """NVERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: nverify char all
    DEPEND: LOOKCHAR
    CODE: NVF7c
    """
    _try_unsupported(env, env.opts.usechar)

def testUnsupportedFifo(t, env):
    """NVERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: nverify fifo all
    DEPEND: LOOKFIFO
    CODE: NVF7f
    """
    _try_unsupported(env, env.opts.usefifo)

def testUnsupportedSocket(t, env):
    """NVERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: nverify socket all
    DEPEND: LOOKSOCK
    CODE: NVF7s
    """
    _try_unsupported(env, env.opts.usesocket)

###################################################

    def testNonUTF8(self):
        """NVERIFY with non-UTF8 FATTR4_OWNER should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 13
        """
        # FRED - NFS4_OK seems reasonable
        lookupops = self.ncl.lookup_path(self.regfile)
        for name in self.get_invalid_utf8strings():
            self._nverify(lookupops,{FATTR4_OWNER:name},
                          error=[NFS4ERR_INVAL, NFS4_OK])

