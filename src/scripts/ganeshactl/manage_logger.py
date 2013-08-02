#!/usr/bin/python

import sys
import time
from PyQt4 import QtCore, QtDBus
from PyQt4.QtGui import QApplication
from dbus.mainloop.qt import DBusQtMainLoop
from Ganesha.log_mgr import LogManager

SERVICE = 'org.ganesha.nfsd'

class ManageLogger(QtCore.QObject):

    show_status = QtCore.pyqtSignal(bool, str)

    def __init__(self, sysbus, parent=None):
        super(ManageLogger, self).__init__()
        self.logmgr = LogManager(SERVICE,
                                 sysbus,
                                 self.show_status)
        self.show_status.connect(self.status_message)
        self.logmgr.show_level.connect(self.proc_level)
        self.logmgr.show_components.connect(self.proc_components)

    def get_level(self, component):
        self.logmgr.Get(component)
        print "Getting log level for %s" % (component)

    def set_level(self, component, level):
        self.logmgr.Set(component, level)
        print "Setting log level for %s to %s" % (component, level)

    def getall(self):
        self.logmgr.GetAll()
        print "Fetching component log levels"

    def proc_level(self, level):
        print "Level = %s" % (level)
        sys.exit()
    
    def proc_components(self, components):
        print "dict of levels:"
        for comp in components.keys():
            print "Component %s is at %s" % (comp, components[comp])
        sys.exit()

    def status_message(self, status, errormsg):
        print "Error: status = %s, %s" % (str(status), errormsg)
        sys.exit()

# Main
if __name__ == '__main__':
    app = QApplication(sys.argv)
    loop = DBusQtMainLoop(set_as_default=True)
    sysbus = QtDBus.QDBusConnection.systemBus()
    logger = ManageLogger(sysbus)
    if sys.argv[1] == "get":
        logger.get_level(sys.argv[2])
    elif sys.argv[1] == "set":
        logger.set_level(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "getall":
        logger.getall()
    else:
        print "unknown/missing command"
        sys.exit()

    app.exec_()
