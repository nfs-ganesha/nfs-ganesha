from base_st_classes import *

class FilehandleSuite(NFSSuite):
    """Test different aspects of file handle management
    """
    def setUp(self):
        NFSSuite.setUp(self)
        self.obj_name = "object1"
        self.lookup_dir_ops = self.ncl.lookup_path(self.tmp_dir)
        self.lookup_obj = self.ncl.lookup_op(self.obj_name)
    
    def _verify_persistent(self):
        operations = [self.putrootfhop] + self.lookup_dir_ops + [self.lookup_obj]
        operations.append(self.ncl.getattr([FATTR4_FH_EXPIRE_TYPE]))
        res = self.do_compound(operations)
        self.assert_OK(res)

        obj = res.resarray[-1].arm.arm.obj_attributes
        d =  nfs4lib.fattr2dict(obj)
        fhtype = d["fh_expire_type"]
        self.failUnless(self._verify_fhtype(fhtype), "Invalid fh_expire_type")
        return (fhtype == FH4_PERSISTENT)
            
    def _verify_fhtype(self, fhtype):
        """Verify consistency for filehandle expire type"""
        if fhtype == FH4_PERSISTENT:
            return 1

        if (fhtype & FH4_VOLATILE_ANY) and (fhtype & (FH4_VOL_MIGRATION | FH4_VOL_RENAME)):
            # FH4_VOL_MIGRATION and FH4_VOL_RENAME may not be combined 
            # with FH4_VOLATILE_ANY
            return 0

        return 1
    
    def testStale(self):
        """Presenting persistent fh repr. deleted object should yield NFS4ERR_STALE

        Extra test

        See section 4.2.2 in specification. 
        """
        if not self.create_object(): return
        # This test requires persistent a filehandle
        if not self._verify_persistent():
            self.info_message("Directory fh is not persistent, which is required")
            self.info_message("by this test. Skipping")
            return

        # FIXME: Rest of test not implemented. Fix when I have access to a
        # server that provides persistent fh:s. 
        self.info_message("(TEST NOT IMPLEMENTED)")
