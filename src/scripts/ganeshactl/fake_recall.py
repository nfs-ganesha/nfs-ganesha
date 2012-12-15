#!/usr/bin/python

import getopt, sys
import gobject

gobject.threads_init()
from dbus import glib
glib.init_threads()

import dbus

def usage():
    print "fake_recall <clientid>"

def main():

    try:
        opts, args = getopt.getopt(sys.argv[1:], "c", [])
        if len(args) < 1:
            usage()
            sys.exit(2)
        clientid = args[0]
        print clientid

        bus = dbus.SystemBus()
        cbsim = bus.get_object("org.ganesha.nfsd",
                               "/org/ganesha/nfsd/CBSIM")
        print cbsim.Introspect()

        # call method
        fake_recall = cbsim.get_dbus_method('fake_recall')
        print fake_recall(dbus.UInt64(clientid))


    except getopt.GetoptError, err:
        print str(err) # will print something like "option -a not recognized"
        usage()
        sys.exit(2)

if __name__ == "__main__":
    main()
