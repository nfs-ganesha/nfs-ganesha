from nfs4.nfs4_const import *
from nfs4.nfs4_type import nfs_client_id4, clientaddr4, cb_client4
from environment import check, checklist
import os
import threading
import time

_lock = threading.Lock()

class _handle_error(object):
    def __init__(self, c, res, ops):
        self.c = c
        self.res = res
        self.ops = ops
        
    def run(self):
        if self.res.status != NFS4_OK:
            time.sleep(2)
            _lock.acquire()
            try:
                self.c.compound(ops)
            except Exception, e:
                print "CALLBACK error in _recall:", e
                pass
            _lock.release()
            
def _recall(c, op, cbid):
    # Note this will be run in the cb_server thread, not the tester thread
    ops = [c.putfh_op(op.opcbrecall.fh),
           c.delegreturn_op(op.opcbrecall.stateid)]
    _lock.acquire()
    try:
        res = c.compound(ops)
    except Exception, e:
        print "CALLBACK error in _recall:", e
        res = None
    _lock.release()
    if res.status != NFS4_OK:
        t_error = _handle_error(c, res, ops)
        t = threading.Thread(target=t_error.run)
        t.setDaemon(1)
        t.start()
    return res

def _cause_recall(t, env):
    c = env.c1
    sleeptime = 5
    while 1:
        # need lock around this to prevent _recall from
        # calling c.unpacker.reset while open is still unpacking
        _lock.acquire()
        res = c.open_file('newowner', c.homedir + [t.code],
                          access=OPEN4_SHARE_ACCESS_WRITE,
                          deny=OPEN4_SHARE_DENY_NONE)
        _lock.release()
        if res.status == NFS4_OK: break
        checklist(res, [NFS4_OK, NFS4ERR_DELAY], "Open which causes recall")
        env.sleep(sleeptime, 'Got NFS4ERR_DELAY on open')
        sleeptime += 5
        if sleeptime > 20:
            sleeptime = 20
    return c.confirm('newowner', res)

def _verify_cb_occurred(t, c, count):
    newcount = c.cb_server.opcounts[OP_CB_RECALL]
    if newcount <= count:
        t.fail("Recall for callback_ident=%i never occurred" % c.cbid)
    res = c.cb_server.get_recall_res(c.cbid)
    if res is not None:
        check(res, msg="DELEGRETURN")

def _get_deleg(t, c, path, funct=None, response=NFS4_OK, write=False,
               deny=OPEN4_SHARE_DENY_NONE):
    time.sleep(0.5) # Give server time to check callback path
    if write:
        access = OPEN4_SHARE_ACCESS_WRITE
        deleg = OPEN_DELEGATE_WRITE
        name = "write delegation"
    else:
        access = OPEN4_SHARE_ACCESS_READ
        deleg = OPEN_DELEGATE_READ
        name = "read delegation"
    # Create the file
    res = c.create_file(t.code, path, access=access, deny=deny, 
                        set_recall=True,
                        recall_funct=funct, recall_return=response)
    check(res)
    fh, stateid = c.confirm(t.code, res)
    # Check for delegation
    deleg_info = res.resarray[-2].arm.arm.delegation
    if deleg_info.delegation_type == deleg:
        return deleg_info, fh, stateid
    
    # Try opening the file again
    res = c.open_file(t.code, path, access=access, deny=deny, 
                      set_recall=True,
                      recall_funct=funct, recall_return=response)
    check(res)
    fh, stateid = c.confirm(t.code, res)
    deleg_info = res.resarray[-2].arm.arm.delegation
    if deleg_info.delegation_type != deleg:
        t.pass_warn("Could not get %s" % name)
    return deleg_info, fh, stateid

def _read_deleg(t, env, funct=None, response=NFS4_OK):
    """Get and recall a read delegation

    The cb_server will first call funct, then respond with response
    """
    c = env.c1
    count = c.cb_server.opcounts[OP_CB_RECALL]
    c.init_connection('pynfs%i_%s' % (os.getpid(), t.code), cb_ident=0)
    _get_deleg(t, c, c.homedir + [t.code], funct, response)
    _cause_recall(t, env)
    _verify_cb_occurred(t, c, count)

