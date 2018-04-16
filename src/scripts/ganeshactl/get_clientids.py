#!/usr/bin/python2

# You must initialize the gobject/dbus support for threading
# before doing anything.
from __future__ import print_function
import gobject
gobject.threads_init()

from dbus import glib
glib.init_threads()

# Create a session bus.
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
