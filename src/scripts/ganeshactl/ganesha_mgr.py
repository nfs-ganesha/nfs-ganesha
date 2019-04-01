#!/usr/bin/python2
#
# ganesha_mgr.py - commandline tool for managing nfs-ganesha.
#
# Copyright (C) 2014 IBM.
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
# Author: Allison Henderson <achender@vnet.linux.ibm.com>
#-*- coding: utf-8 -*-
from __future__ import print_function
import sys
import time
from Ganesha.ganesha_mgr_utils import ClientMgr
from Ganesha.ganesha_mgr_utils import ExportMgr
from Ganesha.ganesha_mgr_utils import AdminInterface
from Ganesha.ganesha_mgr_utils import LogManager

SERVICE = 'org.ganesha.nfsd'

class ManageClients():

    def __init__(self, parent=None):
        self.clientmgr = ClientMgr(SERVICE,
                                   '/org/ganesha/nfsd/ClientMgr',
                                   'org.ganesha.nfsd.clientmgr')

    def status_message(self, status, errormsg):
        print("Returns: status = %s, %s" % (str(status), errormsg))

    def addclient(self, ipaddr):
        print("Add a client %s" % (ipaddr))
        status, errormsg = self.clientmgr.AddClient(ipaddr)
        self.status_message(status, errormsg)

    def removeclient(self, ipaddr):
        print("Remove a client %s" % (ipaddr))
        status, errormsg = self.clientmgr.RemoveClient(ipaddr)
        self.status_message(status, errormsg)

    def showclients(self):
        print("Show clients")
        status, errormsg, reply = self.clientmgr.ShowClients()
        if status == True:
           ts = reply[0]
           clients = reply[1]
           self.proc_clients(ts, clients)
        else:
           self.status_message(status, errormsg)

    def proc_clients(self, ts, clients):
        print("Timestamp: ", time.ctime(ts[0]), ts[1], " nsecs")
        if len(clients) == 0:
            print("No clients")
        else:
            print("Clients:")
            print(" IP addr,  nfsv3, mnt, nlm4, rquota,nfsv40, nfsv41, nfsv42, 9p, last")
            for client in clients:
                print(" %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s, %s %d nsecs" %
                       (client.ClientIP,
                        client.HasNFSv3,
                        client.HasMNT,
                        client.HasNLM4,
                        client.HasRQUOTA,
                        client.HasNFSv40,
                        client.HasNFSv41,
                        client.HasNFSv42,
                        client.Has9P,
                        time.ctime(client.LastTime[0]), client.LastTime[1]))

class ShowExports():

    def __init__(self, parent=None):
        self.exportmgr = ExportMgr(SERVICE,
                                   '/org/ganesha/nfsd/ExportMgr',
                                   'org.ganesha.nfsd.exportmgr')
    def showexports(self):
        print("Show exports")
        status, msg, reply = self.exportmgr.ShowExports()
        if status == True:
           ts = reply[0]
           exports = reply[1]
           self.proc_exports(ts, exports)
        else:
           self.status_message(status, msg)

    def addexport(self, conf_path, exp_expr):
        print("Add Export in %s" % conf_path)
        status, msg = self.exportmgr.AddExport(conf_path, exp_expr)
        self.status_message(status, msg)

    def removeexport(self, exp_id):
        print("Remove Export with id %d" % int(exp_id))
        self.exportmgr.RemoveExport(exp_id)

    def updateexport(self, conf_path, exp_expr):
        print("Update Export in %s" % conf_path)
        status, msg = self.exportmgr.UpdateExport(conf_path, exp_expr)
        self.status_message(status, msg)

    def displayexport(self, exp_id):
        print("Display export with id %d" % int(exp_id))
        status, msg, reply = self.exportmgr.DisplayExport(exp_id)
        if status == True:
           id = reply[0]
           path = reply[1]
           pseudo = reply[2]
           tag = reply[3]
           self.proc_export(id, path, pseudo, tag)
        else:
           self.status_message(status, msg)

    def proc_export(self, id, path, pseudo, tag):
        print("export %d: path = %s, pseudo = %s, tag = %s" % (id, path, pseudo, tag))

    def proc_exports(self, ts, exports):
        print("Timestamp: ", time.ctime(ts[0]), ts[1], " nsecs")
        if len(exports) == 0:
            print("No exports")
        else:
            print("Exports:")
            print("  Id, path,    nfsv3, mnt, nlm4, rquota,nfsv40, nfsv41, nfsv42, 9p, last")
            for export in exports:
                print(" %d,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s,  %s, %s, %d nsecs" %
                       (export.ExportID,
                        export.ExportPath,
                        export.HasNFSv3,
                        export.HasMNT,
                        export.HasNLM4,
                        export.HasRQUOTA,
                        export.HasNFSv40,
                        export.HasNFSv41,
                        export.HasNFSv42,
                        export.Has9P,
                        time.ctime(export.LastTime[0]), export.LastTime[1]))

    def status_message(self, status, errormsg):
        print("Returns: status = %s, %s" % (str(status), errormsg))


