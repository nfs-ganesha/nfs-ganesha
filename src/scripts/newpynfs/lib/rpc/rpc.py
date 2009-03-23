# rpc.py - based on RFC 1831
#
# Requires python 2.3
# 
# Written by Fred Isaman <iisaman@citi.umich.edu>
# Copyright (C) 2004 University of Michigan, Center for 
#                    Information Technology Integration
#

import struct
import xdrlib
import socket
import select
import threading

from rpc_const import *
from rpc_type import *
import rpc_pack

# Import security flavors and store valid ones
from rpcsec.sec_auth_none import SecAuthNone
from rpcsec.sec_auth_sys import SecAuthSys
supported = {'none' : SecAuthNone,
             'sys'  : SecAuthSys }
try:
    from rpcsec.sec_auth_gss import SecAuthGss
    supported['gss'] = SecAuthGss
except ImportError:
    pass

RPCVERSION = 2

if hasattr(select, "poll"):
    _stdmask   = select.POLLERR | select.POLLHUP | select.POLLNVAL
    _readmask  = select.POLLIN  | _stdmask
    _writemask = select.POLLOUT | _stdmask
    _bothmask  = select.POLLOUT | select.POLLIN | _stdmask
else:
    _readmask = 1
    _writemask = 2
    _bothmask = 3
    select.POLLIN = 1
    select.POLLOUT = 2
    select.POLLERR = 4
    select.POLLHUP = select.POLLNVAL = 0
    
    class my_poll(object):
        """Emulate select.poll using select.select"""
        def __init__(self):
            self._in = []
            self._out = []
            self._err = []

        def register(self, fd, eventmask=_bothmask):
            if type(fd) != int:
                fd = fd.fileno()
            self.unregister(fd)
            if eventmask & _readmask:
                self._in.append(fd)
            if eventmask & _writemask:
                self._out.append(fd)
            self._err.append(fd)

        def unregister(self, fd):
            if type(fd) != int:
                fd = fd.fileno()
            # Remove
            if fd in self._in: self._in.remove(fd)
            if fd in self._out: self._out.remove(fd)
            if fd in self._err: self._err.remove(fd)

        def poll(self, timeout=None):
            # STUB - deal with timeout
            read, write, err = select.select(self._in, self._out, self._err)
            list = []
            for fd in read:
                mask = select.POLLIN
                if fd in write:
                    mask |= select.POLLOUT
                    write.remove(fd)
                if fd in err:
                    mask |= select.POLLERR
                    err.remove(fd)
                list.append((fd, mask))
            for fd in write:
                mask = select.POLLOUT
                if fd in err:
                    mask |= select.POLLERR
                    err.remove(fd)
                list.append((fd, mask))
            for fd in err:
                mask = select.POLLOUT
                list.append((fd, mask))
            return list
    select.poll = my_poll
        
class RPCError(Exception):
    pass

class RPCAcceptError(RPCError):
    def __init__(self, a):
        self.verf = a.verf
        a = a.reply_data
        self.stat = a.stat
        if self.stat == PROG_MISMATCH:
            self.low = a.mismatch_info.low
            self.high = a.mismatch_info.high

    def __str__(self):
        if self.stat == PROG_MISMATCH:
            return "RPCError: MSG_ACCEPTED: %s [%i,%i]" % \
                   (accept_stat.get(self.stat, self.stat),
                    self.low, self.high)
        else:
            return "RPCError: MSG_ACCEPTED: %s" % \
                   accept_stat.get(self.stat, self.stat)

class RPCDeniedError(RPCError):
    def __init__(self, r):
        self.stat = r.stat
        if self.stat == RPC_MISMATCH:
            self.low = r.mismatch_info.low
            self.high = r.mismatch_info.high
        elif self.stat == AUTH_ERROR:
            self.astat = r.astat

    def __str__(self):
        if self.stat == RPC_MISMATCH:
            return "RPCError: MSG_DENIED: RPC_MISMATCH [%i,%i]" % \
                   (self.low, self.high)
        else:
            return "RPCError: MSG_DENIED: AUTH_ERROR: %s" % \
                   auth_stat.get(self.astat, self.astat)

###################################################

