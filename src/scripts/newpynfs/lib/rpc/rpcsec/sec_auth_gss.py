from base import SecFlavor, SecError
from rpc.rpc_const import RPCSEC_GSS
from rpc.rpc_type import opaque_auth
from gss_const import *
import gss_pack
import gss_type
import gssapi
import threading

def show_major(m):
    """Return string corresponding to major code"""
    if m == 0:
        return gss_major_codes[0]
    call = m & 0xff000000L
    routine = m & 0xff0000
    supp = m & 0xffff
    out = []
    if call:
        out.append(gss_major_codes[call])
    if routine:
        out.append(gss_major_codes[routine])
    if supp:
        out.append(gss_major_codes[supp])
    return ' | '.join(out)

class SecAuthGss(SecFlavor):
    krb5_oid = "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02"
    def __init__(self, service=rpc_gss_svc_none):
        t = threading.currentThread()
        self.lock = threading.Lock()
        self.gss_seq_num = 0
        self.init = 1
        self.service = service
        self._packer = {t : gss_pack.GSSPacker()}
        self._unpacker = {t : gss_pack.GSSUnpacker('')}

    def getpacker(self):
        t = threading.currentThread()
        self.lock.acquire()
        if t in self._packer:
            out = self._packer[t]
        else:
            out = self._packer[t] = gss_pack.GSSPacker()
            self._unpacker[t] = gss_pack.GSSUnpacker('')
        self.lock.release()
        return out

    def getunpacker(self):
        t = threading.currentThread()
        self.lock.acquire()
        if t in self._unpacker:
            out = self._unpacker[t]
        else:
            self._packer[t] = gss_pack.GSSPacker()
            out = self._unpacker[t] = gss_pack.GSSUnpacker('')
        self.lock.release()
        return out

    def initialize(self, client): # Note this is no thread safe
        """Set seq_num, init, handle, and context"""
        self.gss_seq_num = 0
        #d = gssapi.importName("nfs@%s" % client.remotehost)
        d = gssapi.importName("nfs@yack31.c-yack.bruyeres.t")
        if d['major'] != gssapi.GSS_S_COMPLETE:
            raise SecError, "gssapi.importName returned: %s" % \
                  show_major(d['major'])
        name = d['name']
        # We need to send NULLPROCs with token from initSecContext
        good_major = [gssapi.GSS_S_COMPLETE, gssapi.GSS_S_CONTINUE_NEEDED]
        self.init = 1
        reply_token = ''
        reply_major = ''
        context = buffer('')
        while True:
            #major, x, token, context, x, x, x = \
            d = gssapi.initSecContext(name, reply_token, context)
            major = d['major']
            context = d['context']
            if major not in good_major:
                raise SecError, "gssapi.initSecContext returned: %s" % \
                      show_major(major)
            if (major == gssapi.GSS_S_CONTINUE_NEEDED) and \
                   (reply_major == gssapi.GSS_S_COMPLETE):
                raise SecError, "Unexpected GSS_S_COMPLETE from server"
            token = d['token']
            if reply_major != gssapi.GSS_S_COMPLETE:
                # FRED - sec 5.2.2 of RFC 2203 mentions possibility that
                # no token is returned.  But then how get handle?
                p = self.getpacker()
                p.reset()
                p.pack_opaque(token)
                data = p.get_buffer()
                reply = client.call(0, data)
                up = self.getunpacker()
                up.reset(reply)
                res = up.unpack_rpc_gss_init_res()
                up.done()
                reply_major = res.gss_major
                if reply_major not in good_major:
                    raise SecError, "Server returned: %s" % \
                          show_major(reply_major)
                self.init = 2
                reply_token = res.gss_token
            if major == gssapi.GSS_S_COMPLETE:
                if reply_major != gssapi.GSS_S_COMPLETE:
                    raise SecError, "Unexpected COMPLETE from client"
                break
        self.gss_context = context
        self.gss_handle = res.handle
        self.init = 0
        
    def make_cred(self):
        """Credential sent with each RPC call"""
        seq = 0
        if self.init == 1: # first call in context creation
            cred = self._make_cred_gss('', rpc_gss_svc_none, RPCSEC_GSS_INIT)
        elif self.init > 1: # subsequent calls in context creation
            cred = self._make_cred_gss('', rpc_gss_svc_none,
                                  RPCSEC_GSS_CONTINUE_INIT)
        else: # data transfer calls
            self.lock.acquire()
            self.gss_seq_num += 1 # FRED - check for overflow
            seq = self.gss_seq_num
            self.lock.release()
            cred = self._make_cred_gss(self.gss_handle, self.service, seq=seq)
        return opaque_auth(RPCSEC_GSS, cred), seq

    def make_verf(self, data):
        """Verifier sent with each RPC call

        'data' is packed header upto and including cred
        """
        if self.init:
            return self._none
        else:
            d = gssapi.getMIC(self.gss_context, data)
            major = d['major']
            if major != gssapi.GSS_S_COMPLETE:
                raise SecError, "gssapi.getMIC returned: %s" % \
                      show_major(major)
            return opaque_auth(RPCSEC_GSS, d['token'])
        
    def _make_cred_gss(self, handle, service, gss_proc=RPCSEC_GSS_DATA, seq=0):
        data = gss_type.rpc_gss_cred_vers_1_t(gss_proc, seq, service, handle)
        cred = gss_type.rpc_gss_cred_t(RPCSEC_GSS_VERS_1, data)
        p = self.getpacker()
        p.reset()
        p.pack_rpc_gss_cred_t(cred)
        return p.get_buffer()

    def secure_data(self, data, seqnum):
        """Filter procedure arguments before sending to server"""
        if self.service == rpc_gss_svc_none or self.init:
            pass
        elif self.service == rpc_gss_svc_integrity:
            # data = opaque[gss_seq_num+data] + opaque[checksum]
            p = self.getpacker()
            p.reset()
            p.pack_uint(seqnum)
            data = p.get_buffer() + data
            d = gssapi.getMIC(self.gss_context, data)
            if d['major'] != gssapi.GSS_S_COMPLETE:
                raise SecError, "gssapi.getMIC returned: %s" % \
                      show_major(d['major'])
            p.reset()
            p.pack_opaque(data)
            p.pack_opaque(d['token'])
            data = p.get_buffer()
        elif self.service == rpc_gss_svc_privacy:
            # data = opaque[wrap([gss_seq_num+data])]
            # FRED - this is untested
            p = self.getpacker()
            p.reset()
            p.pack_uint(seqnum)
            data = p.get_buffer() + data
            d = gssapi.wrap(self.gss_context, data)
            if d['major'] != gssapi.GSS_S_COMPLETE:
                raise SecError, "gssapi.wrap returned: %s" % \
                      show_major(d['major'])
            p.reset()
            p.pack_opaque(d['msg'])
            data = p.get_buffer()
        else:
            raise SecError, "Unknown service %i for RPCSEC_GSS" % self.service
        return data

    def unsecure_data(self, data, orig_seqnum):
        """Filter procedure results received from server"""
        if self.service == rpc_gss_svc_none or self.init:
            pass
        elif self.service == rpc_gss_svc_integrity:
            # data = opaque[gss_seq_num+data] + opaque[checksum]
            p = self.getunpacker()
            p.reset(data)
            data = p.unpack_opaque()
            checksum = p.unpack_opaque()
            p.done()
            d = gssapi.verifyMIC(self.gss_context, data, checksum)
            if d['major'] != gssapi.GSS_S_COMPLETE:
                raise SecError, "gssapi.verifyMIC returned: %s" % \
                      show_major(d['major'])
            p.reset(data)
            seqnum = p.unpack_uint()
            if seqnum != orig_seqnum:
                raise SecError, \
                      "Mismatched seqnum in reply: got %i, expected %i" % \
                      (seqnum, orig_seqnum)
            data = p.get_buffer()[p.get_position():]
        elif self.service == rpc_gss_svc_privacy:
            # data = opaque[wrap([gss_seq_num+data])]
            # FRED - this is untested
            p = self.getunpacker()
            p.reset(data)
            data = p.unpack_opaque()
            p.done()
            d = gssapi.unwrap(self.gss_context, data)
            if d['major'] != gssapi.GSS_S_COMPLETE:
                raise SecError, "gssapi.unwrap returned %s" % \
                      show_major(d['major'])
            p.reset(d['msg'])
            seqnum = p.unpack_uint()
            if seqnum != orig_seqnum:
                raise SecError, \
                      "Mismatched seqnum in reply: got %i, expected %i" % \
                      (seqnum, self.orig_seqnum)
            data = p.get_buffer()[p.get_position():]
        else:
            raise SecError, "Unknown service %i for RPCSEC_GSS" % self.service
        return data

