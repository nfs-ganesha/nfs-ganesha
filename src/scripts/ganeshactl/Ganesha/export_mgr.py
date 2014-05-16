#
# export_mgr.py - ExportMgr DBUS object class.
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

import sys, time
from PyQt4.QtCore import *
from PyQt4 import QtDBus, QtGui
from collections import namedtuple

Export = namedtuple('Export',
                    ['ExportID',
                     'ExportPath',
                     'HasNFSv3',
                     'HasMNT',
                     'HasNLM4',
                     'HasRQUOTA',
                     'HasNFSv40',
                     'HasNFSv41',
                     'Has9P',
                     'LastTime'])

class ExportMgr(QtDBus.QDBusAbstractInterface):
    '''
    org.ganesha.nfsd.exportmgr
    '''
    show_exports = pyqtSignal(tuple, list)
    display_export = pyqtSignal(int, str, str, str)
    
    def __init__(self, service, path, connection,
                 show_status, parent=None):
        super(ExportMgr, self).__init__(service,
                                        path,
                                        'org.ganesha.nfsd.exportmgr',
                                        connection,
                                        parent)
        self.show_status = show_status

    def AddExport(self, conf_path, exp_expr):
        async = self.asyncCall("AddExport", conf_path, exp_expr)
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.exportadd_done)

    def RemoveExport(self, exp_id):
        async = self.asyncCall("RemoveExport", int(exp_id))
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.exportrm_done)

    def DisplayExport(self, exp_id):
        async = self.asyncCall("DisplayExport", int(exp_id))
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.exportdisplay_done)
        
    def ShowExports(self):
        async = self.asyncCall("ShowExports")
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.exportshow_done)

    def exportadd_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(False,
                                  "Error:" + str(reply.error().message()))
        else:
            message = reply.argumentAt(0).toPyObject()
            self.show_status.emit(True, "Done: " + message)

    def exportrm_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(False,
                                  "Error:" + str(reply.error().message()))
        else:
            self.show_status.emit(True, "Done")

    def exportdisplay_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(False,
                                  "Error:" + str(reply.error().message()))
        else:
            id = reply.argumentAt(0).toPyObject()
            fullpath = reply.argumentAt(1).toPyObject()
            pseudopath = reply.argumentAt(2).toPyObject()
            tag = reply.argumentAt(3).toPyObject()
            self.display_export.emit(id, fullpath, pseudopath, tag)
            
    def exportshow_done(self, call):
        reply = QtDBus.QDBusPendingReply(call)
        if reply.isError():
            self.show_status.emit(False,
                                  "DBUS error:" + str(reply.error().message()))
        else:
            ts = (reply.argumentAt(0).toPyObject()[0].toULongLong()[0],
                  reply.argumentAt(0).toPyObject()[1].toULongLong()[0])
            exports = []
            for export in reply.argumentAt(1).toPyObject():
                ex = export.toPyObject()
                lasttime = ex[9].toPyObject()
                exp = Export(ExportID = ex[0].toInt()[0],
                             ExportPath = str(ex[1].toString()),
                             HasNFSv3 = ex[2].toBool(),
                             HasMNT = ex[3].toBool(),
                             HasNLM4 = ex[4].toBool(),
                             HasRQUOTA = ex[5].toBool(),
                             HasNFSv40 = ex[6].toBool(),
                             HasNFSv41 = ex[7].toBool(),
                             Has9P = ex[8].toBool(),
                             LastTime = (lasttime[0].toPyObject(),
                                         lasttime[1].toPyObject()))
                exports.append(exp)
            self.show_exports.emit(ts, exports)


class ExportStats(QtDBus.QDBusAbstractInterface):
    '''
    org.ganesha.nfsd.exportstats
    '''
    def __init__(self, service, path, connection, stats_handler, parent=None):
        super(ExportStats, self).__init__(service,
                                          path,
                                          'org.ganesha.nfsd.exportstats',
                                          connection,
                                          parent)
        self.stats_handler = stats_handler

    def GetNFSv3IO(self, exportid):
        async = self.asyncCall("GetNFSv3IO", exportid)
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.io_done)

    def GetNFSv40IO(self, exportid):
        async = self.asyncCall("GetNFSv40IO", exportid)
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.io_done)

    def GetNFSv41IO(self, exportid):
        async = self.asyncCall("GetNFSv41IO", exportid)
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.io_done)

    def GetNFSv41Layouts(self, exportid):
        async = self.asyncCall("GetNFSv41Layouts", exportid)
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.layout_done)

    def io_done(self, call):
        pass

    def layout_done(self, call):
        pass
    
