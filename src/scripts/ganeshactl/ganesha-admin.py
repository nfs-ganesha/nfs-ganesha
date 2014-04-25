#!/usr/bin/python
#
# ganesha-admin.py - commandline tool for admin of nfs-ganesha.
#
# Copyright (C) 2014 Panasas Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
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
#-*- coding: utf-8 -*-

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

