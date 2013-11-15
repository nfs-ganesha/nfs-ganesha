#!/usr/bin/python

import sys
from PyQt4 import QtCore, QtDBus
from PyQt4.QtGui import QApplication
from dbus.mainloop.qt import DBusQtMainLoop
from Ganesha.admin import AdminInterface

SERVICE = 'org.ganesha.nfsd'

class Reload(QtCore.QObject):

    show_status = QtCore.pyqtSignal(bool, str)
    
    def __init__(self, sysbus, parent=None):
        super(Reload, self).__init__()
        self.admin = AdminInterface(SERVICE,
                                    '/org/ganesha/nfsd/admin',
                                    sysbus,
                                    self.show_status)
        self.show_status.connect(self.status_message)

    def reload(self):
        self.admin.reload()
        print "Reload server configuration."

    def status_message(self, status, errormsg):
        print "Returns: status = %s, %s" % (str(status), errormsg)
        sys.exit()

# Main
if __name__ == '__main__':
    app = QApplication(sys.argv)
    loop = DBusQtMainLoop(set_as_default=True)
    sysbus = QtDBus.QDBusConnection.systemBus()
    reload = Reload(sysbus)
    reload.reload()
    app.exec_()

