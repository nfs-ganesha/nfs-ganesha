import sys, time
from PyQt4.QtCore import *
from PyQt4 import QtDBus, QtGui
from PyQt4.QtGui import QColor

class ExportTableModel(QAbstractTableModel):
    '''
    Exports Table Model to match its table view
    '''
    
    def __init__(self, exportmgr, parent=None):
        super(ExportTableModel, self).__init__(parent)
        self.header = ['Export ID',
                       'Export Path',
                       'NFSv3',
                       'MNT',
                       'NLMv4',
                       'RQUOTA',
                       'NFSv4.0',
                       'NFSv4.1',
                       '9P',
                       'Last Stats Update']
        self.exportmgr = exportmgr
        self.exportmgr.show_exports.connect(self.FetchExports_done)
        self.exports = []
        self.ts = (0L, 0L)

    # Fetch current exports
    def FetchExports(self):
        self.exportmgr.ShowExports()

    def FetchExports_done(self, ts, exports):
        if len(self.exports) != len(exports):
            if len(self.exports) > 0:
                self.removeRows(0, len(self.exports))
            self.insertRows(0, len(exports))
        for i in xrange(len(exports)):
            exp = exports[i]
            for j in xrange(len(exp)):
                if isinstance(exp[j], bool):
                    if exp[j]:
                        val = "yes"
                    else:
                        val = "no"
                elif isinstance(exp[j], tuple):
                    val = time.ctime(exp[j][0])
                else:
                    val = str(exp[j])
                self.setData(self.createIndex(i, j), val)

    # model abstract methods
    def setData(self, index, value, role = Qt.EditRole):
        if role == Qt.EditRole:
            row = index.row()
            col = index.column()
            t = self.exports[row]
            if row > self.rowCount() or col > self.columnCount():
                return False
            t[col] = value
            self.emit(SIGNAL('dataChanged'), index, index)
            return True
        return False

    def insertRow(self, row, parent=QModelIndex()):
        self.insertRows(self, row, 1, parent)

    def insertRows(self, row, count, parent=QModelIndex()):
        self.beginInsertRows(parent, row, row + count - 1)
        for i in xrange(count):
            self.exports.insert(row, ['',]*self.columnCount())
        self.endInsertRows()
        return True

    def removeRow(self, row, parent=QModelIndex()):
        self.removeRows(self, row, 1, parent)

    def removeRows(self, row, count, parent=QModelIndex()):
        self.beginRemoveRows(parent, row, row + count -1)
        for i in reversed(xrange(count)):
            self.exports.pop(row + i)
        self.endRemoveRows()
    
    def rowCount(self, parent=QModelIndex()):
        return len(self.exports)

    def columnCount(self, parent=QModelIndex()):
        return len(self.header)

    def headerData(self, col, orientation, role):
        if orientation == Qt.Horizontal and role == Qt.DisplayRole:
            return QVariant(self.header[col])
        return QVariant()

    def flags(self, index):
        return Qt.NoItemFlags

    def data(self, index, role):
        if not index.isValid():
            return QVariant()
        elif role == Qt.DisplayRole:
            return QVariant(self.exports[index.row()][index.column()])
        elif role == Qt.TextAlignmentRole:
            align = Qt.AlignVCenter
            if index.column() == 0:
                align = align + Qt.AlignRight
            elif index.column() == 1 or index.column() == 9:
                align = align + Qt.AlignLeft
            else:
                align = align + Qt.AlignCenter
            return QVariant(align)
        elif role == Qt.BackgroundRole:
            if index.row() % 2 == 0:
                return QVariant(QColor(Qt.gray))
            else:
                return QVariant(QColor(Qt.lightGray))
        elif role == Qt.ForegroundRole:
            return QVariant(QColor(Qt.black))

        return QVariant()

