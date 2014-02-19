#!/usr/bin/python

# You must initialize the gobject/dbus support for threading
# before doing anything.
import gobject
import sys


gobject.threads_init()

from dbus import glib
glib.init_threads()

# Create a session bus.
import dbus
bus = dbus.SystemBus()

# Create an object that will proxy for a particular remote object.
try:
	admin = bus.get_object("org.ganesha.nfsd",
                       "/org/ganesha/nfsd/ExportMgr")
except: # catch *all* exceptions
      print "Error: Can't talk to ganesha service on d-bus. Looks like Ganesha is down"
      sys.exit(1)



# call method
ganesha_exports = admin.get_dbus_method('ShowExports',
                              	'org.ganesha.nfsd.exportmgr')
ganesha_nfsstats_v3 = admin.get_dbus_method('GetNFSv3IO',
                               'org.ganesha.nfsd.exportstats')
ganesha_nfsstats_v4 = admin.get_dbus_method('GetNFSv40IO',
                               'org.ganesha.nfsd.exportstats')


export_list=ganesha_exports()
for exports in export_list[1]:
	export_id=exports[0]
	stats_v3=ganesha_nfsstats_v3(export_id)
	stats_v4=ganesha_nfsstats_v4(export_id)
	print "Statistics for:",exports[1]
	print '\t\trequested\ttransferred\ttotal\terrors\tlatency\t\tqueue wait'
	if stats_v3[1] == "Export does not have any NFSv3 activity":
		print "No NFSv3 activity"
	else:
		print 'readv3 ',
		for stat in stats_v3[3]:
			print '\t', stat,
		print
		print 'writev3',
		for stat in stats_v3[4]:
			print '\t', stat,
		print
	if stats_v4[1] == "Export does not have any NFSv4.0 activity":
		print "No NFSv4.0 activity"
	else:
		print 'readv4 ',
		for stat in stats_v4[3]:
			print '\t', stat,
		print
		print 'writev4',
		for stat in stats_v4[4]:
			print '\t', stat,
		print


sys.exit(0)