# Add some record marking functions to sockets
# FRED - is there a cleaner (class based) way to do this?

def _recv_all(self, n):
    """Receive n bytes, or raise an error"""
    data = ""
    while n > 0:
        newdata = self.recv(n)
        count = len(newdata)
        if not count:
            raise socket.error("Connection closed")
        data += newdata
        n -= count
    return data

def _recv_record(self):
    """Receive data sent using record marking standard"""
    last = False
    data = ""
    while not last:
        rec_mark = self.recv_all(4)
        count = struct.unpack('>L', rec_mark)[0]
        last = count & 0x80000000L
        if last:
            count &= 0x7fffffffL
        data += self.recv_all(count)
    return data

def _send_record(self, data, chunksize=2048):
    """Send data using record marking standard"""
    dlen = len(data)
    i = last = 0
    while not last:
        chunk = data[i:i+chunksize]
        i += chunksize
        if i >= dlen:
            last = 0x80000000L
        mark = struct.pack('>L', last | len(chunk))
        self.sendall(mark + chunk)

socket._socketobject.recv_all = _recv_all
socket._socketobject.recv_record = _recv_record
socket._socketobject.send_record = _send_record

#################################################

class RPCClient(object):
    def __init__(self, host='localhost', port=51423,
                 program=None, version=None, sec_list=None, timeout=15.0):
        self.debug = 0
        t = threading.currentThread()
        self.lock = threading.Lock()
        self.remotehost = host
        self.remoteport = port
        self.timeout = timeout
        self._socket = {}
        self.getsocket() # init socket, is this needed here?
        self.ipaddress = self.socket.getsockname()[0]
        self._rpcpacker = {t : rpc_pack.RPCPacker()}
        self._rpcunpacker = {t : rpc_pack.RPCUnpacker('')}
        self.default_prog = program
        self.default_vers = version
        self.xid = 0L
        self._xidlist = {}
        if sec_list is None:
            sec_list = [SecAuthNone()]
        self.sec_list = sec_list
        self._init_security(self.sec_list) # Note this can make calls
        self.security = sec_list[0]

    def _init_security(self, list):
        # Each element of list must have functions:
        # initialize, secure_data, make_cred, make_verf
        for flavor in list:
            self.security = flavor
            flavor.initialize(self)

    def getsocket(self):
        t = threading.currentThread()
        self.lock.acquire()
        if t in self._socket:
            out = self._socket[t]
        else:
            out = self._socket[t] = socket.socket(socket.AF_INET,
                                                  socket.SOCK_STREAM)
            # out.bind
            out.connect((self.remotehost, self.remoteport))
            out.settimeout(self.timeout)
        self.lock.release()
        return out

    socket = property(getsocket)

    def getrpcpacker(self):
        t = threading.currentThread()
        self.lock.acquire()
        if t in self._rpcpacker:
            out = self._rpcpacker[t]
        else:
            out = self._rpcpacker[t] = rpc_pack.RPCPacker()
            self._rpcunpacker[t] = rpc_pack.RPCUnpacker('')
        self.lock.release()
        return out

    def getrpcunpacker(self):
        t = threading.currentThread()
        self.lock.acquire()
        if t in self._rpcunpacker:
            out = self._rpcunpacker[t]
        else:
            self._rpcpacker[t] = gss_pack.GSSPacker()
            out = self._rpcunpacker[t] = gss_pack.GSSUnpacker('')
        self.lock.release()
        return out

    class XidCache(object):
        def __init__(self, header, data, seq=None, proc=1):
            self.header = header
            self.data = data
            self.seq = seq
            self.rhead = None
            self.rdata = None
            self.proc = proc

        def __repr__(self):
            return "%s\n%s" % (self.header, self.data)

    def add_outstanding_xids(self, xid, header, data, seq, proc):
        t = threading.currentThread()
        self.lock.acquire()
        if t in self._xidlist:
            if xid in self._xidlist[t]: raise
            self._xidlist[t][xid] = self.XidCache(header, data, seq, proc)
        else:
            self._xidlist[t] = {xid : self.XidCache(header, data, seq, proc)}
        self.lock.release()

    def get_outstanding_xids(self):
        t = threading.currentThread()
        self.lock.acquire()
        out = self._xidlist[t]
        self.lock.release()
        return out

    def reconnect(self):
        t = threading.currentThread()
        self.lock.acquire()
        self._socket[t].close()
        out = self._socket[t] = socket.socket(socket.AF_INET,
                                              socket.SOCK_STREAM)
        # out.bind
        out.connect((self.remotehost, self.remoteport))
        out.settimeout(self.timeout)
        self.lock.release()
        return out
        
    def send(self, procedure, data='', program=None, version=None):
        """Send an RPC call to the server

        Takes as input packed arguments
        """
        if program is None: program = self.default_prog
        if version is None: version = self.default_vers
        if program is None or version is None:
            raise RPCError("Bad program/version: %s/%s" % (program, version))

        xid = self.get_new_xid()
        header, seq = self.get_call_header(xid, program, version, procedure)
        data = self.security.secure_data(data, seq)
        try:
            if self.debug: print "send %i" % xid
            self.socket.send_record(header + data)
        except socket.timeout:
            raise
        except socket.error, e:
            print "Got error:", e
            if self.debug: print "resend", xid
            try:
                self.reconnect().send_record(header + data)
            except socket.error:
                self.reconnect()
                raise
        self.add_outstanding_xids(xid, header, data, seq, procedure)
        return xid

    def listen(self, xid):
        # Exists (per thread) list of outstanding xid/seq pairs
        # If xid not on list, return error.
        # Listen unitl get reply with given xid.  Cache others received
        # on list.  Return error if get one not on list.
        if self.debug: print "listen", xid
        list = self.get_outstanding_xids()
        if xid not in list:
            raise
        done = False
        rdata = list[xid].rdata
        if rdata is not None:
            rhead = list[xid].rhead
            done = True
        while not done:
            try:
                reply = self.socket.recv_record()
            except socket.timeout:
                raise
            except socket.error, e:
                print "Got error:", e
                if self.debug: print "relisten", xid
                try:
                    s = self.reconnect()
                    s.send_record(list[xid].header + list[xid].data)
                    reply = s.recv_record()
                except socket.error:
                    self.reconnect()
                    raise
            p = self.getrpcunpacker()
            p.reset(reply)
            rhead = p.unpack_rpc_msg()
            rxid = rhead.xid
            if rxid not in list:
                raise RPCError("Got reply xid %i, expected %i" % \
                               (rxid, xid))
            rdata = reply[p.get_position():]
            try:
                rdata = self.security.unsecure_data(rdata, list[rxid].seq)
            except:
                if 0:
                    # need for servers that don't add gss checksum to errors
                    pass
                else:
                    raise
            if rxid != xid:
                list[rxid].cached = (rhead, rdata)
            else:
                done = True
        del list[xid]
        self.check_reply(rhead)
        return rdata

    def call(self, procedure, data='', program=None, version=None):
        """Make an RPC call to the server

        Takes as input packed arguments
        Returns packed results
        """
        xid = self.send(procedure, data, program, version)
        return self.listen(xid)

    def get_new_xid(self): # Thread safe
        self.lock.acquire()
        self.xid += 1
        if self.xid >= 0x100000000:
            self.xid = 0
        out = self.xid
        self.lock.release()
        return out

    # Because some security flavors use partial packing info to determine
    # verf, can't call packer.pack_rpc_msg.
    # Also, need to return seq from make_cred
    def get_call_header(self, xid, prog, vers, proc): # THREAD SAFE
        p = self.getrpcpacker()
        p.reset()
        cred, seq = self.security.make_cred()
        p.pack_uint(xid)
        p.pack_enum(CALL)
	p.pack_uint(RPCVERSION)
	p.pack_uint(prog)
	p.pack_uint(vers)
	p.pack_uint(proc)
	p.pack_opaque_auth(cred)
        verf = self.security.make_verf(p.get_buffer())
        p.pack_opaque_auth(verf)
        return p.get_buffer(), seq

    def check_reply(self, msg): # THREAD SAFE
        """Looks at rpc_msg reply and raises error if necessary

        xid has already been checked
        """
        msg = msg.body
        if msg.mtype != REPLY:
            raise RPCError("Msg was not a REPLY")
        msg = msg.rbody
        if msg.stat == MSG_DENIED:
            # Do more here
            raise RPCDeniedError(msg.rreply)
        elif msg.areply.reply_data.stat != SUCCESS:
            raise RPCAcceptError(msg.areply)
            
