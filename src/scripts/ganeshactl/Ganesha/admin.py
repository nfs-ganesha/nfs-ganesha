# SPDX-License-Identifier: LGPL-3.0-or-later
#
# admin.py - AdminInterface DBUS object class.
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

from PyQt5 import QtDBus

class AdminInterface(QtDBus.QDBusAbstractInterface):
    '''
    org.ganesha.nfsd.admin interface
    '''
    def __init__(self, service, path, connection, show_status, parent=None):
        super(AdminInterface, self).__init__(service,
                                             path,
                                             'org.ganesha.nfsd.admin',
                                             connection,
                                             parent)
        self.show_status = show_status

    def grace(self, ipaddr):
        _async = self.asyncCall("grace", ipaddr)
        status = QtDBus.QDBusPendingCallWatcher(_async, self)
        status.finished.connect(self.admin_done)

    def reload(self):
        _async = self.asyncCall("reload")
        status = QtDBus.QDBusPendingCallWatcher(_async, self)
        status.finished.connect(self.admin_done)

    def shutdown(self):
        _async = self.asyncCall("shutdown")
        status = QtDBus.QDBusPendingCallWatcher(_async, self)
        status.finished.connect(self.admin_done)

    # catch the reply and forward it to the UI
    def admin_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(False,
                                  "DBUS error:" + str(reply.error().message()))
        else:
            status = reply.argumentAt(0).toPyObject()
            msg = reply.argumentAt(1).toPyObject()
            self.show_status.emit(status, msg)

