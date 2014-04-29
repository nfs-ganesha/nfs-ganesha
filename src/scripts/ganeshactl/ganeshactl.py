#! /usr/bin/python
#
# ganeshactl.py - PyQt4 GUI tool for admin of nfs-ganesha.
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

"""
NFS Ganesha administration tool
"""

import sys

from PyQt4 import QtCore, QtGui, QtDBus 
from Ganesha.QtUI.ui_main_window import Ui_MainWindow
from Ganesha.admin import AdminInterface
from Ganesha.export_mgr import ExportMgr
from Ganesha.QtUI.exports_table import ExportTableModel
from Ganesha.client_mgr import ClientMgr
from Ganesha.QtUI.clients_table import ClientTableModel
from Ganesha.log_mgr import LogManager
from Ganesha.QtUI.log_settings import LogSetDialog

SERVICE = 'org.ganesha.nfsd'

class MainWindow(QtGui.QMainWindow):

    show_status = QtCore.pyqtSignal(bool, str)
    
    def __init__(self, sysbus, parent=None):
        QtGui.QWidget.__init__(self, parent)

        # Init ourself from the Designer UI
        self.ui = Ui_MainWindow()
        self.ui.setupUi(self)
        # light up the admin interface
        self.admin = AdminInterface(SERVICE,
                                    '/org/ganesha/nfsd/admin',
                                    sysbus,
                                    self.show_status)
        self.exportmgr = ExportMgr(SERVICE,
                                   '/org/ganesha/nfsd/ExportMgr',
                                   sysbus,
                                   self.show_status)
        self.clientmgr = ClientMgr(SERVICE,
                                   '/org/ganesha/nfsd/ClientMgr',
                                   sysbus,
                                   self.show_status)
        self.logmanager = LogManager(SERVICE,
                                     sysbus,
                                     self.show_status)
        self.logdialog = LogSetDialog(self.logmanager)
        self.show_status.connect(self.status_message)

        # Connect up the ui menubar
        #File
        self.ui.actionDBus_connect.triggered.connect(self.connect_gsh)
        self.ui.actionQuit.triggered.connect(self.quit)
        #Manage
        #Manage->Clients
        self.ui.actionAdd_Client.triggered.connect(self.add_client)
        self.ui.actionRemove_Client.triggered.connect(self.remove_client)
        #Manage->Exports
        self.ui.actionExports.triggered.connect(self.export_mgr)
        #Manage->Log Levels
        self.ui.actionLog_Settings.triggered.connect(self.logsettings)
        #Manage->Admin
        self.ui.actionReset_Grace.triggered.connect(self.reset_grace)
        self.ui.actionShutdown.triggered.connect(self.shutdown)
        self.ui.actionReload.triggered.connect(self.reload)
        #View
        self.ui.actionStatistics.triggered.connect(self.stats)
        self.ui.actionViewExports.triggered.connect(self.view_exports)
        self.ui.actionViewClients.triggered.connect(self.view_clients)
        #Help
        self.ui.actionAbout.triggered.connect(self.help)

        # Dbus data models
        self.exports_show_model = ExportTableModel(self.exportmgr)
        self.clients_show_model = ClientTableModel(self.clientmgr)
        
        # Tabs, tables, and views
        self.ui.exports.setModel(self.exports_show_model)
        self.ui.exports.resizeColumnsToContents()
        self.ui.exports.verticalHeader().setVisible(False)
        self.ui.clients.setModel(self.clients_show_model)
        self.ui.clients.resizeColumnsToContents()
        self.ui.clients.verticalHeader().setVisible(False)

    # actions to real work...
    def quit(self):
        self.statusBar().showMessage("Bye bye kiddies, quitting")
        quit()

    def connect_gsh(self):
        self.statusBar().showMessage("Connecting to nfs-ganesha...")
        
    def add_client(self):
        ipaddr, ok = QtGui.QInputDialog.getText(self,
                                                'Add a Client',
                                                'IP Address (N.N.N.N) of client: ')
        if ok:
            self.clientmgr.AddClient(ipaddr)

    def remove_client(self):
        ipaddr, ok = QtGui.QInputDialog.getText(self,
                                                'Remove a Client',
                                                'IP Address (N.N.N.N) of client: ')
        if ok:
            self.clientmgr.RemoveClient(ipaddr)

    def export_mgr(self):
        self.statusBar().showMessage("Export manager")
        
    def logsettings(self):
        self.logdialog.show_logsetting_dialog()

    def reset_grace(self):
        ipaddr, ok = QtGui.QInputDialog.getText(self,
                                                'Grace Period',
                                                'IP Address (N.N.N.N) of client: ')
        if ok:
            self.admin.grace(ipaddr)

    def shutdown(self):
        reply = QtGui.QMessageBox.question(self,
                                           'Warning!!!',
                                           "Do you really want to shut down the server?",
                                           QtGui.QMessageBox.Yes |
                                           QtGui.QMessageBox.No,
                                           QtGui.QMessageBox.No)
        if reply == QtGui.QMessageBox.Yes:
            self.admin.shutdown()
        
    def reload(self):
        reply = QtGui.QMessageBox.question(self,
                                           'Warning!!!',
                                           "Do you really want to reload exports?",
                                           QtGui.QMessageBox.Yes |
                                           QtGui.QMessageBox.No,
                                           QtGui.QMessageBox.No)
        if reply == QtGui.QMessageBox.Yes:
            self.admin.reload()

    def stats(self):
        self.statusBar().showMessage("stats go here")
        
    def view_exports(self):
        self.exports_show_model.FetchExports()
        
    def view_clients(self):
        self.clients_show_model.FetchClients()
        
    def help(self):
        self.statusBar().showMessage("Help! Help!!")

    def status_message(self, status, errormsg):
        if status:
            str = "Success: "
        else:
            str = "Failed: "
        self.statusBar().showMessage(str + errormsg)

# Main
if __name__ == '__main__':
    app = QtGui.QApplication(sys.argv)
    sysbus = QtDBus.QDBusConnection.systemBus()
    mw = MainWindow(sysbus)
    mw.show()
    sys.exit(app.exec_())
