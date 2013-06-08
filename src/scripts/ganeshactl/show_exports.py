#!/usr/bin/python

import sys
import time
from PyQt4 import QtCore, QtDBus
from PyQt4.QtGui import QApplication
from dbus.mainloop.qt import DBusQtMainLoop
from export_mgr import ExportMgr

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

    def showexports(self):
        self.exportmgr.ShowExports()
        print "Show exports"

    def proc_exports(self, ts, exports):
        print "Timestamp: ", time.ctime(ts[0]), ts[1], " nsecs"
        if len(exports) == 0:
            print "No exports"
        else:
            print "Exports:"
            print "  Id, path,    nfsv3, mnt, nlm4, rquota,nfsv40, nfsv41, 9p, last"
            for export in exports:
                print (" %d,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s %d nsecs" %
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
        print "Error: status = %s, %s" % (str(status), errormsg)
        sys.exit()
        
# Main
if __name__ == '__main__':
    app = QApplication(sys.argv)
    loop = DBusQtMainLoop(set_as_default=True)
    sysbus = QtDBus.QDBusConnection.systemBus()
    exportmgr = ShowExports(sysbus)
    exportmgr.showexports()
    app.exec_()
