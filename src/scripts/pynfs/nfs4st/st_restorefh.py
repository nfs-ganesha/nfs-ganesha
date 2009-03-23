from base_st_classes import *

class RestorefhSuite(NFSSuite):
    """Test operation 31: RESTOREFH

    Equivalence partitioning:

    Input Condition: saved filehandle
        Valid equivalence classes:
            valid filehandle(10)
        Invalid equivalence classes:
            no filehandle(11)

    Comments: We do not test restoration of a invalid filehandle,
    since it's hard to save one. It's not possible to PUTFH an invalid
    filehandle, for example.
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """SAVEFH and RESTOREFH

        Covered valid equivalence classes: 10

        Comments: Also tests SAVEFH operation. 
        """
        # The idea is to use a sequence of operations like this:
        # PUTROOTFH, LOOKUP, GETFH#1, SAVEFH, LOOKUP, RESTOREFH, GETH#2.
        # If this procedure succeeds and result from GETFH#1 and GETFH#2 match,
        # things should be OK.

        # Lookup a file, get and save FH. 
        operations = [self.putrootfhop]
        operations.extend(self.ncl.lookup_path(self.regfile))
        operations.append(self.ncl.getfh_op())
        operations.append(self.ncl.savefh_op())

        # Lookup another file.
        operations.append(self.putrootfhop)
        operations.extend(self.ncl.lookup_path(self.hello_c))

        # Restore saved fh and get fh. 
        operations.append(self.ncl.restorefh_op())
        operations.append(self.ncl.getfh_op())
        
        res = self.do_compound(operations)
        self.assert_OK(res)

        # putrootfh + #lookups
        getfh1index = 1 + len(self.regfile) 
        fh1 = res.resarray[getfh1index].arm.arm.object
        # getfh1index + savefh + putrootfh + #lookups + restorefh + getfh
        getfh2index = getfh1index + 2 + len(self.hello_c) + 2
        fh2 = res.resarray[getfh2index].arm.arm.object 
        self.failIf(fh1 != fh2, "restored FH does not match saved FH")

    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """RESTOREFH without (sfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 11
        """
        res = self.do_compound([self.ncl.restorefh_op()])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])
