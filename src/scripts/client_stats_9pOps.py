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
                       "/org/ganesha/nfsd/ClientMgr")
except: # catch *all* exceptions
      print "Error: Can't talk to ganesha service on d-bus. Looks like Ganesha is down"
      exit(1)



# call method
ganesha_9pOpstats = admin.get_dbus_method('Get9pOpStats',
                                          'org.ganesha.nfsd.clientstats')


# get parameters
if len(sys.argv) != 2:
	print "Usage: %s client_ipaddr" % sys.argv[0]
	exit(1)
client_ipaddr=sys.argv[1]

# for each 9p protocol operation
OpNames=("_9P_TSTATFS", "_9P_TLOPEN", "_9P_TLCREATE", "_9P_TSYMLINK", "_9P_TMKNOD", "_9P_TRENAME", "_9P_TREADLINK", "_9P_TGETATTR", "_9P_TSETATTR", "_9P_TXATTRWALK", "_9P_TXATTRCREATE", "_9P_TREADDIR", "_9P_TFSYNC", "_9P_TLOCK", "_9P_TGETLOCK", "_9P_TLINK", "_9P_TMKDIR", "_9P_TRENAMEAT", "_9P_TUNLINKAT", "_9P_TVERSION", "_9P_TAUTH", "_9P_TATTACH", "_9P_TFLUSH", "_9P_TWALK", "_9P_TOPEN", "_9P_TCREATE", "_9P_TREAD", "_9P_TWRITE", "_9P_TCLUNK", "_9P_TREMOVE", "_9P_TSTAT", "_9P_TWSTAT")

for opname in OpNames:
	opstats=ganesha_9pOpstats(client_ipaddr, opname)
	status=opstats[0]
	errmsg=opstats[1]
	if (not(status)):
		print errmsg
		break
	total=opstats[3][0]
	if (total != 0):
		print "%-16s\t%ld" % (opname, total)


sys.exit(0);