class ServerAdmin():

    def __init__(self, parent=None):
        self.admin = AdminInterface(SERVICE,
                                    '/org/ganesha/nfsd/admin',
                                    'org.ganesha.nfsd.admin')
    def shutdown(self):
        print("Shutting down server.")
        status, msg = self.admin.shutdown()
        self.status_message(status, msg)

    def grace(self, ipaddr):
        print("Start grace period.")
        status, msg = self.admin.grace(ipaddr)
        self.status_message(status, msg)

    def purge_netgroups(self):
        print("Purging netgroups cache")
        status, msg = self.admin.purge_netgroups()
        self.status_message(status, msg)

    def purge_idmap(self):
        print("Purging idmapper cache")
        status, msg = self.admin.purge_idmap()
        self.status_message(status, msg)

    def purge_gids(self):
        print("Purging gids cache")
        status, msg = self.admin.purge_gids()
        self.status_message(status, msg)

    def status_message(self, status, errormsg):
        print("Returns: status = %s, %s" % (str(status), errormsg))

class ManageLogs():

    def __init__(self, parent=None):
        self.logmgr = LogManager(SERVICE,
                                 '/org/ganesha/nfsd/admin',
                                 'org.freedesktop.DBus.Properties')

    def set(self, property, value):
        print("Set log %s to %s" % (property, value))
        status, msg = self.logmgr.Set(property, value)
        self.status_message(status, msg)

    def get(self, property):
        print("Get property %s" % (property))
        status, msg, level = self.logmgr.Get(property)
        if status == True:
           self.show_loglevel(level)
        else:
           self.status_message(status, msg)

    def getall(self):
        print("Get all")
        status, msg, properties = self.logmgr.GetAll()
        if status == True:
           self.print_components(properties)
        else:
           self.status_message(status, msg)

    def show_loglevel(self, level):
        print("Log level: %s"% (str(level)))

    def status_message(self, status, errormsg):
        print("Returns: status = %s, %s" % (str(status), errormsg))

    def print_components(self, properties):
       for prop in properties:
          print(str(prop))

