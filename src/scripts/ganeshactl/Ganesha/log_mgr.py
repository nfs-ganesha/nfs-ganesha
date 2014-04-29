#
# log_mgr.py - LogManager DBus object class.
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

from PyQt4 import QtCore, QtDBus

ADMIN_OBJECT = '/org/ganesha/nfsd/admin'
PROP_INTERFACE = 'org.freedesktop.DBus.Properties'
LOGGER_PROPS = 'org.ganesha.nfsd.log.component'

class LogManager(QtDBus.QDBusAbstractInterface):
    '''
    org.ganesha.nfsd.log.component
    '''
    
    show_components = QtCore.pyqtSignal(dict)
    show_level = QtCore.pyqtSignal(str)
        
    def __init__(self, service, connection, show_status, parent=None):
        super(LogManager, self).__init__(service,
                                         ADMIN_OBJECT,
                                         PROP_INTERFACE,
                                         connection,
                                         parent)
        self.show_status = show_status

    def GetAll(self):
        async = self.asyncCall("GetAll", LOGGER_PROPS)
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.GetAll_done)

    def GetAll_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(False,
                                  "DBus error:" + str(reply.error().message()))
        else:
            # what follows is DBus+Qt magic.  We get a Variant object back
            # which contains a "map", aka "dict" in python.  Each item in
            # the map has a variant as a key and a variant as the value
            # first unwrap the top variant into d...
            # then walk d, unwrap the variant key to store the unwrapped
            # variant value into a string value.
            prop_dict = {}
            d = reply.value().toPyObject()
            for key in d.keys():
                prop_dict[str(key.toString())] = str(d[key].toPyObject().toString())
            self.show_components.emit(prop_dict)

    def Get(self, property):
        async = self.asyncCall("Get", LOGGER_PROPS, property)
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.Get_done)

    def Get_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(FALSE,
                                  "DBUS error:" + str(reply.error().message()))
        else:
            level = str(reply.value().toPyObject().toString())
            self.show_level.emit(level)

    def Set(self, property, setval):
        qval = QtDBus.QDBusVariant()
        qval.setVariant(str(str(setval)))
        async = self.asyncCall("Set", LOGGER_PROPS,
                               property,
                               qval)
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.Set_done)

    def Set_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(False,
                                  "DBUS error:" + str(reply.error().message() +
                                                      str(reply.error().name())))
        else:
            self.show_status.emit(True, "Done")
