#!/usr/bin/python2
#
# manage_exports.py - commandline tool for managing exports in nfs-ganesha.
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
from Ganesha.export_mgr import ExportMgr

SERVICE = 'org.ganesha.nfsd'

class ShowExports(QtCore.QObject):

    show_status = QtCore.pyqtSignal(bool, str)

    def __init__(self, sysbus, parent=None):
        super(ShowExports, self).__init__()
        self.exportmgr = ExportMgr(SERVICE,
                                   '/org/ganesha/nfsd/ExportMgr',
                                   sysbus, self.show_status)
        self.show_status.connect(self.status_message)
        self.exportmgr.show_exports.connect(self.proc_exports)
        self.exportmgr.display_export.connect(self.proc_export)

    def showexports(self):
        self.exportmgr.ShowExports()
        print("Show exports")

    def addexport(self, conf_path, exp_expr):
        self.exportmgr.AddExport(conf_path, exp_expr)
        print("Add Export in %s" % conf_path)

    def updateexport(self, conf_path, exp_expr):
        self.exportmgr.UpdateExport(conf_path, exp_expr)
        print("Update Export in %s" % conf_path)

    def removeexport(self, exp_id):
        self.exportmgr.RemoveExport(exp_id)
        print("Remove Export with id %d" % int(exp_id))

    def displayexport(self, exp_id):
        self.exportmgr.DisplayExport(exp_id)
        print("Display export with id %d" % int(exp_id))

    def proc_export(self, id, path, pseudo, tag):
        print("export %d: path = %s, pseudo = %s, tag = %s" % (id, path, pseudo, tag))
        sys.exit()

    def proc_exports(self, ts, exports):
        print("Timestamp: ", time.ctime(ts[0]), ts[1], " nsecs")
        if len(exports) == 0:
            print("No exports")
        else:
            print("Exports:")
            print("  Id, path,    nfsv3, mnt, nlm4, rquota,nfsv40, nfsv41, 9p, last")
            for export in exports:
                print(" %d,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s %d nsecs" %
                       (export.ExportID,
                        export.ExportPath,
                        export.HasNFSv3,
                        export.HasMNT,
                        export.HasNLM4,
                        export.HasRQUOTA,
                        export.HasNFSv40,
                        export.HasNFSv41,
                        export.Has9P,
                        time.ctime(export.LastTime[0]), export.LastTime[1]))
        sys.exit()

    def status_message(self, status, errormsg):
        print("Error: status = %s, %s" % (str(status), errormsg))
        sys.exit()

# Main
if __name__ == '__main__':
    app = QApplication(sys.argv)
    loop = DBusQtMainLoop(set_as_default=True)
    sysbus = QtDBus.QDBusConnection.systemBus()
    exportmgr = ShowExports(sysbus)
    if sys.argv[1] == "add":
        exportmgr.addexport(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "update":
        exportmgr.updateexport(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "remove":
        exportmgr.removeexport(sys.argv[2])
    elif sys.argv[1] == "display":
        exportmgr.displayexport(sys.argv[2])
    elif sys.argv[1] == "show":
        exportmgr.showexports()
    else:
        print("Unknown/missing command")
        sys.exit()
    app.exec_()
