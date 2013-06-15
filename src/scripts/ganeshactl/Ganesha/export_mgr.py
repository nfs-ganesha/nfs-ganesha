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
    
    def __init__(self, service, path, connection, show_status, parent=None):
        super(ExportMgr, self).__init__(service,
                                        path,
                                        'org.ganesha.nfsd.exportmgr',
                                        connection,
                                        parent)
        self.show_status = show_status

    def ShowExports(self):
        async = self.asyncCall("ShowExports")
        status = QtDBus.QDBusPendingCallWatcher(async, self)
        status.finished.connect(self.exportshow_done)

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
    
