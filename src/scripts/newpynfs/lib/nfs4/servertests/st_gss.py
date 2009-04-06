from nfs4.nfs4_const import *
from environment import check
from socket import timeout
import rpc
import rpc.rpcsec.gss_const as gss

def _using_gss(t, env):
    if 'gss' not in rpc.supported:
        return False
    return isinstance(env.c1.security, rpc.supported['gss'])

def _using_service(t, env):
    return env.c1.security.service != gss.rpc_gss_svc_none

def _using_integrity(t, env):
    return env.c1.security.service == gss.rpc_gss_svc_integrity

def testBadGssSeqnum(t, env):
    """GSS: using an old gss_seq_num should cause dropped reply

    FLAGS: gss all
    DEPEND: _using_gss
    CODE: GSS1
    """
    c = env.c1
    res = c.compound([c.putrootfh_op()])
    check(res)
    success = False
    orig = c.security.gss_seq_num
    try:
        c.security.gss_seq_num -= 1
        try:
            res = c.compound([c.putrootfh_op()])
        except timeout:
            success = True
        if not success:
            t.fail("Using old gss_seq_num %i should cause dropped reply" %
                   (orig + 1))
    finally:
        c.security.gss_seq_num = orig
        
def testInconsistentGssSeqnum(t, env):
    """GSS: using inconsistent gss_seq_nums should return GARBAGE_ARGS in rpc accept_stat

    See RFC2203, end of sect 5.3.3.1

    FLAGS: gss all
    DEPEND: _using_gss _using_service
    CODE: GSS2
    """
    c = env.c1
    orig_funct = c.security.secure_data
    def bad_secure_data(data, seqnum):
        # Mess up gss_seq_num
        return orig_funct(data, seqnum + 1)

    try:
        c.security.secure_data = bad_secure_data
        try:
            res = c.compound([c.putrootfh_op()])
            e = "operation erroneously suceeding"
        except rpc.RPCAcceptError, e:
            if e.stat == rpc.GARBAGE_ARGS:
                # This is correct response
                return
        except Exception, e:
            pass
        t.fail("Using inconsistent gss_seq_nums in header and body of message "
               "should return GARBAGE_ARGS, instead got %s" % e)
    finally:
        c.security.secure_data = orig_funct

def testBadVerfChecksum(t, env):
    """GSS: Bad verifier checksum should return RPCSEC_GSS_CREDPROBLEM

    FLAGS: gss all
    DEPEND: _using_gss
    CODE: GSS3
    """
    c = env.c1
    orig_funct = c.security.make_verf
    def bad_make_verf(data):
        # Mess up verifier
        return orig_funct(data + "x")

    try:
        c.security.make_verf = bad_make_verf
        try:
            res = c.compound([c.putrootfh_op()])
            e = "peration erroneously suceeding"
        except rpc.RPCDeniedError, e:
            if e.stat == rpc.AUTH_ERROR and e.astat == rpc.RPCSEC_GSS_CREDPROBLEM:
                # This is correct response
                return
        except Exception, e:
            pass
        t.fail("Using bad verifier checksum in header "
               "should return RPCSEC_GSS_CREDPROBLEM, instead got %s" % e)
    finally:
        c.security.make_verf = orig_funct

def testBadDataChecksum(t, env):
    """GSS: Bad data checksum should return GARBAGE_ARGS

    See RFC2203 sect 5.3.3.4.2

    FLAGS: gss all
    DEPEND: _using_gss _using_integrity
    CODE: GSS4
    """
    c = env.c1
    orig_funct = c.security.secure_data
    def bad_secure_data(data, seqnum):
        # Mess up checksum
        data = orig_funct(data, seqnum)
        if data[-4]:
            tail = chr(0) + data[-3:]
        else:
            tail = chr(1) + data[-3:]
        return data[:-4] + tail

    try:
        c.security.secure_data = bad_secure_data
        try:
            res = c.compound([c.putrootfh_op()])
            e = "operation erroneously suceeding"
        except rpc.RPCAcceptError, e:
            if e.stat == rpc.GARBAGE_ARGS:
                # This is correct response
                return
        except Exception, e:
            pass
        t.fail("Using bad data checksum for body of message "
               "should return GARBAGE_ARGS, instead got %s" % e)
    finally:
        c.security.secure_data = orig_funct

