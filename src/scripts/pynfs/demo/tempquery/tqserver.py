#!/usr/bin/env python2

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
from tqconstants import *
from tqtypes import *
import tqpacker
import socket
import os


class TQServer(rpc.UDPServer):
    def handle_0(self):
        print "Got request"
        m = self.unpacker.unpack_uint()
        print "Arguments was", m

        self.turn_around()
        self.packer.pack_array([1, 2, 3], self.packer.pack_int)
        
        #res = PFresults(self, status=TRUE, phone="555-12345")
        #res.pack()


if __name__ == "__main__":
    s =  TQServer("", TQ_PROGRAM, TQ_VERSION, TQ_PORT)
    print "Service started..."
    try:
        s.loop()
    finally:
        print "Service interrupted."
    

