#
# log_settings.py - DebugLevelDelegate class.
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

from PyQt4.QtCore import *
from PyQt4 import QtGui
from Ganesha.QtUI.ui_log_dialog import Ui_LogSettings

class DebugLevelDelegate(QtGui.QItemDelegate):
    '''
    Log level combo box editor
    '''

    # Copied from log_levels_t in src/include/log.h:69
    loglevels = ['NIV_NULL',
                 'NIV_FATAL',
                 'NIV_MAJ',
                 'NIV_CRIT',
                 'NIV_WARN',
                 'NIV_EVENT',
                 'NIV_INFO',
                 'NIV_DEBUG',
                 'NIV_MID_DEBUG',
                 'NIV_FULL_DEBUG']

    def __init__(self, parent = None):
        super(DebugLevelDelegate, self).__init__(parent)

    def createEditor(self, parent, option, index):
        editor = QtGui.QComboBox(parent)
        editor.addItems(self.loglevels)
        editor.setCurrentIndex(0)
        editor.installEventFilter(self)
        return editor

    def setEditorData(self, editor, index):
        value = index.data(Qt.DisplayRole).toString()
        combo_index = editor.findText(value)
        editor.setCurrentIndex(combo_index)
        
    def setModelData(self, editor, model, index):
        text = editor.currentText()
        model.setData(index, text, Qt.EditRole)

    def updateEditorGeometry(self, editor, option, index):
        editor.setGeometry(option.rect)

class LogSettingsModel(QAbstractTableModel):
    '''
    Table of log settings as an editable form.
    Radio buttons are used for setting levels
    '''

    def __init__(self, logmanager, parent=None):
        super(LogSettingsModel, self).__init__(parent)
        self.header = ['Log Component', 'Logging Level']
        self.logmanager = logmanager
        self.logmanager.show_components.connect(self.getComponents_done)
        self.log_components = []

    def getComponents(self):
        self.logmanager.GetAll()

    def getComponents_done(self, comp_dict):
        if len(self.log_components) != len(comp_dict):
            if len(self.log_components) > 0:
                self.removeRows(0, len(self.log_components))
            self.insertRows(0,len(comp_dict))
        comps = comp_dict.keys()
        comps.sort()
        # Populate the table in sorted order by hand.
        # in order to avoid the signal in setData which would
        # go back to ganesha and set what we just got...
        for i in xrange(len(comps)):
            self.log_components[i] = [comps[i], comp_dict[comps[i]]]

    def updateSetting(self, ULIndex, LRIndex):
        comp = self.log_components[ULIndex.row()][0]
        level = self.log_components[ULIndex.row()][1]
        self.logmanager.Set(comp, level)
        # Refresh the table because things like "All" whack everybody...
        self.logmanager.GetAll()

    # model abstract methods
    def setData(self, index, value, role = Qt.EditRole):
        if role == Qt.EditRole:
            row = index.row()
            col = index.column()
            comp = self.log_components[row]
            comp[col] = value
            self.dataChanged.emit(index, index)
            return True
        else:
            return False

    def insertRow(self, row, parent=QModelIndex()):
        self.insertRows(self, row, 1, parent)

    def insertRows(self, row, count, parent=QModelIndex()):
        self.beginInsertRows(parent, row, row + count -1)
        for row in xrange(count):
            self.log_components.insert(row, ['',]*self.columnCount())
        self.endInsertRows()
        return True

    def removeRow(self, row, parent=QModelIndex()):
        self.removeRows(self, row, 1, parent)

    def removeRows(self, row, count, parent=QModelIndex()):
        self.beginRemoveRows(parent, row, count + count -1)
        for i in reversed(xrange(count)):
            self.log_comp_levels.pop(row + i)
        self.endRemoveRows()

    def rowCount(self, parent=QModelIndex()):
        return len(self.log_components)

    def columnCount(self, parent=QModelIndex()):
        return len(self.header)

    def headerData(self, section, orientation, role):
        if role == Qt.DisplayRole:
            if orientation == Qt.Horizontal:
                return QVariant(self.header[section])
        else:
            return QVariant()

    def flags(self, index):
        if not index.isValid():
            return Qt.ItemIsEnabled
        if index.column() == 1:
            return Qt.ItemFlags(QAbstractTableModel.flags(self, index) |
                                Qt.ItemIsEditable)
        else:
            return Qt.ItemFlags(QAbstractTableModel.flags(self, index))

    def data(self, index, role):
        if not index.isValid():
            return QVariant()
        elif role == Qt.DisplayRole:
            if index.column() == 0:
                comp = self.log_components[index.row()][0]
                return QVariant(comp)
            else:
                return QVariant(self.log_components[index.row()][1])
        elif role == Qt.EditRole:
            if index.column() == 0:
                comp = self.log_components[index.row()][0]
                print "edit partitioned comp", comp
                return QVariant(comp)
            else:
                return QVariant(self.log_components[index.row()][1])
        elif role == Qt.TextAlignmentRole:
            align = Qt.AlignCenter
            if index.column() == 0:
                align = Qt.AlignLeft
            return QVariant(align)

class LogSetDialog(QtGui.QDialog):
    '''
    Manage popup of log setting dialog
    '''

    def __init__(self, log_mgr, parent=None):
        super(LogSetDialog, self).__init__(parent)
        self.log_ui = Ui_LogSettings()
        self.log_ui.setupUi(self)
        self.log_mgr = log_mgr
            
        self.log_setting_model = LogSettingsModel(self.log_mgr)
        self.level_edit_delegate = DebugLevelDelegate()
        self.log_ui.log_levels.setModel(self.log_setting_model)
        self.log_ui.log_levels.resizeColumnsToContents()
        self.log_ui.log_levels.verticalHeader().setVisible(False)
        self.log_ui.log_levels.setItemDelegateForColumn(1,
                                                        self.level_edit_delegate)
        self.log_ui.log_done.clicked.connect(self.close_logsetting_dialog)
        self.log_setting_model.getComponents()
        self.log_setting_model.dataChanged.connect(self.log_setting_model.updateSetting)
        
    def show_logsetting_dialog(self):
        self.show()

    def close_logsetting_dialog(self):
        self.hide()
