#!/usr/bin/python3
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
from Ganesha.ganesha_mgr_utils import CacheMgr

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
            _ts = reply[0]
            clients = reply[1]
            self.proc_clients(_ts, clients)
        else:
            self.status_message(status, errormsg)

    def proc_clients(self, _ts, clients):
        print("Timestamp: ", time.ctime(_ts[0]), _ts[1], " nsecs")
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
            _ts = reply[0]
            exports = reply[1]
            self.proc_exports(_ts, exports)
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
            _id = reply[0]
            path = reply[1]
            pseudo = reply[2]
            tag = reply[3]
            clients = reply[4]
            self.proc_export(_id, path, pseudo, tag, clients)
        else:
            self.status_message(status, msg)

    def proc_export(self, _id, path, pseudo, tag, clients):
        print("export %d: path = %s, pseudo = %s, tag = %s" %\
              (_id, path, pseudo, tag))
        print(" Client type,  CIDR version, CIDR address, CIDR mask, " +\
              "CIDR proto, Anonymous UID, Anonymous GID, " +\
              "Attribute timeout, Options, Set")
        for client in clients:
            print(" %s,  %d,  %d,  %d,  %d,  %d,  %d,  %d,  %d, %d" %
                  (client.Client_type,
                   client.CIDR_version,
                   client.CIDR_address,
                   client.CIDR_mask,
                   client.CIDR_proto,
                   client.Anonymous_uid,
                   client.Anonymous_gid,
                   client.Expire_time_attr,
                   client.Options,
                   client.Set))

    def proc_exports(self, _ts, exports):
        print("Timestamp: ", time.ctime(_ts[0]), _ts[1], " nsecs")
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

    def show_version(self):
        status, msg, versions = self.admin.GetAll()
        if status:
            print("NFS-Ganesha Release = V{}".format(versions['VERSION_RELEASE']))
            try:
                print("ganesha compiled on {} at {}".format(versions['VERSION_COMPILE_DATE'],
                                                            versions['VERSION_COMPILE_TIME']))
                print("Release comment = {}".format(versions['VERSION_COMMENT']))
                print("Git HEAD = {}".format(versions['VERSION_GIT_HEAD']))
                print("Git Describe = {}".format(versions['VERSION_GIT_DESCRIBE']))
            except KeyError:
                pass
        else:
            self.status_message(status, msg)

    def trim_enable(self):
        status, msg = self.admin.trim_enable()
        self.status_message(status, msg)

    def trim_disable(self):
        status, msg = self.admin.trim_disable()
        self.status_message(status, msg)

    def trim_call(self):
        status, msg = self.admin.trim_call()
        self.status_message(status, msg)

    def trim_status(self):
        status, msg = self.admin.trim_status()
        self.status_message(status, msg)

    def status_message(self, status, errormsg):
        print("Returns: status = %s, %s" % (str(status), errormsg))

class ManageCache():

    def __init__(self, parent=None):
        self.cachemgr = CacheMgr(SERVICE,
                                 '/org/ganesha/nfsd/CacheMgr',
                                 'org.ganesha.nfsd.cachemgr')

    def status_message(self, status, errormsg):
        print("Returns: status = %s, %s" % (str(status), errormsg))

    def showfs(self):
        print("Show filesystems")
        status, errormsg, reply = self.cachemgr.ShowFileSys()
        if status == True:
            _ts = reply[0]
            fss = reply[1]
            self.proc_fs(_ts, fss)
        else:
            self.status_message(status, errormsg)

    def proc_fs(self, _ts, fss):
        print("Timestamp: ", time.ctime(_ts[0]), _ts[1], " nsecs")
        if len(fss) == 0:
            print("No filesystems")
        else:
            print("Filesystems:")
            print(" Path,  MajorDevId, MinorDevId")
            for _fs in fss:
                print(" %s,  %s,  %s" %
                      (_fs.Path,
                       _fs.MajorDevId,
                       _fs.MinorDevId))

    def showidmapper(self):
        print("Show idmapper cache")
        status, errormsg, reply = self.cachemgr.ShowIdmapper()
        if status == True:
            _ts = reply[0]
            ids = reply[1]
            self.proc_id(_ts, ids)
        else:
            self.status_message(status, errormsg)

    def proc_id(self, _ts, ids):
        print("Timestamp: ", time.ctime(_ts[0]), _ts[1], " nsecs")
        if len(ids) == 0:
            print("No entries in idmapper cache")
        else:
            print("Idmapper cache:")
            print(" Name,  UID, GID")
            for entry in ids:
                if entry.HasGID == True:
                    print(" %s,  %s,  %s" % (entry.Name, entry.UID, entry.GID))
                else:
                    print(" %s,  %s,  -" % (entry.Name, entry.UID))

