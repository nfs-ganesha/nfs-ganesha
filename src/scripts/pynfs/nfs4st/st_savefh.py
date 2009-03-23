from base_st_classes import *

class SavefhSuite(NFSSuite):
    """Test operation 32: SAVEFH

    Equivalence partitioning:

    Input Condition: current filehandle
        Valid equivalence classes:
            valid filehandle(10)
        Invalid equivalence classes:
            no filehandle(11)

    Comments: Equivalence class 10 is covered by
    RestorefhSuite.testValid.
    """
    #
    # Testcases covering invalid equivalence classes.
    #
    def testNoFh(self):
        """SAVEFH without (cfh) should return NFS4ERR_NOFILEHANDLE

        Covered invalid equivalence classes: 11
        """
        res = self.do_compound([self.ncl.savefh_op()])
        self.assert_status(res, [NFS4ERR_NOFILEHANDLE])
