#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Copyright (C) 2013 IBM
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
# Author: Marc Eshel <eshel@us.ibm.com>

from __future__ import print_function
import sys

ipaddr = sys.argv[1]
print('event:ip_addr=', ipaddr)

# Create a system bus object.
import dbus
bus = dbus.SystemBus()

# Create an object that will proxy for a particular remote object.
try:
    admin = bus.get_object("org.ganesha.nfsd",
                           "/org/ganesha/nfsd/admin")
except dbus.exceptions.DBusException as e:
    sys.exit("Error: Can't talk to ganesha service on d-bus. "
             "Looks like Ganesha is down")

# call method
ganesha_grace = admin.get_dbus_method('grace', 'org.ganesha.nfsd.admin')

print("Start grace period.")

try:
    print(ganesha_grace(ipaddr))
except dbus.exceptions.DBusException as e:
    sys.exit("Error: Failed to start grace period")