###################################################

class Server(object):
    # STUB - just prints out what it receives, sends nothing back
    def __init__(self, host='', port=51423, name="SERVER"):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind((host, port))
        self.port = self.s.getsockname()[1]
        self.s.setblocking(0)
        # Set up poll object
        self.p = select.poll()
        self.p.register(self.s, _readmask)
        self.name = name

    def run(self, debug=0):
        while 1:
            if debug: print "%s: Calling poll" % self.name
            res = self.p.poll()
            if debug: print "%s: %s" % (self.name, res)
            for fd, event in res:
                if debug:
                    print "%s: Handling fd=%i, event=%x" % \
                          (self.name, fd, event)
                if event & select.POLLHUP:
                    self.event_hup(fd)
                elif event & select.POLLNVAL:
                    if debug: print "%s: POLLNVAL for fd=%i" % (self.name, fd)
                    self.p.unregister(fd)
                elif event & ~(select.POLLIN | select.POLLOUT):
                    print "%s: ERROR: event %i for fd %i" % \
                          (self.name, event, fd)
                    self.event_error(fd)
                else:
                    if event & select.POLLIN:
                        if fd == self.s.fileno():
                            self.event_connect(fd)
                        else:
                            data = self.sockets[fd].recv(4096)
                            if data:
                                self.event_read(fd, data)
                            else:
                                self.event_close(fd)
                    if event & select.POLLOUT:
                        self.event_write(fd)

