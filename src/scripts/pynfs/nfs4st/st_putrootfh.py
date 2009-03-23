from base_st_classes import *

class PutrootfhSuite(NFSSuite):
    """Test operation 24: PUTROOTFH

    Equivalence partitioning:

    Input Condition: -
        Valid equivalence classes:
            -
        Invalid equivalence classes:
            -
    """
    def testOp(self):
        """Testing PUTROOTFH

        Covered valid equivalence classes: -
        """
        putrootfhop = self.ncl.putrootfh_op()
        res = self.do_compound([putrootfhop])
        self.assert_OK(res)
