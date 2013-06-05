#! /usr/bin/python
#-*- coding: utf-8 -*-

"""
NFS Ganesha administration tool
"""

import sys

from PyQt4 import QtCore, QtGui, QtDBus 
from ui_main_window import Ui_MainWindow
from admin import AdminInterface

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
        self.show_status.connect(self.status_message)

        # Connect up the ui menubar
        #File
        self.ui.actionDBus_connect.triggered.connect(self.connect_gsh)
        self.ui.actionQuit.triggered.connect(self.quit)
        #Manage
        self.ui.actionClients.triggered.connect(self.client_mgr)
        self.ui.actionExports.triggered.connect(self.export_mgr)
        self.ui.actionLog_Levels.triggered.connect(self.loglevels)
        self.ui.actionReset_Grace.triggered.connect(self.reset_grace)
        self.ui.actionShutdown.triggered.connect(self.shutdown)
        self.ui.actionReload.triggered.connect(self.reload)
        #View
        self.ui.actionStatistics.triggered.connect(self.stats)
        self.ui.actionViewExports.triggered.connect(self.view_exports)
        self.ui.actionViewClients.triggered.connect(self.view_clients)
        #Help
        self.ui.actionAbout.triggered.connect(self.help)
        
    # actions to real work...
    def quit(self):
        self.statusBar().showMessage("Bye bye kiddies, quitting")
        quit()

    def connect_gsh(self):
        self.statusBar().showMessage("Connecting to nfs-ganesha...")
        
    def client_mgr(self):
        self.statusBar().showMessage("Client manager")
        
    def export_mgr(self):
        self.statusBar().showMessage("Export manager")
        
    def loglevels(self):
        self.statusBar().showMessage("tweak log levels")

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
        self.statusBar().showMessage("show exports")
        
    def view_clients(self):
        self.statusBar().showMessage("show clients")
        
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
