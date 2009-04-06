#!/usr/bin/env python2

# pfserver.py - An example of an RPC server in pynfs
#
# Written by Peter Åstrand <peter@cendio.se>
# Copyright (C) 2001 Cendio Systems AB (http://www.cendio.se)
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License. 
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


import rpc
from pfconstants import *
from pftypes import *
import pfpacker
import socket
import os


class PFServer(rpc.UDPServer):
    def handle_0(self):
        print "Handling NULL procedure"
        self.turn_around()
    
    def handle_1(self):
        print "Got request"
        args = PFargs(self)
        args.unpack()
        print "Arguments was", args

        self.turn_around()
        res = PFresults(self, status=TRUE, phone="555-12345")
        res.pack()


if __name__ == "__main__":
    s =  PFServer("", PF_PROGRAM, PF_VERSION, PF_PORT)
    print "Service started..."
    try:
        s.loop()
    finally:
        print "Service interrupted."
    

