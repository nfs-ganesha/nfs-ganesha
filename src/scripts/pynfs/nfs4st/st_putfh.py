from base_st_classes import *

class PutfhSuite(NFSSuite):
    """Test operation 22: PUTFH

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:

    Input Condition: supplied filehandle
        Valid equivalence classes:
            file(1)
            dir(2)
            block(3)
            char(4)
            link(5)
            socket(6)
            FIFO(7)
            attribute directory(8)
            named attribute(9)
        Invalid equivalence classes:
            invalid filehandle(10)

    Comments: It's not possible to cover eq. class 10, since a filehandle
    is opaque to the client.
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testAllObjects(self):
        """PUTFH followed by GETFH on all type of objects

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7
        """
        # Fetch filehandles of all types
        # List with (objpath, fh)
        filehandles = []
        for lookupops in self.ncl.lookup_all_objects():
            operations = [self.putrootfhop] + lookupops
            
            operations.append(self.ncl.getfh_op())
            res = self.do_compound(operations)
            self.assert_OK(res)

            objpath = self.ncl.lookuplist2comps(lookupops)
            fh = res.resarray[-1].arm.arm.object
            filehandles.append((objpath, fh))

        # Try PUTFH & GETFH on all these filehandles. 
        for (objpath, fh) in filehandles:
            putfhop = self.ncl.putfh_op(fh)
            getfhop = self.ncl.getfh_op()
            res = self.do_compound([putfhop, getfhop])
            self.assert_OK(res)

            new_fh = res.resarray[-1].arm.arm.object
            self.failIf(new_fh != fh, "GETFH after PUTFH returned different fh "\
                        "for object %s" % objpath)