class ManageLogs():

    def __init__(self, parent=None):
        self.logmgr = LogManager(SERVICE,
                                 '/org/ganesha/nfsd/admin',
                                 'org.freedesktop.DBus.Properties')

    def set(self, prop, value):
        print("Set log %s to %s" % (prop, value))
        status, msg = self.logmgr.Set(prop, value)
        self.status_message(status, msg)

    def get(self, prop):
        print("Get property %s" % (prop))
        status, msg, level = self.logmgr.Get(prop)
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
    cachemgr = ManageCache()

    USAGE = '''ganesha_mgr.py command [OPTIONS]
       COMMANDS
          add_client ipaddr
          add client ipaddr
              Adds the client with the given IP
          remove_client ipaddr
          remove client ipaddr
              Removes the client with the given IP
          show clients
              Displays the current clients
          show version
              Displays ganesha release information
          show posix_fs
              Displays the mounted POSIX filesystems
          show exports
              Displays all current exports
          show idmap
              Displays the idmapper cache
          display_export export_id
          display export export_id
              Displays the export with the given ID
          add_export conf expr
          add export conf expr
              Adds an export from the given config file that contains
              the given expression
              Example:
              add_export /etc/ganesha/gpfs.conf \"EXPORT(Export_ID=77)\"
          remove_export id
          remove export id
              Removes the export with the given id
          update_export conf expr
              Updates an export from the given config file that contains
              the given expression
              Example:
              update_export /etc/ganesha/gpfs.conf \"EXPORT(Export_ID=77)\"
          shutdown
              Shuts down the ganesha nfs server
          purge netgroups
              Purges netgroups cache
          purge idmap
              Purges idmapper cache
          purge gids
              Purges gids cache
          grace ipaddr
              Begins grace for the given IP
          trim enable
              Enable malloc trim
          trim disable
              Disable malloc trim
          trim call
              Call malloc trim
          trim status
              Get current malloc trim status
          get_log component
          get log component
              Gets the log level for the given component
          set_log component level
          set log component level
              Sets the given log level to the given component
          getall_logs
          getall logs
              Prints all log components
       '''

    if len(sys.argv) < 2:
        print("Too few arguments."\
              " Try \"ganesha_mgr.py help\" for more info")
        sys.exit(1)

    # add
    elif sys.argv[1] == "add_client":
        if len(sys.argv) < 3:
           print("add_client requires an IP."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        clientmgr.addclient(sys.argv[2])
    elif sys.argv[1] == "add_export":
        if len(sys.argv) < 4:
           print("add_export requires a config file and an expression."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        exportmgr.addexport(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "add":
        if len(sys.argv) < 3:
            msg = 'add requires an option, '
            msg += 'Try "ganesha_mgr.py help" for more info'
            sys.exit(msg)
        if sys.argv[2] == "client":
            if len(sys.argv) < 4:
                print("add client requires an IP."\
                      " Try \"ganesha_mgr.py help\" for more info")
                sys.exit(1)
            clientmgr.addclient(sys.argv[3])
        elif sys.argv[2] == "export":
            if len(sys.argv) < 5:
                print("add export requires a config file and an expression."\
                      " Try \"ganesha_mgr.py help\" for more info")
                sys.exit(1)
            exportmgr.addexport(sys.argv[3], sys.argv[4])
        else:
            msg = "Adding '%s' is not supported" % sys.argv[2]
            sys.exit(msg)

    # remove
    elif sys.argv[1] == "remove_client":
        if len(sys.argv) < 3:
           print("remove_client requires an IP."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        clientmgr.removeclient(sys.argv[2])
    elif sys.argv[1] == "remove_export":
        if len(sys.argv) < 3:
           print("remove_export requires an export ID."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        exportmgr.removeexport(sys.argv[2])
    elif sys.argv[1] == "remove":
        if len(sys.argv) < 3:
            msg = 'remove requires an option, '
            msg += 'Try "ganesha_mgr.py help" for more info'
            sys.exit(msg)
        if sys.argv[2] == "client":
            if len(sys.argv) < 4:
                print("remove client requires an IP."\
                      " Try \"ganesha_mgr.py help\" for more info")
                sys.exit(1)
            clientmgr.removeclient(sys.argv[3])
        elif sys.argv[2] == "export":
            if len(sys.argv) < 4:
                print("remove export requires an export ID."\
                      " Try \"ganesha_mgr.py help\" for more info")
                sys.exit(1)
            exportmgr.removeexport(sys.argv[3])
        else:
            msg = "Removing '%s' is not supported" % sys.argv[2]
            sys.exit(msg)

    # update
    elif sys.argv[1] == "update_export":
        if len(sys.argv) < 4:
           print("update_export requires a config file and an expression."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        exportmgr.updateexport(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "update":
        if len(sys.argv) < 3:
            msg = 'update requires an option, '
            msg += 'Try "ganesha_mgr.py help" for more info'
            sys.exit(msg)
        if sys.argv[2] == "export":
            if len(sys.argv) < 5:
                print("update export requires a config file and an expression."\
                      " Try \"ganesha_mgr.py help\" for more info")
                sys.exit(1)
            exportmgr.updateexport(sys.argv[3], sys.argv[4])
        else:
            msg = "Updating '%s' is not supported" % sys.argv[2]
            sys.exit(msg)

    # display
    elif sys.argv[1] == "display_export":
        if len(sys.argv) < 3:
           print("display_export requires an export ID."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        exportmgr.displayexport(sys.argv[2])
    elif sys.argv[1] == "display":
        if len(sys.argv) < 3:
            msg = 'display requires an option, '
            msg += 'Try "ganesha_mgr.py help" for more info'
            sys.exit(msg)
        if sys.argv[2] == "export":
            if len(sys.argv) < 4:
                print("display export requires an export ID."\
                      " Try \"ganesha_mgr.py help\" for more info")
                sys.exit(1)
            exportmgr.displayexport(sys.argv[3])
        else:
            msg = "Displaying '%s' is not supported" % sys.argv[2]
            sys.exit(msg)

    # purge
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

    # show
    elif sys.argv[1] == "show_exports":
        exportmgr.showexports()
    elif sys.argv[1] == "show_client":
        clientmgr.showclients()
    elif sys.argv[1] == "show":
        if len(sys.argv) < 3:
            msg = 'show requires an option, '
            msg += 'Try "ganesha_mgr.py help" for more info'
            sys.exit(msg)
        if sys.argv[2] == "clients":
            clientmgr.showclients()
        elif sys.argv[2] == "version":
            ganesha.show_version()
        elif sys.argv[2] == "exports":
            exportmgr.showexports()
        elif sys.argv[2] == "posix_fs":
            cachemgr.showfs()
        elif sys.argv[2] == "idmap":
            cachemgr.showidmapper()
        else:
            msg = "Showing '%s' is not supported" % sys.argv[2]
            sys.exit(msg)

    # grace
    elif sys.argv[1] == "grace":
        if len(sys.argv) < 3:
            print("grace requires an IP."\
                  " Try \"ganesha_mgr.py help\" for more info")
            sys.exit(1)
        ganesha.grace(sys.argv[2])

    # set
    elif sys.argv[1] == "set":
        if sys.argv[2] == "log":
            if len(sys.argv) < 5:
                print("set log requires a component and a log level."\
                      " Try \"ganesha_mgr.py help\" for more info")
                sys.exit(1)
            logmgr.set(sys.argv[3], sys.argv[4])
        else:
            msg = "Setting '%s' is not supported" % sys.argv[2]
            sys.exit(msg)

    elif sys.argv[1] == "trim":
        if len(sys.argv) < 3:
            print("trim requires enable/disable/call/status arg. "
                  "Try \"ganesha_mgr.py help\" for more info")
            sys.exit(1)

        if sys.argv[2] == 'enable':
            ganesha.trim_enable()
        elif sys.argv[2] == 'disable':
            ganesha.trim_disable()
        elif sys.argv[2] == 'status':
            ganesha.trim_status()
        elif sys.argv[2] == 'call':
            ganesha.trim_call()
        else:
            msg = "trim '%s' is unknown" % sys.argv[2]
            sys.exit(msg)

    # set
    elif sys.argv[1] == "set_log":
        if len(sys.argv) < 4:
           print("set_log requires a component and a log level."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        logmgr.set(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "set":
        if len(sys.argv) < 3:
            msg = 'set requires an option, '
            msg += 'Try "ganesha_mgr.py help" for more info'
            sys.exit(msg)
        if sys.argv[2] == "log":
            if len(sys.argv) < 5:
                print("set log requires a component and a log level."\
                      " Try \"ganesha_mgr.py help\" for more info")
                sys.exit(1)
            logmgr.set(sys.argv[3], sys.argv[4])
        else:
            msg = "Setting '%s' is not supported" % sys.argv[2]
            sys.exit(msg)
 
    # get
    elif sys.argv[1] == "get_log":
        if len(sys.argv) < 3:
           print("get_log requires a component."\
                 " Try \"ganesha_mgr.py help\" for more info")
           sys.exit(1)
        logmgr.get(sys.argv[2])
    elif sys.argv[1] == "get":
        if len(sys.argv) < 3:
            msg = 'get requires an option, '
            msg += 'Try "ganesha_mgr.py help" for more info'
            sys.exit(msg)
        if sys.argv[2] == "log":
            if len(sys.argv) < 4:
                print("get log requires a component."\
                      " Try \"ganesha_mgr.py help\" for more info")
                sys.exit(1)
            logmgr.get(sys.argv[3])
        else:
            msg = "Getting '%s' is not supported" % sys.argv[2]
            sys.exit(msg)

    # getall
    elif sys.argv[1] == "getall_logs":
        logmgr.getall()
    elif sys.argv[1] == "getall":
        if len(sys.argv) < 3:
            msg = 'getall requires an option, '
            msg += 'Try "ganesha_mgr.py help" for more info'
            sys.exit(msg)
        if sys.argv[2] == "logs":
            logmgr.getall()
        else:
            msg = "Getall'%s' is not supported" % sys.argv[2]
            sys.exit(msg)

    # others
    elif sys.argv[1] == "shutdown":
        ganesha.shutdown()

    elif sys.argv[1] == "help":
        print(USAGE)

    else:
        print("Unknown/missing command."\
              " Try \"ganesha_mgr.py help\" for more info")
        sys.exit(1)