# Main
if __name__ == '__main__':
    exportmgr = ShowExports()
    clientmgr = ManageClients()
    ganesha = ServerAdmin()
    logmgr = ManageLogs()

    USAGE = \
       "\nganesha_mgr.py command [OPTIONS]\n\n"                                \
       "COMMANDS\n\n"                                                        \
       "   add_client ipaddr: Adds the client with the given IP\n\n"         \
       "   remove_client ipaddr: Removes the client with the given IP\n\n"   \
       "   show_client: Shows the current clients\n\n"                       \
       "   display_export export_id: \n"                                     \
       "      Displays the export with the given ID\n\n"                     \
       "   show_exports: Displays all current exports\n\n"                   \
       "   add_export conf expr:\n"                                          \
       "      Adds an export from the given config file that contains\n"     \
       "      the given expression\n"                                        \
       "      Example: \n"                                                   \
       "      add_export /etc/ganesha/gpfs.conf \"EXPORT(Export_ID=77)\"\n\n"\
       "   remove_export id: Removes the export with the given id    \n\n"   \
       "   update_export conf expr:\n"                                       \
       "      Updates an export from the given config file that contains\n"  \
       "      the given expression\n"                                        \
       "      Example: \n"                                                   \
       "      update_export /etc/ganesha/gpfs.conf \"EXPORT(Export_ID=77)\"\n\n"\
       "   shutdown: Shuts down the ganesha nfs server\n\n"                  \
       "   purge netgroups: Purges netgroups cache\n\n"                      \
       "   purge idmap: Purges idmapper cache\n\n"                      \
       "   purge gids: Purges gids cache\n\n"                      \
       "   grace ipaddr: Begins grace for the given IP\n\n"                  \
       "   get_log component: Gets the log level for the given component\n\n"\
       "   set_log component level: \n"                                      \
       "       Sets the given log level to the given component\n\n"          \
       "   getall_logs: Prints all log components\n\n"
    if len(sys.argv) < 2:
       print("Too few arguments."\
             " Try \"ganesha_mgr.py help\" for more info")
       sys.exit(1)

    elif sys.argv[1] == "add_client":
        if len(sys.argv) < 3:
           print("add_client requires an IP."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        clientmgr.addclient(sys.argv[2])
    elif sys.argv[1] == "remove_client":
        if len(sys.argv) < 3:
           print("remove_client requires an IP."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        clientmgr.removeclient(sys.argv[2])
    elif sys.argv[1] == "show_client":
        clientmgr.showclients()

    elif sys.argv[1] == "add_export":
        if len(sys.argv) < 4:
           print("add_export requires a config file and an expression."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        exportmgr.addexport(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "remove_export":
        if len(sys.argv) < 3:
           print("remove_export requires an export ID."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        exportmgr.removeexport(sys.argv[2])
    elif sys.argv[1] == "update_export":
        if len(sys.argv) < 4:
           print("update_export requires a config file and an expression."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        exportmgr.updateexport(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "display_export":
        if len(sys.argv) < 3:
           print("display_export requires an export ID."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        exportmgr.displayexport(sys.argv[2])
    elif sys.argv[1] == "show_exports":
        exportmgr.showexports()

    elif sys.argv[1] == "shutdown":
        ganesha.shutdown()
    elif sys.argv[1] == "purge":
        if len(sys.argv) < 3:
            msg = 'purge requires a cache name to purge, '
            msg += 'Try "ganesha_mgr.py help" for more info'
            sys.exit(msg)
        if sys.argv[2] == "netgroups":
            ganesha.purge_netgroups()
        elif sys.argv[2] == "idmap":
            ganesha.purge_idmap()
        elif sys.argv[2] == "gids":
            ganesha.purge_gids()
        else:
            msg = "Purging '%s' is not supported" % sys.argv[2]
            sys.exit(msg)

    elif sys.argv[1] == "grace":
        if len(sys.argv) < 3:
           print("grace requires an IP."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        ganesha.grace(sys.argv[2])

    elif sys.argv[1] == "set_log":
        if len(sys.argv) < 4:
           print("set_log requires a component and a log level."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        logmgr.set(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "get_log":
        if len(sys.argv) < 3:
           print("get_log requires a component."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        logmgr.get(sys.argv[2])
    elif sys.argv[1] == "getall_logs":
        logmgr.getall()

    elif sys.argv[1] == "help":
       print(USAGE)

    else:
        print("Unknown/missing command."\
              " Try \"ganesha_mgr.py help\" for more info")
