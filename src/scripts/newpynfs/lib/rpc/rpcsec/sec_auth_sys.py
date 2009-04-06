from base import SecFlavor, SecError
from rpc.rpc_const import AUTH_SYS
from rpc.rpc_type import opaque_auth
from xdrlib import Packer, Error

class SecAuthSys(SecFlavor):
    def __init__(self, stamp, machinename, uid, gid, gids):
        if len(machinename) > 255:
            raise SecError("machinename %s is too long" % machinename)
        if len(gids) > 16:
            raise SecError("gid array too long: %s" % str(gids))
        try:
            p = Packer()
            p.pack_int(stamp)
            p.pack_string(machinename)
            p.pack_uint(uid)
            p.pack_uint(gid)
            p.pack_array(gids, p.pack_uint)
            self.cred = p.get_buffer()
        except Error, e:
            raise SecError("Packing error: %s", str(e))
        self.uid = uid
        self.gid = gid

    def make_cred(self):
        return opaque_auth(AUTH_SYS, self.cred), None

    def get_owner(self):
        return self.uid

    def get_group(self):
        return self.gid
