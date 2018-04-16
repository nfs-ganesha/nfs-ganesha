#!/usr/bin/python2
#
# manage_clients.py - commandline tool for managing clients of nfs-ganesha.
#
# Copyright (C) 2014 Panasas Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
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
#-*- coding: utf-8 -*-
from __future__ import print_function
import sys
import time
from PyQt4 import QtCore, QtDBus
from PyQt4.QtGui import QApplication
from dbus.mainloop.qt import DBusQtMainLoop
from Ganesha.client_mgr import ClientMgr

SERVICE = 'org.ganesha.nfsd'

class ManageClients(QtCore.QObject):

    show_status = QtCore.pyqtSignal(bool, str)

    def __init__(self, sysbus, parent=None):
        super(ManageClients, self).__init__()
        self.clientmgr = ClientMgr(SERVICE,
                                   '/org/ganesha/nfsd/ClientMgr',
                                   sysbus, self.show_status)
        self.show_status.connect(self.status_message)
        self.clientmgr.show_clients.connect(self.proc_clients)

    def addclient(self, ipaddr):
        self.clientmgr.AddClient(ipaddr)
        print("Add a client %s" % (ipaddr))

    def removeclient(self, ipaddr):
        self.clientmgr.RemoveClient(ipaddr)
        print("Remove a client %s" % (ipaddr))

    def showclients(self):
        self.clientmgr.ShowClients()
        print("Show clients")

    def proc_clients(self, ts, clients):
        print("Timestamp: ", time.ctime(ts[0]), ts[1], " nsecs")
        if len(clients) == 0:
            print("No clients")
        else:
            print("Clients:")
            print(" IP addr,  nfsv3, mnt, nlm4, rquota,nfsv40, nfsv41, 9p, last")
            for client in clients:
                print(" %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s %d nsecs" %
                       (client.ClientIP,
                        client.HasNFSv3,
                        client.HasMNT,
                        client.HasNLM4,
                        client.HasRQUOTA,
                        client.HasNFSv40,
                        client.HasNFSv41,
                        client.Has9P,
                        time.ctime(client.LastTime[0]), client.LastTime[1]))
        sys.exit()

    def status_message(self, status, errormsg):
        print("Error: status = %s, %s" % (str(status), errormsg))
        sys.exit()

# Main
if __name__ == '__main__':
    app = QApplication(sys.argv)
    loop = DBusQtMainLoop(set_as_default=True)
    sysbus = QtDBus.QDBusConnection.systemBus()
    clientmgr = ManageClients(sysbus)
    if sys.argv[1] == "add":
        clientmgr.addclient(sys.argv[2])
    elif sys.argv[1] == "remove":
        clientmgr.removeclient(sys.argv[2])
    elif sys.argv[1] == "show":
        clientmgr.showclients()
    else:
        print("unknown/missing command")
        sys.exit()

    app.exec_()
