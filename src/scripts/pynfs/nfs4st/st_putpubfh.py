from base_st_classes import *

class PutpubfhSuite(NFSSuite):
    """Test operation 23: PUTPUBFH

    Equivalence partitioning:

    Input Condition: -
        Valid equivalence classes:
            -
        Invalid equivalence classes:
            -
    """
    def testOp(self):
        """Testing PUTPUBFH

        Covered valid equivalence classes: -
        """
        putpubfhop = self.ncl.putpubfh_op()
        res = self.do_compound([putpubfhop])
        self.assert_OK(res)
