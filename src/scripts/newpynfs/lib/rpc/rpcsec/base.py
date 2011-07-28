from rpc.rpc_const import AUTH_NONE
from rpc.rpc_type import opaque_auth

class SecError(Exception):
    pass

class SecFlavor(object):
    _none = opaque_auth(AUTH_NONE, '')
    
    def initialize(self, client):
        pass

    def secure_data(self, data, seqnum):
        """Filter procedure arguments before sending to server"""
        return data

    def unsecure_data(self, data, seqnum):
        """Filter procedure results received from server"""
        return data

    def make_cred(self):
        """Credential and seqnum sent with each RPC call"""
        return self._none, None

    def make_verf(self, data):
        """Verifier sent with each RPC call

        'data' is packed header upto and including cred
        """
        return self._none

    def make_reply_verf(self, data):
        """Verifier sent by server with each RPC reply"""
        # FRED - currently data is always ''
        return self._none

    def get_owner(self):
        """Return uid"""
        return 0

    def get_group(self):
        """Return gid"""
        return 0