class RPCServer(Server):
    def __init__(self, prog=10, vers=4, host='', port=51423):
        Server.__init__(self, host, port)
        self.rpcpacker =  rpc_pack.RPCPacker()
        self.rpcunpacker = rpc_pack.RPCUnpacker('')
        self.prog = prog
        self.vers = vers # FRED - this could be more general
        self.security = SecAuthNone()
        self.readbufs = {}
        self.writebufs = {}
        self.packetbufs = {} # store packets read until have a whole record
        self.recordbufs = {} # write buffer for outgoing records
        self.sockets = {}
        self.s.listen(5)

    def handle_0(self, data):
        if data != '':
            return GARBAGE_ARGS, ''
        else:
            return SUCCESS, ''
    
    def event_connect(self, fd, debug=0):
        csock, caddr = self.s.accept()
        csock.setblocking(0)
        if debug:
            print "SERVER: got connection from %s, " \
                  "assigned to fd=%i" % \
                  (csock.getpeername(), csock.fileno())
        self.p.register(csock, _readmask)
        cfd = csock.fileno()
        self.readbufs[cfd] = ''
        self.writebufs[cfd] = ''
        self.packetbufs[cfd] = []
        self.recordbufs[cfd] = []
        self.sockets[cfd] = csock
        
    def event_read(self, fd, data, debug=0):
        """Reads incoming record marked packets

        Also responds to command codes sent as encoded integers
        """
        if debug: print "SERVER: In read event for %i" % fd
        self.readbufs[fd] += data
        str = self.readbufs[fd]
        if len(str) >= 4:
            packetlen = struct.unpack('>L', str[0:4])[0]
            last = 0x80000000L & packetlen
            packetlen &= 0x7fffffffL
            if len(str) >= 4 + packetlen:
                self.packetbufs[fd].append(str[4:4 + packetlen])
                self.readbufs[fd] = str[4 + packetlen:]
                if last:
                    if debug: print "SERVER: Received record from %i" % fd
                    recv_data = ''.join(self.packetbufs[fd])
                    self.packetbufs[fd] = []
                    if len(recv_data) == 4:
                        reply = self.event_command(fd, struct.unpack('>L', recv_data)[0])
                    else:
                        # All handle_* functions are called in compute_reply
                        reply = self.compute_reply(recv_data)
                    if reply is not None:
                        self.recordbufs[fd].append(reply)
                        self.p.register(fd, _bothmask)

    def event_write(self, fd, chunksize=2048, debug=0):
        if debug: print "SERVER: In write event for %i" % fd
        if self.writebufs[fd]:
            if debug: print "  writing from writebuf"
            count = self.sockets[fd].send(self.writebufs[fd])
            self.writebufs[fd] = self.writebufs[fd][count:]
            # check if done?
        elif self.recordbufs[fd]:
            if debug: print "  writing from recordbuf"
            data = self.recordbufs[fd][0]
            chunk = data[:chunksize]
            if len(data) > chunksize:
                last = 0
                self.recordbufs[fd][0] = data[chunksize:]
            else:
                last = 0x80000000L
                del self.recordbufs[fd][0]
            mark = struct.pack('>L', last | len(chunk))
            self.writebufs[fd] = (mark + chunk)
            # Duplicated code
            count = self.sockets[fd].send(self.writebufs[fd])
            self.writebufs[fd] = self.writebufs[fd][count:]
        else:
            if debug: print "  done writing"
            self.p.register(fd, _readmask)

    def event_command(self, cfd, comm, debug=0):
        if debug:
            print "SERVER: command = %i, cfd = %i" % (comm, cfd)
        if comm == 0: # Turn off server
            self.compute_reply = lambda x: None
            return '\0'*4
        elif comm == 1: # Turn server on
            self.compute_reply = self.__compute_reply_orig
            return '\0'*4

    def event_close(self, fd, debug=0):
        if debug:
            print "SERVER: closing %i" % fd
        self.event_error(fd)

    def event_error(self, fd):
        self.p.unregister(fd)
        self.sockets[fd].close()
        del self.readbufs[fd]
        del self.writebufs[fd]
        del self.packetbufs[fd]
        del self.recordbufs[fd]
        del self.sockets[fd]
        
    event_hup = event_error

    def compute_reply(self, recv_data):
        # Decode RPC specific info
        self.rpcunpacker.reset(recv_data)
        try:
            recv_msg = self.rpcunpacker.unpack_rpc_msg()
        except xdrlib.Error, e:
            print "XDRError", e
            return
        if recv_msg.body.mtype != CALL:
            print "Received a REPLY, expected a CALL"
            return
        # Check for reasons to deny the call
        call = recv_msg.body.cbody
        #print call
        reply_stat = MSG_ACCEPTED
        areply = rreply = None
        proc_response = ''
        class C(object):
            pass
        data = C()
        if call.rpcvers != RPCVERSION:
            data.low = data.high = RPCVERSION
            rreply = rejected_reply(RPC_MISMATCH, mismatch_info=data)
            reply_stat = MSG_DENIED
        elif 0: # STUB
            # Auth checking
            rreply = rejected_reply(AUTH_ERROR, astat=AUTH_FAILED)
            reply_stat = MSG_DENIED
        # At this point recv_msg has been accepted
        # Check for reasons to fail before calling handle_*
        elif self.prog != call.prog:
            verf = self.security.make_reply_verf('')
            data.stat = PROG_UNAVAIL
            areply = accepted_reply(verf, data)
        elif self.vers != call.vers:
            verf = self.security.make_reply_verf('')
            data.stat = PROG_MISMATCH
            data.mismatch_info = C()
            data.mismatch_info.low = data.mismatch_info.high = self.vers
            areply = accepted_reply(verf, data)
        elif not hasattr(self, "handle_%i" % call.proc):
            verf = self.security.make_reply_verf('')
            data.stat = PROC_UNAVAIL
            areply = accepted_reply(verf, data)
        # Call appropriate handle_*
        else:
            verf = self.security.make_reply_verf('')
            meth_data = recv_data[self.rpcunpacker.get_position():]
            method = getattr(self, "handle_%i" % call.proc)
            a_stat, proc_response = method(meth_data)
            data.stat = a_stat
            data.results = ''
            areply = accepted_reply(verf, data)
        # Build reply
        body = reply_body(reply_stat, areply, rreply)
        data = C()
        data.mtype = REPLY
        data.rbody = body
        msg = rpc_msg(recv_msg.xid, data)
        self.rpcpacker.reset()
        self.rpcpacker.pack_rpc_msg(msg)
        return self.rpcpacker.get_buffer() + proc_response

    __compute_reply_orig = compute_reply
