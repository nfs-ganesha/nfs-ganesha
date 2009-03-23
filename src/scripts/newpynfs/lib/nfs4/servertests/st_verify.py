from nfs4.nfs4_const import *
from environment import check, checklist, get_invalid_clientid, makeStaleId

def _try_mand(env, path):
    c = env.c1
    mand_bits = [attr.bitnum for attr in env.attr_info \
                 if attr.mandatory and attr.name != 'rdattr_error']
    dict = c.do_getattrdict(path, mand_bits)
    ops = c.use_obj(path) + [c.verify(dict)] + c.use_obj(path)
    res = c.compound(ops)
    check(res, msg="Verifying mandatory attributes against getattr")

def _try_type(env, path, type):
    c = env.c1
    ops = c.use_obj(path) + [c.verify({FATTR4_TYPE:type})] + c.use_obj(path)
    res = c.compound(ops)
    check(res, msg="Verifying type of /%s" % '/'.join(path))

def _try_changed_size(env, path):
    c = env.c1
    dict = c.do_getattrdict(path, [FATTR4_SIZE])
    dict[FATTR4_SIZE] += 1
    ops = c.use_obj(path) + [c.verify(dict)] + c.use_obj(path)
    res = c.compound(ops)
    check(res, NFS4ERR_NOT_SAME, "Verifying incorrect size")

def _try_write_only(env, path):
    c = env.c1
    baseops = c.use_obj(path)
    wo = [attr for attr in env.attr_info \
          if attr.writeonly or attr.name=='rdattr_error']
    for attr in wo:
        ops = baseops + [c.verify({attr.bitnum: attr.sample})]
        res = c.compound(ops)
        check(res, NFS4ERR_INVAL, "VERIFY with attr %s" % attr.name)

def _try_unsupported(env, path):
    c = env.c1
    baseops = c.use_obj(path)
    supp_mask = c.supportedAttrs(path)
    unsupp = [attr for attr in env.attr_info if not (attr.mask & supp_mask)]
    for attr in unsupp:
        ops = baseops + [c.verify({attr.bitnum: attr.sample})]
        res = c.compound(ops)
        if attr.writeonly:
            checklist(res, [NFS4ERR_ATTRNOTSUPP, NFS4ERR_INVAL],
                      "VERIFY with unsupported attr %s" % attr.name)
        else:
            check(res, NFS4ERR_ATTRNOTSUPP,
                  "VERIFY with unsupported attr %s" % attr.name)

####################################################

def testMandFile(t, env):
    """VERIFY mandatory attributes against getattr

    FLAGS: verify file all
    DEPEND: LOOKFILE
    CODE: VF1r
    """
    _try_mand(env, env.opts.usefile)

def testMandDir(t, env):
    """VERIFY mandatory attributes against getattr

    FLAGS: verify dir all
    DEPEND: LOOKDIR
    CODE: VF1d
    """
    _try_mand(env, env.opts.usedir)

def testMandLink(t, env):
    """VERIFY mandatory attributes against getattr

    FLAGS: verify symlink all
    DEPEND: LOOKLINK
    CODE: VF1a
    """
    _try_mand(env, env.opts.uselink)

def testMandBlock(t, env):
    """VERIFY mandatory attributes against getattr

    FLAGS: verify block all
    DEPEND: LOOKBLK
    CODE: VF1b
    """
    _try_mand(env, env.opts.useblock)

def testMandChar(t, env):
    """VERIFY mandatory attributes against getattr

    FLAGS: verify char all
    DEPEND: LOOKCHAR
    CODE: VF1c
    """
    _try_mand(env, env.opts.usechar)

def testMandFifo(t, env):
    """VERIFY mandatory attributes against getattr

    FLAGS: verify fifo all
    DEPEND: LOOKFIFO
    CODE: VF1f
    """
    _try_mand(env, env.opts.usefifo)

def testMandSocket(t, env):
    """VERIFY mandatory attributes against getattr

    FLAGS: verify socket all
    DEPEND: LOOKSOCK
    CODE: VF1s
    """
    _try_mand(env, env.opts.usesocket)

def testTypeFile(t, env):
    """VERIFY type of testtree files

    FLAGS: verify file all
    DEPEND: LOOKFILE
    CODE: VF2r
    """
    _try_type(env, env.opts.usefile, NF4REG)

def testTypeDir(t, env):
    """VERIFY type of testtree files

    FLAGS: verify dir all
    DEPEND: LOOKDIR
    CODE: VF2d
    """
    _try_type(env, env.opts.usedir, NF4DIR)

def testTypeLink(t, env):
    """VERIFY type of testtree files

    FLAGS: verify symlink all
    DEPEND: LOOKLINK
    CODE: VF2a
    """
    _try_type(env, env.opts.uselink, NF4LNK)

def testTypeBlock(t, env):
    """VERIFY type of testtree files

    FLAGS: verify block all
    DEPEND: LOOKBLK
    CODE: VF2b
    """
    _try_type(env, env.opts.useblock, NF4BLK)

def testTypeChar(t, env):
    """VERIFY type of testtree files

    FLAGS: verify char all
    DEPEND: LOOKCHAR
    CODE: VF2c
    """
    _try_type(env, env.opts.usechar, NF4CHR)

def testTypeFifo(t, env):
    """VERIFY type of testtree files

    FLAGS: verify fifo all
    DEPEND: LOOKFIFO
    CODE: VF2f
    """
    _try_type(env, env.opts.usefifo, NF4FIFO)

def testTypeSocket(t, env):
    """VERIFY type of testtree files

    FLAGS: verify socket all
    DEPEND: LOOKSOCK
    CODE: VF2s
    """
    _try_type(env, env.opts.usesocket, NF4SOCK)