def _write_deleg(t, env, funct=None, response=NFS4_OK):
    """Get and recall a read delegation

    The cb_server will first call funct, then respond with response
    """
    c = env.c1
    count = c.cb_server.opcounts[OP_CB_RECALL]
    c.init_connection('pynfs%i_%s' % (os.getpid(), t.code), cb_ident=0)
    _get_deleg(t, c, c.homedir + [t.code], funct, response, write=True)
    _cause_recall(t, env)
    _verify_cb_occurred(t, c, count)

####################################################

def testReadDeleg1(t, env):
    """DELEGATION test

    Get read delegation, then have conflicting open recall it.
    Respond properly and send DELEGRETURN.

    FLAGS: delegations
    CODE: DELEG1
    """
    _read_deleg(t, env, _recall)

def testReadDeleg2(t, env):
    """DELEGATION test

    Get read delegation, then have conflicting open recall it.
    Have callback server return OK, but client never sends DELEGRETURN.

    FLAGS: delegations
    CODE: DELEG2
    """
    _read_deleg(t, env)

def testReadDeleg3a(t, env):
    """DELEGATION test

    Get read delegation, then have conflicting open recall it.
    Have callback server return error.

    FLAGS: delegations
    CODE: DELEG3a
    """
    _read_deleg(t, env, None, NFS4ERR_RESOURCE)

def testReadDeleg3b(t, env):
    """DELEGATION test

    Get read delegation, then have conflicting open recall it.
    Have callback server return error.

    FLAGS: delegations
    CODE: DELEG3b
    """
    _read_deleg(t, env, None, NFS4ERR_SERVERFAULT)

def testReadDeleg3c(t, env):
    """DELEGATION test

    Get read delegation, then have conflicting open recall it.
    Have callback server return error.

    FLAGS: delegations
    CODE: DELEG3c
    """
    _read_deleg(t, env, None, NFS4ERR_BADXDR)

def testReadDeleg3d(t, env):
    """DELEGATION test

    Get read delegation, then have conflicting open recall it.
    Have callback server return error.

    FLAGS: delegations
    CODE: DELEG3d
    """
    _read_deleg(t, env, None, NFS4ERR_BAD_STATEID)

def testReadDeleg3e(t, env):
    """DELEGATION test

    Get read delegation, then have conflicting open recall it.
    Have callback server return error.

    FLAGS: delegations
    CODE: DELEG3e
    """
    _read_deleg(t, env, None, NFS4ERR_BADHANDLE)

def testCloseDeleg(t, env, funct=_recall, response=NFS4_OK):
    """Get a read delegation, close the file, then recall

    Get read delegation, close the file, then have conflicting open recall it.
    Respond properly and send DELEGRETURN.

    (The cb_server will first call funct, then respond with response)

    FLAGS: delegations
    CODE: DELEG4
    """
    c = env.c1
    count = c.cb_server.opcounts[OP_CB_RECALL]
    c.init_connection('pynfs%i_%s' % (os.getpid(), t.code), cb_ident=0)
    
    deleg_info, fh, stateid = _get_deleg(t, c, c.homedir + [t.code],
                                         funct, response)
    res = c.close_file(t.code, fh, stateid)
    check(res, msg="Closing a file with a delegation held")
    _cause_recall(t, env)
    _verify_cb_occurred(t, c, count)

