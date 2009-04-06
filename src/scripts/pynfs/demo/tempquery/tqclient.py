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

class PartialTQClient:
    def __init__(self):
        pass

    def addpackers(self):
        self.packer = tqpacker.TQPacker(self)
        self.unpacker = tqpacker.TQUnpacker(self, '')

    def unpack_month_temperatures(self):
        return self.unpacker.unpack_array(self.unpacker.unpack_uint)

    def call(self, month):
        res = self.make_call(0, month, self.packer.pack_uint, self.unpack_month_temperatures)
        return res

class UDPTQClient(PartialTQClient, rpc.RawUDPClient):
    def __init__(self, host):
        rpc.RawUDPClient.__init__(self, host, TQ_PROGRAM, TQ_VERSION, TQ_PORT)
        PartialTQClient.__init__(self)


if __name__ == "__main__":
    tqcl = UDPTQClient("localhost")

    res = tqcl.call(22)
    print "Got result", res