def testBadSizeFile(t, env):
    """VERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: verify file all
    DEPEND: LOOKFILE
    CODE: VF3r
    """
    _try_changed_size(env, env.opts.usefile)

def testBadSizeDir(t, env):
    """VERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: verify dir all
    DEPEND: LOOKDIR
    CODE: VF3d
    """
    _try_changed_size(env, env.opts.usedir)

def testBadSizeLink(t, env):
    """VERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: verify symlink all
    DEPEND: LOOKLINK
    CODE: VF3a
    """
    _try_changed_size(env, env.opts.uselink)

def testBadSizeBlock(t, env):
    """VERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: verify block all
    DEPEND: LOOKBLK
    CODE: VF3b
    """
    _try_changed_size(env, env.opts.useblock)

def testBadSizeChar(t, env):
    """VERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: verify char all
    DEPEND: LOOKCHAR
    CODE: VF3c
    """
    _try_changed_size(env, env.opts.usechar)

def testBadSizeFifo(t, env):
    """VERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: verify fifo all
    DEPEND: LOOKFIFO
    CODE: VF3f
    """
    _try_changed_size(env, env.opts.usefifo)

def testBadSizeSocket(t, env):
    """VERIFY with bad size should return NFS4ERR_NOT_SAME

    FLAGS: verify socket all
    DEPEND: LOOKSOCK
    CODE: VF3s
    """
    _try_changed_size(env, env.opts.usesocket)

def testNoFh(t, env):
    """VERIFY without (cfh) should return NFS4ERR_NOFILEHANDLE

    FLAGS: verify emptyfh all
    CODE: VF4
    """
    c = env.c1
    res = c.compound([c.verify({FATTR4_SIZE:17})])
    check(res, NFS4ERR_NOFILEHANDLE, "VERIFY with no <cfh>")
                     
def testWriteOnlyFile(t, env):
    """VERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: verify file all
    DEPEND: LOOKFILE
    CODE: VF5r
    """
    _try_write_only(env, env.opts.usefile)

def testWriteOnlyDir(t, env):
    """VERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: verify dir all
    DEPEND: LOOKDIR
    CODE: VF5d
    """
    _try_write_only(env, env.opts.usedir)

def testWriteOnlyLink(t, env):
    """VERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: verify symlink all
    DEPEND: LOOKLINK
    CODE: VF5a
    """
    _try_write_only(env, env.opts.uselink)

def testWriteOnlyBlock(t, env):
    """VERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: verify block all
    DEPEND: LOOKBLK
    CODE: VF5b
    """
    _try_write_only(env, env.opts.useblock)

def testWriteOnlyChar(t, env):
    """VERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: verify char all
    DEPEND: LOOKCHAR
    CODE: VF5c
    """
    _try_write_only(env, env.opts.usechar)

def testWriteOnlyFifo(t, env):
    """VERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: verify fifo all
    DEPEND: LOOKFIFO
    CODE: VF5f
    """
    _try_write_only(env, env.opts.usefifo)

def testWriteOnlySocket(t, env):
    """VERIFY with write-only attribute should return NFS4ERR_INVAL

    FLAGS: verify socket all
    DEPEND: LOOKSOCK
    CODE: VF5s
    """
    _try_write_only(env, env.opts.usesocket)

def testUnsupportedFile(t, env):
    """VERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: verify file all
    DEPEND: LOOKFILE
    CODE: VF7r
    """
    _try_unsupported(env, env.opts.usefile)

def testUnsupportedDir(t, env):
    """VERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: verify dir all
    DEPEND: LOOKDIR
    CODE: VF7d
    """
    _try_unsupported(env, env.opts.usedir)

def testUnsupportedLink(t, env):
    """VERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: verify symlink all
    DEPEND: LOOKLINK
    CODE: VF7a
    """
    _try_unsupported(env, env.opts.uselink)

def testUnsupportedBlock(t, env):
    """VERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: verify block all
    DEPEND: LOOKBLK
    CODE: VF7b
    """
    _try_unsupported(env, env.opts.useblock)

def testUnsupportedChar(t, env):
    """VERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: verify char all
    DEPEND: LOOKCHAR
    CODE: VF7c
    """
    _try_unsupported(env, env.opts.usechar)

def testUnsupportedFifo(t, env):
    """VERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: verify fifo all
    DEPEND: LOOKFIFO
    CODE: VF7f
    """
    _try_unsupported(env, env.opts.usefifo)

def testUnsupportedSocket(t, env):
    """VERIFY with an unsupported attribute should return NFS4ERR_ATTRNOTSUPP

    FLAGS: verify socket all
    DEPEND: LOOKSOCK
    CODE: VF7s
    """
    _try_unsupported(env, env.opts.usesocket)

###################################################

    def _verify(self,lookupops,attrdict={FATTR4_SIZE:17},
                error=[NFS4_OK]):
        """call verify and then getattr
        """
        ops = [self.ncl.putrootfh_op()] + lookupops
        attrs = nfs4lib.dict2fattr(attrdict)
        ops.append(self.ncl.verify_op(attrs))
        ops.append(self.ncl.getattr(attrdict.keys()))
        res = self.ncl.do_ops(ops)
        self.assert_status(res,error)
        return res


    def testNonUTF8(self):
        """VERIFY with non-UTF8 FATTR4_OWNER should return NFS4ERR_INVAL

        Covered invalid equivalence classes: 13
        """
        # FRED - NFS4ERR_NOT_SAME seems reasonable
        lookupops = self.ncl.lookup_path(self.regfile)
        for name in self.get_invalid_utf8strings():
            self._verify(lookupops,{FATTR4_OWNER:name},
                         error=[NFS4ERR_INVAL, NFS4ERR_NOT_SAME])
