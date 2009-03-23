from base_st_classes import *

class SetclientidSuite(NFSSuite):
    """Test operation 35: SETCLIENTID

    FIXME: Test cases that trigger NFS4ERR_CLID_INUSE. 

    Equivalence partitioning:

    Input Condition: client.verifier
        Valid equivalence classes:
            all input(10)
        Invalid equivalence classes:
            -
    Input Condition: client.id
        Valid equivalence classes:
            all input(20)
        Invalid equivalence classes:
            -
    Input Condition: callback.cb_program
            all input(30)
        Invalid equivalence classes:
            -
    Input Condition: callback.cb_location
            all input(40)
        Invalid equivalence classes:
            -

    Comments: If client has never connected to the server, every
    client.verifier and client.id is valid. All callback data is also
    allowed as input, but failing to provide the correct addres means
    callbacks will not be used. 
    """
    #
    # Testcases covering valid equivalence classes.
    #
    def testValid(self):
        """Simple SETCLIENTID

        Covered valid equivalence classes: 10, 20, 30, 40
        """
        # client
        verifier = self.ncl.gen_random_64()
        id = self.ncl.gen_uniq_id()
        client = nfs_client_id4(self.ncl, verifier, id)

        # callback
        cb_program = 0
        r_netid = "udp"
        # FIXME
        # Internet, Port number, IP, RFU
        r_addr = "0002" + "0000" + "00112233" + "00" 
        cb_location = clientaddr4(self.ncl, r_netid, r_addr)
        callback = cb_client4(self.ncl, cb_program, cb_location)
        
        setclientidop = self.ncl.setclientid_op(client, callback)
        res = self.do_compound([setclientidop])
        self.assert_OK(res)

    #
    # Extra tests.
    #
    def _set(self, ncl, id):
        setclientidop = ncl.setclientid(id=id)
        res = self.do_compound([setclientidop], ncl=ncl)
        return res

    def _confirm(self, ncl, clientid):
        setclientid_confirmop = ncl.setclientid_confirm_op(clientid)
        res = self.do_compound([setclientid_confirmop], ncl=ncl)
        return res
    
    def testInUse(self):
        """SETCLIENTID with same nfs_client_id.id should return NFS4ERR_CLID_INUSE

        Extra test
        """
        id = self.ncl.gen_uniq_id()

        # 1st SETCLIENTID + SETCLIENTID_CONFIRM
        #self._set_and_confirm(self.ncl, id)
        res = self._set(self.ncl, id)
        self.assert_OK(res)
        clientid = res.resarray[0].arm.arm.clientid
        res = self._confirm(self.ncl, clientid)
        self.assert_OK(res)
        

        # 2nd SETCLIENTID 
        ncl2 = self.create_client(UID+1, GID+1)
        res = self._set(ncl2, id)
        self.assert_status(res, [NFS4ERR_CLID_INUSE])
        # FIXME: Should NFS4ERR_CLID_INUSE be returned on SETCLIENTID
        # or SETCLIENTID_CONFIRM?
        #clientid = res.resarray[0].arm.arm.clientid
        #res = self._confirm(self.ncl, clientid)
        #self.assert_OK(res)
