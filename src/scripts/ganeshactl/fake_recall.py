#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Copyright (C) 2012 Panasas Inc.
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

from __future__ import print_function
import getopt, sys
import dbus

def usage():
    print("fake_recall <clientid>")

def main():

    try:
        args = getopt.getopt(sys.argv[1:], "c", [])
        if len(args) < 1:
            usage()
            sys.exit(2)
        clientid = args[0]
        print(clientid)

        bus = dbus.SystemBus()
        cbsim = bus.get_object("org.ganesha.nfsd",
                               "/org/ganesha/nfsd/CBSIM")
        print(cbsim.Introspect())

        # call method
        fake_recall = cbsim.get_dbus_method('fake_recall',
                                            'org.ganesha.nfsd.cbsim')
        print(fake_recall(dbus.UInt64(clientid)))


    except getopt.GetoptError as err:
        print(str(err)) # will print something like "option -a not recognized"
        usage()
        sys.exit(2)

if __name__ == "__main__":
    main()
