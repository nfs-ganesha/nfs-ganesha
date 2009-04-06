from base_st_classes import *

class GetfhSuite(NFSSuite):
    """Test operation 10: GETFH

    FIXME: Add attribute directory and named attribute testing. 

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            file(1)
            link(2)
            block(3)
            char(4)
            socket(5)
            FIFO(6)
            dir(7)
        Invalid equivalence classes:
            invalid filehandle(8)
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testAllObjects(self):
        """GETFH on all type of objects

        Covered valid equivalence classes: 1, 2, 3, 4, 5, 6, 7
        """
        for lookupops in self.ncl.lookup_all_objects():
            operations = [self.putrootfhop] + lookupops
            operations.append(self.ncl.getfh_op())
            res = self.do_compound(operations)
            self.assert_OK(res)            
    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """GETFH should fail with NFS4ERR_NOFILEHANDLE if no (cfh)

        Covered invalid equivalence classes: 8

        Comments: GETFH should fail with NFS4ERR_NOFILEHANDLE if no
        (cfh)
        """
        getfhop = self.ncl.getfh_op()
        res = self.do_compound([getfhop])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])
