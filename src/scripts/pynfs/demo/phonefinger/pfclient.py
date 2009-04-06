#!/usr/bin/env python2

# pfclient.py - An example of an RPC client in pynfs
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

class PartialPFClient:
    def __init__(self):
        pass

    def addpackers(self):
        self.packer = pfpacker.PFPacker(self)
        self.unpacker = pfpacker.PFUnpacker(self, '')

    def null(self):
	return self.make_call(0, None, None, None)

    def call(self, user):
        args = PFargs(self, user)
        res = PFresults(self)
        self.make_call(1, None, args.pack, res.unpack)
        return res

class UDPPFClient(PartialPFClient, rpc.RawUDPClient):
    def __init__(self, host):
        rpc.RawUDPClient.__init__(self, host, PF_PROGRAM, PF_VERSION, PF_PORT)
        PartialPFClient.__init__(self)


if __name__ == "__main__":
    pfcl = UDPPFClient("localhost")

    # Call null procedure
    pfcl.null()

    res = pfcl.call("astrand")
    print "Got result", res
