#
# clients_table.py - ClientTableModel model class.
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

import sys, time
from PyQt4.QtCore import *
from PyQt4 import QtDBus, QtGui
from PyQt4.QtGui import QColor

class ClientTableModel(QAbstractTableModel):
    '''
    Clients Table Model to match its table view
    '''
    
    def __init__(self, clientmgr, parent=None):
        super(ClientTableModel, self).__init__(parent)
        self.header = ['Client IP',
                       'NFSv3',
                       'MNT',
                       'NLMv4',
                       'RQUOTA',
                       'NFSv4.0',
                       'NFSv4.1',
                       '9P',
                       'Last Stats Update']
        self.clientmgr = clientmgr
        self.clientmgr.show_clients.connect(self.FetchClients_done)
        self.clients = []
        self.ts = (0L, 0L)

    # Fetch current clients
    def FetchClients(self):
        self.clientmgr.ShowClients()

    def FetchClients_done(self, ts, clients):
        if len(self.clients) != len(clients):
            if len(self.clients) > 0:
                self.removeRows(0, len(self.clients))
            self.insertRows(0, len(clients))
        for i in xrange(len(clients)):
            exp = clients[i]
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
            t = self.clients[row]
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
            self.clients.insert(row, ['',]*self.columnCount())
        self.endInsertRows()
        return True

    def removeRow(self, row, parent=QModelIndex()):
        self.removeRows(self, row, 1, parent)

    def removeRows(self, row, count, parent=QModelIndex()):
        self.beginRemoveRows(parent, row, row + count -1)
        for i in reversed(xrange(count)):
            self.clients.pop(row + i)
        self.endRemoveRows()
    
    def rowCount(self, parent=QModelIndex()):
        return len(self.clients)

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
            return QVariant(self.clients[index.row()][index.column()])
        elif role == Qt.TextAlignmentRole:
            align = Qt.AlignVCenter
            if index.column() == 0:
                align = align + Qt.AlignRight
            elif index.column() == 9:
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

