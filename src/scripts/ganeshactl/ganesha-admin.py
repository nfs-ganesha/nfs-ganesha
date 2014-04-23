#!/usr/bin/python

import sys
from PyQt4 import QtCore, QtDBus
from PyQt4.QtGui import QApplication
from dbus.mainloop.qt import DBusQtMainLoop
from Ganesha.admin import AdminInterface

SERVICE = 'org.ganesha.nfsd'

class ServerAdmin(QtCore.QObject):

    show_status = QtCore.pyqtSignal(bool, str)
    
    def __init__(self, sysbus, parent=None):
        super(ServerAdmin, self).__init__()
        self.admin = AdminInterface(SERVICE,
                                    '/org/ganesha/nfsd/admin',
                                    sysbus,
                                    self.show_status)
        self.show_status.connect(self.status_message)

    def shutdown(self):
        self.admin.shutdown()
        print "Shutting down server."

    def reload(self):
        self.admin.reload()
        print "Reload server configuration."

    def grace(self, ipaddr):
        self.admin.grace(ipaddr)
        print "Start grace period."

    def status_message(self, status, errormsg):
        print "Returns: status = %s, %s" % (str(status), errormsg)
        sys.exit()

# Main
if __name__ == '__main__':
    app = QApplication(sys.argv)
    loop = DBusQtMainLoop(set_as_default=True)
    sysbus = QtDBus.QDBusConnection.systemBus()
    ganesha = ServerAdmin(sysbus)
    if sys.argv[1] == "shutdown":
        ganesha.shutdown()
    elif sys.argv[1] == "reload":
        ganesha.reload()
    elif sys.argv[1] == "grace":
        ganesha.grace(argv[2])
    else:
        print "Unknown/missing command"
        sys.exit()
    app.exec_()

