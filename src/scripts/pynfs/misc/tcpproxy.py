#!/usr/bin/env python2

import socket
import rpc
import select


udpsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, 0)
udpsock.connect(("citi-1", 2049))

tcpsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
tcpsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
tcpsock.bind(("", 2049))
tcpsock.listen(1)

(tcpsock, addr) = tcpsock.accept()

print 'Connected by', addr
while 1:
    (r, w, err) = select.select([tcpsock, udpsock], [], [])
    s = r[0]
    if s == tcpsock:
        # We got data from tcp client. Send to UDP.
        (last, frag) = rpc.recvfrag(tcpsock)
        print "Recieved fragment from server, len=%d, last=%d" % (len(frag), last)
        udpsock.send(frag)        
    
    else:
        # We got data from the UDP server
        data = udpsock.recv(1024)
        print "Recieved fragment from client, len=%d" % len(data)
        rpc.sendfrag(tcpsock, 1, data)

conn.close()



