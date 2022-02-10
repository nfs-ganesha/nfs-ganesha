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

# Create a system dbu bus object.
import dbus
bus = dbus.SystemBus()

# Create an object that will proxy for a particular remote object.
cbsim = bus.get_object("org.ganesha.nfsd",
                       "/org/ganesha/nfsd/CBSIM")
print("introspection data")
introspect = dbus.Interface(
    cbsim,
    dbus.INTROSPECTABLE_IFACE,
    )
print(introspect.Introspect())

# call method
get_client_ids = cbsim.get_dbus_method('get_client_ids',
                                       'org.ganesha.nfsd.cbsim')
print("client ids:")
print(get_client_ids())
