#!/usr/bin/python

# You must initialize the gobject/dbus support for threading
# before doing anything.
import sys, gobject, dbus, dbus.glib
gobject.threads_init()
dbus.glib.init_threads()

# Create a session bus.
bus = dbus.SystemBus()

# Create an object that will proxy for a particular remote object.
admin = bus.get_object("org.ganesha.nfsd", "/org/ganesha/nfsd/admin")

# call method
purge_gids = admin.get_dbus_method('purge_gids', 'org.ganesha.nfsd.admin')

(purged, msg) = purge_gids()
if not purged:
    print("Purging gids cache failed: %s" % msg)
    sys.exit(1)
