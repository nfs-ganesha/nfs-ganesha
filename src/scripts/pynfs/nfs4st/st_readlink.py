from base_st_classes import *

class ReadlinkSuite(NFSSuite):
    """Test operation 27: READLINK

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            symlink(10)
        Invalid equivalence classes:
            not symlink or directory(11)
            directory(12)
            no filehandle(13)
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testReadlink(self):
        """READLINK on link

        Covered valid equivalence classes: 10
        """
        lookupops = self.ncl.lookup_path(self.linkfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.readlink_op())
        
        res = self.do_compound(operations)
        self.assert_OK(res)
        linkdata = res.resarray[-1].arm.arm.link
        self.failIf(linkdata != "fd0",
                    "link data was %s, should be fd0" % linkdata)
    #
    # Testcases covering invalid equivalence classes.
    #
    def testFhNotSymlink(self):
        """READLINK on non-symlink objects should return NFS4ERR_INVAL

        Covered valid equivalence classes: 11
        """
        for pathcomps in [self.regfile,
                          self.blockfile,
                          self.charfile,
                          self.socketfile,
                          self.fifofile]:
            lookupops = self.ncl.lookup_path(pathcomps)
            operations = [self.putrootfhop] + lookupops
            operations.append(self.ncl.readlink_op())

            res = self.do_compound(operations)

            if res.status != NFS4ERR_INVAL:
                self.info_message("READLINK on %s did not return NFS4ERR_INVAL" \
                                  % pathcomps)
            
            self.assert_status(res, [NFS4ERR_INVAL])
            
    def testDirFh(self):
        """READLINK on a directory should return NFS4ERR_ISDIR

        Covered valid equivalence classes: 12
        """
        lookupops = self.ncl.lookup_path(self.dirfile)
        operations = [self.putrootfhop] + lookupops
        operations.append(self.ncl.readlink_op())

        res = self.do_compound(operations)
        self.assert_status(res, [NFS4ERR_ISDIR])
        

    def testNoFh(self):
        """READLINK without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 13
        """
        readlinkop = self.ncl.readlink_op()
        res = self.do_compound([readlinkop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])