def testManyReaddeleg(t, env, funct=_recall, response=NFS4_OK):
    """Width test - recall many read delegations at once

    Get many read delegation, then have conflicting open recall them.
    Respond properly and send DELEGRETURN for each.

    (The cb_server will first call funct, then respond with response)

    FLAGS: delegations
    CODE: DELEG5
    """
    # XXX needs to use _get_deleg
    count = 100 # Number of read delegations to grab
    c = env.c1
    c.init_connection('pynfs%i_%s' % (os.getpid(), t.code), cb_ident=0)
    cbids = []
    fh, stateid = c.create_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ,
                                   deny=OPEN4_SHARE_DENY_NONE)
    for i in range(count):
        c.init_connection('pynfs%i_%s_%i' % (os.getpid(), t.code, i), cb_ident=0)
        fh, stateid = c.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ,
                                     deny=OPEN4_SHARE_DENY_NONE)
            
        # Get a read delegation
        res = c.open_file(t.code, access=OPEN4_SHARE_ACCESS_READ,
                          set_recall=True,
                          recall_funct=funct, recall_return=response)
        fh, stateid = c.confirm(t.code, res)
        deleg_info = res.resarray[-2].arm.arm.delegation
        if deleg_info.delegation_type == OPEN_DELEGATE_READ:
            cbids.append(c.cbid)
    if not cbids:
        t.pass_warn("Could not get any read delegations")
    print "Got %i out of %i read delegations" % (len(cbids), count)
    # Cause them to be recalled
    fh2, stateid2 = _cause_recall(t, env)
    miss_count = 0
    for id in cbids:
        res = c.cb_server.get_recall_res(id)
        if res is None:
            miss_count += 1
        else:
            check(res, msg="DELEGRETURN for cb_id=%i" % id)
    if miss_count:
        t.pass_warn("Recall never occurred for %i of %i read delegations" %
                    (miss_count, len(cbids)))

