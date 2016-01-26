#
# io_stats.py - IOstat table object.
#
# Copyright (C) 2014 Panasas Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Author: Jim Lieb <jlieb@panasas.com>
#-*- coding: utf-8 -*-

from collections import namedtuple

# Basic I/O reply and io stats structs
BasicIO = namedtuple('BasicIO',
                     [requested,
                      transferred,
                      total_ops,
                      errors,
                      latency,
                      queue_wait])

IOReply = namedtuple('IOReply',
                     [status,
                      errormsg,
                      sampletime,
                      read,
                      write])

# pNFS layout stats reply and stats structs
Layout = namedtuple('Layout',
                    [total_ops,
                     errors,
                     delays])

pNFSReply = namedtuple('pNFSReply',
                       [status,
                        errormsg,
                        sampletime,
                        getdevinfo,
                        layout_get,
                        layout_commit,
                        layout_return,
                        layout_recall])

class IOstat(Object):
    def __init__(self, stat):
        pass