def testBadVersion(t, env):
    """GSS: bad version number should return AUTH_BADCRED

    See RFC2203 end of sect 5.3.3.3

    FLAGS: gss all
    DEPEND: _using_gss
    CODE: GSS5
    """
    c = env.c1
    orig_funct = c.security._make_cred_gss
    def bad_version(handle, service, gss_proc=0, seq=0):
        # Mess up version in credential
        p = c.security.getpacker()
        p.reset()
        p.pack_uint(version)
        p.pack_uint(gss_proc)
        p.pack_uint(seq)
        p.pack_uint(service)
        p.pack_opaque(handle)
        return p.get_buffer()

    try:
        c.security._make_cred_gss = bad_version
        bad_versions = [0, 2, 3, 1024]
        for version in bad_versions:
            try:
                res = c.compound([c.putrootfh_op()])
                e = "operation erroneously suceeding"
            except rpc.RPCDeniedError, e:
                if e.stat == rpc.AUTH_ERROR and e.astat == rpc.AUTH_BADCRED:
                    # This is correct response
                    e = None
            except Exception, e:
                pass
            if e is not None:
                t.fail("Using bad gss version number %i "
                       "should return AUTH_BADCRED, instead got %s" %
                       (version, e))
    finally:
        c.security._make_cred_gss = orig_funct

def testHighSeqNum(t, env):
    """GSS: a seq_num over MAXSEQ should return RPCSEC_GSS_CTXPROBLEM

    FLAGS: gss all
    DEPEND: _using_gss
    CODE: GSS6
    """
    c = env.c1
    orig_seq = c.security.gss_seq_num
    try:
        c.security.gss_seq_num = gss.MAXSEQ + 1
        try:
            res = c.compound([c.putrootfh_op()])
            e = "operation erroneously suceeding"
        except rpc.RPCDeniedError, e:
            if e.stat == rpc.AUTH_ERROR and e.astat == rpc.RPCSEC_GSS_CTXPROBLEM:
                # This is correct response
                return
        except Exception, e:
            pass
        t.fail("Using gss_seq_num over MAXSEQ "
               "should return RPCSEC_GSS_CTXPROBLEM, instead got %s" % e)
    finally:
        c.security.gss_seq_num = orig_seq

def testBadProcedure(t, env):
    """GSS: bad procedure number should return AUTH_BADCRED

    FLAGS: gss all
    DEPEND: _using_gss
    CODE: GSS7
    """
    c = env.c1
    orig_funct = c.security._make_cred_gss
    def bad_proc(handle, service, gss_proc=0, seq=0):
        # Mess up procedure number in credential
        p = c.security.getpacker()
        p.reset()
        p.pack_uint(1)
        p.pack_uint(proc)
        p.pack_uint(seq)
        p.pack_uint(service)
        p.pack_opaque(handle)
        return p.get_buffer()

    try:
        c.security._make_cred_gss = bad_proc
        bad_procss = [4, 5, 1024]
        for proc in bad_procss:
            try:
                res = c.compound([c.putrootfh_op()])
                e = "operation erroneously suceeding"
            except rpc.RPCDeniedError, e:
                if e.stat == rpc.AUTH_ERROR and e.astat == rpc.AUTH_BADCRED:
                    # This is correct response
                    e = None
            except Exception, e:
                pass
            if e is not None:
                t.fail("Using bad gss procedure number %i "
                       "should return AUTH_BADCRED, instead got %s" %
                       (proc, e))
    finally:
        c.security._make_cred_gss = orig_funct

def testBadService(t, env):
    """GSS: bad service number should return AUTH_BADCRED

    See RFC2203 end of sect 5.3.3.3

    FLAGS: gss all
    DEPEND: _using_gss
    CODE: GSS8
    """
    c = env.c1
    orig_funct = c.security._make_cred_gss
    def bad_service(handle, ignore_service, gss_proc=0, seq=0):
        # Mess up procedure number in credential
        p = c.security.getpacker()
        p.reset()
        p.pack_uint(1)
        p.pack_uint(gss_proc)
        p.pack_uint(seq)
        p.pack_uint(service)
        p.pack_opaque(handle)
        return p.get_buffer()

    try:
        c.security._make_cred_gss = bad_service
        bad_services = [0, 4, 5, 1024]
        for service in bad_services:
            try:
                res = c.compound([c.putrootfh_op()])
                e = "operation erroneously suceeding"
            except rpc.RPCDeniedError, e:
                if e.stat == rpc.AUTH_ERROR and e.astat == rpc.AUTH_BADCRED:
                    # This is correct response
                    e = None
            except Exception, e:
                pass
            if e is not None:
                t.fail("Using bad gss service number %i "
                       "should return AUTH_BADCRED, instead got %s" %
                       (service, e))
    finally:
        c.security._make_cred_gss = orig_funct