def testRenew(t, env, funct=None, response=NFS4_OK):
    """Get and recall a read delegation

    The cb_server will first call funct, then respond with response
    FLAGS: delegations
    CODE: DELEG6
    """
    c = env.c1
    c.init_connection('pynfs%i_%s' % (os.getpid(), t.code), cb_ident=0)
    lease = c.getLeaseTime()
    _get_deleg(t, c, c.homedir + [t.code], funct, response)
    try:
        c.cb_command(0) # Shut off callback server
        noticed = False
        for i in range(4):
            res = c.open_file('newowner', c.homedir + [t.code],
                              access=OPEN4_SHARE_ACCESS_WRITE)
            env.sleep(lease // 2 + 5, "Waiting to send RENEW")
            res = c.compound([c.renew_op(c.clientid)])
            checklist(res, [NFS4_OK, NFS4ERR_CB_PATH_DOWN], "RENEW")
            if res.status != NFS4_OK:
                noticed = True
                break
    finally:
        c.cb_command(1) # Turn on callback server
    if not noticed:
        t.fail("RENEWs should not have all returned OK")

def testIgnoreDeleg(t, env, funct=_recall, response=NFS4_OK):
    """Get a read delegation, and ignore it, then recall

    Get read delegation, close the file, then do more open/closes/locks.
    Finally have conflicting open recall it.
    Respond properly and send DELEGRETURN.

    (The cb_server will first call funct, then respond with response)

    FLAGS: delegations
    CODE: DELEG7
    """
    c = env.c1
    count = c.cb_server.opcounts[OP_CB_RECALL]
    c.init_connection('pynfs%i_%s' % (os.getpid(), t.code), cb_ident=0)
    path = c.homedir + [t.code]
    deleg_info, fh, stateid = _get_deleg(t, c, path, funct, response)

    # Close the file
    res = c.close_file(t.code, fh, stateid)
    check(res, msg="Closing a file with a delegation held")

    # Play with file some more
    fh, stateid = c.open_confirm("NaughtyOwner", path,
                                 access=OPEN4_SHARE_ACCESS_READ,
                                 deny=OPEN4_SHARE_DENY_NONE)
    res = c.lock_file("NaughtyOwner", fh, stateid, type=READ_LT)
    check(res)
    fh2, stateid2 = c.open_confirm(t.code, access=OPEN4_SHARE_ACCESS_READ,
                                   deny=OPEN4_SHARE_DENY_NONE)
    res = c.unlock_file(1, fh, res.lockid)
    check(res)
    
    # Cause it to be recalled
    _cause_recall(t, env)
    _verify_cb_occurred(t, c, count)


def testDelegShare(t, env, funct=_recall, response=NFS4_OK):
    """Get a read delegation with share_deny_write, then try to recall

    Get read delegation with share_deny_write, then see if a conflicting
    write open recalls the delegation (it shouldn't).

    FLAGS: delegations
    CODE: DELEG8
    """
    c = env.c1
    c.init_connection('pynfs%i_%s' % (os.getpid(), t.code), cb_ident=0)
    _get_deleg(t, c, c.homedir + [t.code], funct, response,
               deny=OPEN4_SHARE_DENY_WRITE)

    # Try conflicting write open
    sleeptime = 5
    while 1:
        # need lock around this to prevent _recall from
        # calling c.unpacker.reset while open is still unpacking
        _lock.acquire()
        res = c.open_file('newowner', c.homedir + [t.code],
                          access=OPEN4_SHARE_ACCESS_WRITE)
        _lock.release()
        if res.status in  [NFS4_OK, NFS4ERR_SHARE_DENIED]: break
        checklist(res, [NFS4_OK, NFS4ERR_DELAY, NFS4ERR_SHARE_DENIED],
                  "Open which causes recall")
        env.sleep(sleeptime, 'Got NFS4ERR_DELAY on open')
        sleeptime += 5
        if sleeptime > 20:
            sleeptime = 20
    check(res, NFS4ERR_SHARE_DENIED)

    # Verify cb did NOT occur
    if funct is not None:
        res = c.cb_server.get_recall_res(c.cbid)
        if res is not None:
            t.fail("Recall for callback_ident=%i occurred" % c.cbid)

def _set_clientid(c, id, server):
    client_id = nfs_client_id4(c.verifier, id)
    r_addr = c.ipaddress + server.dotport
    cb_location = clientaddr4('tcp', r_addr)
    callback = cb_client4(server.prog, cb_location)
    return c.setclientid_op(client_id, callback, 1)

def testChangeDeleg(t, env, funct=_recall):
    """Get a read delegation, change to a different callback server, then
    recall the delegation

    FLAGS: delegations
    CODE: DELEG9
    """
    from nfs4.nfs4lib import CBServer
    c = env.c1
    id = 'pynfs%i_%s' % (os.getpid(), t.code)
    c.init_connection(id, cb_ident=0)
    deleg_info, fh, stateid = _get_deleg(t, c, c.homedir + [t.code], funct, NFS4_OK)
    # Create new callback server
    new_server = CBServer(c)
    cb_thread = threading.Thread(target=new_server.run)
    cb_thread.setDaemon(1)
    cb_thread.start()
    env.sleep(3)
    # Switch to using new server
    res = c.compound([_set_clientid(c, id, new_server)])
    check(res, msg="Switch to new callback server")
    c.clientid = res.resarray[0].arm.arm.clientid
    confirm = res.resarray[0].arm.arm.setclientid_confirm
    confirmop = c.setclientid_confirm_op(c.clientid, confirm)
    res = c.compound([confirmop])
    checklist(res, [NFS4_OK, NFS4ERR_RESOURCE])
    if res.status == NFS4ERR_RESOURCE:
        # ibm workaround
        res = c.compound([confirmop])
        check(res)
    count = new_server.opcounts[OP_CB_RECALL]
    fh2, stateid2 = _cause_recall(t, env)
    _verify_cb_occurred(t, c, count)
    ops = c.use_obj(fh) + [c.delegreturn_op(deleg_info.read.stateid)]
    res = c.compound(ops)
    check(res)


   
    
###################################

def testWriteDeleg1(t, env):
    """DELEGATION test

    Get write delegation, then have conflicting open recall it.
    Respond properly and send DELEGRETURN.

    FLAGS: delegations
    CODE: DELEG11
    """
    _write_deleg(t, env, _recall)

def testWriteDeleg2(t, env):
    """DELEGATION test

    Get write delegation, then have conflicting open recall it.
    Have callback server return OK, but client never sends DELEGRETURN.

    FLAGS: delegations
    CODE: DELEG12
    """
    _write_deleg(t, env)

def testWriteDeleg3a(t, env):
    """DELEGATION test

    Get write delegation, then have conflicting open recall it.
    Have callback server return error.

    FLAGS: delegations
    CODE: DELEG13a
    """
    _write_deleg(t, env, None, NFS4ERR_RESOURCE)

def testWriteDeleg3b(t, env):
    """DELEGATION test

    Get write delegation, then have conflicting open recall it.
    Have callback server return error.

    FLAGS: delegations
    CODE: DELEG13b
    """
    _write_deleg(t, env, None, NFS4ERR_SERVERFAULT)

def testWriteDeleg3c(t, env):
    """DELEGATION test

    Get write delegation, then have conflicting open recall it.
    Have callback server return error.

    FLAGS: delegations
    CODE: DELEG13c
    """
    _write_deleg(t, env, None, NFS4ERR_BADXDR)

def testWriteDeleg3d(t, env):
    """DELEGATION test

    Get write delegation, then have conflicting open recall it.
    Have callback server return error.

    FLAGS: delegations
    CODE: DELEG13d
    """
    _write_deleg(t, env, None, NFS4ERR_BAD_STATEID)

def testWriteDeleg3e(t, env):
    """DELEGATION test

    Get write delegation, then have conflicting open recall it.
    Have callback server return error.

    FLAGS: delegations
    CODE: DELEG13e
    """
    _write_deleg(t, env, None, NFS4ERR_BADHANDLE)

def testClaimCur(t, env):
    """DELEGATION test

    Get read delegation, then have it recalled.  In the process
    of returning, send some OPENs with CLAIM_DELEGATE_CUR

    FLAGS: delegations
    CODE: DELEG14
    """
    c = env.c1
    c.init_connection('pynfs%i_%s' % (os.getpid(), t.code), cb_ident=0)
    
    deleg_info, fh, stateid = _get_deleg(t, c, c.homedir + [t.code],
                                         None, NFS4_OK)
    
    # Cause it to be recalled, and wait for cb_recall to finish
    # FRED - this is problematic if server doesn't reply until
    # it gets the DELEGRETURN
    res = c.open_file('newowner', c.homedir + [t.code],
                      access=OPEN4_SHARE_ACCESS_WRITE,
                      deny=OPEN4_SHARE_DENY_NONE)
    checklist(res, [NFS4_OK, NFS4ERR_DELAY], "Open which causes recall")
    env.sleep(2, "Waiting for recall")

    # Now send some opens
    path = c.homedir + [t.code]
    res = c.open_file('owner1', path, access=OPEN4_SHARE_ACCESS_READ,
                            claim_type=CLAIM_DELEGATE_CUR,
                            deleg_stateid=deleg_info.read.stateid)
    check(res)
    ops = c.use_obj(path) + [c.delegreturn_op(deleg_info.read.stateid)]
    res = c.compound(ops)
    check(res)
                            
def testRemove(t, env):
    """DELEGATION test

    Get read delegation, then ensure REMOVE recalls it.
    Respond properly and send DELEGRETURN.

    FLAGS: delegations
    CODE: DELEG15
    """
    c = env.c1
    count = c.cb_server.opcounts[OP_CB_RECALL]
    c.init_connection('pynfs%i_%s' % (os.getpid(), t.code), cb_ident=0)
    _get_deleg(t, c, c.homedir + [t.code], _recall, NFS4_OK)
    sleeptime = 5
    while 1:
        ops = c.use_obj(c.homedir) + [c.remove_op(t.code)]
        _lock.acquire()
        res = c.compound(ops)
        _lock.release()
        if res.status == NFS4_OK: break
        checklist(res, [NFS4_OK, NFS4ERR_DELAY], "Remove which causes recall")
        env.sleep(sleeptime, 'Got NFS4ERR_DELAY on remove')
        sleeptime += 5
        if sleeptime > 20:
            sleeptime = 20
    _verify_cb_occurred(t, c, count)

    
