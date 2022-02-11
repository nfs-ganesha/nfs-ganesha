# SPDX-License-Identifier: LGPL-3.0-or-later
#
# client_mgr.py - ClientMgr DBus object class.
#
# Copyright (C) 2014 Panasas Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
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

from PyQt5 import QtCore, QtDBus
from collections import namedtuple

Client = namedtuple('Client',
                    ['ClientIP',
                     'HasNFSv3',
                     'HasMNT',
                     'HasNLM4',
                     'HasRQUOTA',
                     'HasNFSv40',
                     'HasNFSv41',
                     'HasNFSv42',
                     'Has9P',
                     'LastTime'])

class ClientMgr(QtDBus.QDBusAbstractInterface):
    '''
    org.ganesha.nfsd.clientmgr
    '''
    show_clients = QtCore.pyqtSignal(tuple, list)

    def __init__(self, service, path, connection, show_status, parent=None):
        super(ClientMgr, self).__init__(service,
                                        path,
                                        'org.ganesha.nfsd.clientmgr',
                                        connection,
                                        parent)
        self.show_status = show_status

    def AddClient(self, ipaddr):
        _async = self.asyncCall("AddClient", ipaddr)
        status = QtDBus.QDBusPendingCallWatcher(_async, self)
        status.finished.connect(self.clientmgr_done)

    def RemoveClient(self, ipaddr):
        _async = self.asyncCall("RemoveClient", ipaddr)
        status = QtDBus.QDBusPendingCallWatcher(_async, self)
        status.finished.connect(self.clientmgr_done)

    def ShowClients(self):
        _async = self.asyncCall("ShowClients")
        status = QtDBus.QDBusPendingCallWatcher(_async, self)
        status.finished.connect(self.clientshow_done)

    # catch the reply and forward it to the UI
    def clientmgr_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(False,
                                  "DBUS error:" + str(reply.error().message()))
        else:
            status = reply.argumentAt(0).toPyObject()
            msg = reply.argumentAt(1).toPyObject()
            self.show_status.emit(status, msg)

    def clientshow_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(False,
                                  "DBUS error:" + str(reply.error().message()))
        else:
            ts_ = (reply.argumentAt(0).toPyObject()[0].toULongLong()[0],
                   reply.argumentAt(0).toPyObject()[1].toULongLong()[0])
            interval_nsecs = ts_[0] * 1000000000 + ts_[1]
            clients = []
            for client in reply.argumentAt(1).toPyObject():
                cl_ = client.toPyObject()
                lasttime = cl_[9].toPyObject()
                clt = Client(ClientIP=str(cl_[0].toString()),
                             HasNFSv3=cl_[1].toBool(),
                             HasMNT=cl_[2].toBool(),
                             HasNLM4=cl_[3].toBool(),
                             HasRQUOTA=cl_[4].toBool(),
                             HasNFSv40=cl_[5].toBool(),
                             HasNFSv41=cl_[6].toBool(),
                             HasNFSv42=cl_[7].toBool(),
                             Has9P=cl_[8].toBool(),
                             LastTime=(lasttime[0].toPyObject(),
                                       lasttime[1].toPyObject()))
                clients.append(clt)
            self.show_clients.emit(ts_, clients)

class ClientStats(QtDBus.QDBusAbstractInterface):
    '''
    org.ganesha.nfsd.clientstats
    '''
    def __init__(self, service, path, connection, status_handler, parent=None):
        super(ClientStats, self).__init__(service, path,
                                          'org.ganesha.nfsd.clientstats',
                                          connection, parent)
        self.status_handler = status_handler

    def GetNFSv3IO(self, ipaddr):
        _async = self.asyncCall("GetNFSv3IO", ipaddr)
        status = QtDBus.QDBusPendingCallWatcher(_async, self)
        status.finished.connect(self.io_done)

    def GetNFSv40IO(self, ipaddr):
        _async = self.asyncCall("GetNFSv40IO", ipaddr)
        status = QtDBus.QDBusPendingCallWatcher(_async, self)
        status.finished.connect(self.io_done)

    def GetNFSv41IO(self, ipaddr):
        _async = self.asyncCall("GetNFSv41IO", ipaddr)
        status = QtDBus.QDBusPendingCallWatcher(_async, self)
        status.finished.connect(self.io_done)

    def GetNFSv41Layouts(self, ipaddr):
        _async = self.asyncCall("GetNFSv41Layouts", ipaddr)
        status = QtDBus.QDBusPendingCallWatcher(_async, self)
        status.finished.connect(self.layout_done)

    def io_done(self, call):
        pass

    def layout_done(self, call):
        pass

