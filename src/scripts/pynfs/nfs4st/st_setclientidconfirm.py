from base_st_classes import *

class SetclientidconfirmSuite(NFSSuite):
    """Test operation 36: SETCLIENTID_CONFIRM

    Equivalence partitioning:

    Input Condition: clientid
        Valid equivalence classes:
            valid clientid(10)
        Invalid equivalence classes:
            stale clientid(11)
    """
    # Override setUp. Just connect, don't do SETCLIENTID etc. 
    def setUp(self):
        self.connect()

    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """SETCLIENTID_CONFIRM on valid verifier
        
        Covered valid equivalence classes: 10
        """
        
        # SETCLIENTID
        setclientidop = self.ncl.setclientid()
        res =  self.do_compound([setclientidop])
        self.assert_OK(res)
        clientid = res.resarray[0].arm.arm.clientid
        
        # SETCLIENTID_CONFIRM
        setclientid_confirmop = self.ncl.setclientid_confirm_op(clientid)
        res =  self.do_compound([setclientid_confirmop])
        self.assert_OK(res)

    #
    # Testcases covering invalid equivalence classes.
    #
    def testStale(self):
        """SETCLIENTID_CONFIRM on stale vrf should return NFS4ERR_STALE_CLIENTID

        Covered invalid equivalence classes: 11
        """
        clientid = self.get_invalid_clientid()
        setclientid_confirmop = self.ncl.setclientid_confirm_op(clientid)
        res =  self.do_compound([setclientid_confirmop])
        self.assert_status(res, [NFS4ERR_STALE_CLIENTID])
