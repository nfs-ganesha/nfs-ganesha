#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# ganesha_mgr_utils.py - commandline tool utils for managing nfs-ganesha.
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

import sys
import dbus
from collections import namedtuple

Client = namedtuple('Client',
                    ['ClientIP',
                     'HasNFSv3',
                     'HasMNT',
                     'HasNLM4',
                     'HasRQUOTA',
                     'HasNFSv40',
                     'HasNFSv41',
                     'HasNFSv42',
                     'Has9P',
                     'LastTime'])

class ClientMgr():

    def __init__(self, service, path, interface):
        self.dbus_service_name = service
        self.dbus_path = path
        self.dbus_interface = interface

        self.bus = dbus.SystemBus()
        try:
            self.dbusobj = self.bus.get_object(self.dbus_service_name,
                                               self.dbus_path)
        except:
            sys.exit("Error: Can't talk to ganesha service on d-bus." \
                     " Looks like Ganesha is down")

    def AddClient(self, ipaddr):
        add_client_method = self.dbusobj.get_dbus_method("AddClient",
                                                         self.dbus_interface)
        try:
            reply = add_client_method(ipaddr)
        except dbus.exceptions.DBusException as ex:
            return False, ex

        status = reply[0]
        msg = reply[1]
        return status, msg

    def RemoveClient(self, ipaddr):
        remove_client_method = self.dbusobj.get_dbus_method("RemoveClient",
                                                            self.dbus_interface)
        try:
            reply = remove_client_method(ipaddr)
        except dbus.exceptions.DBusException as ex:
            return False, ex

        status = reply[0]
        msg = reply[1]
        return status, msg

    def ShowClients(self):
        show_client_method = self.dbusobj.get_dbus_method("ShowClients",
                                                          self.dbus_interface)
        try:
            reply = show_client_method()
        except dbus.exceptions.DBusException as ex:
            return False, ex, []

        time = reply[0]
        client_array = reply[1]

        ts_ = (time[0], time[1])
        clients = []
        for client in client_array:
            cl_ = client
            lasttime = cl_[9]
            clt = Client(ClientIP=str(cl_[0]),
                         HasNFSv3=cl_[1],
                         HasMNT=cl_[2],
                         HasNLM4=cl_[3],
                         HasRQUOTA=cl_[4],
                         HasNFSv40=cl_[5],
                         HasNFSv41=cl_[6],
                         HasNFSv42=cl_[7],
                         Has9P=cl_[8],
                         LastTime=(lasttime[0],
                                   lasttime[1]))
            clients.append(clt)
        return True, "Done", [ts_, clients]



Export = namedtuple('Export',
                    ['ExportID',
                     'ExportPath',
                     'HasNFSv3',
                     'HasMNT',
                     'HasNLM4',
                     'HasRQUOTA',
                     'HasNFSv40',
                     'HasNFSv41',
                     'HasNFSv42',
                     'Has9P',
                     'LastTime'])

ExportClient = namedtuple('ExportClient',
                          ['Client_type',
                           'CIDR_version',
                           'CIDR_address',
                           'CIDR_mask',
                           'CIDR_proto',
                           'Anonymous_uid',
                           'Anonymous_gid',
                           'Expire_time_attr',
                           'Options',
                           'Set'])


class ExportMgr():
    '''
    org.ganesha.nfsd.exportmgr
    '''
    def __init__(self, service, path, interface):
        self.dbus_service_name = service
        self.dbus_path = path
        self.dbus_interface = interface
        self.bus = dbus.SystemBus()
        try:
            self.dbusobj = self.bus.get_object(self.dbus_service_name,
                                               self.dbus_path)
        except:
            sys.exit("Error: Can't talk to ganesha service on d-bus." \
                     " Looks like Ganesha is down")

    def AddExport(self, conf_path, exp_expr):
        add_export_method = self.dbusobj.get_dbus_method("AddExport",
                                                         self.dbus_interface)
        try:
            msg = add_export_method(conf_path, exp_expr)
        except dbus.exceptions.DBusException as ex:
            return False, ex

        return True, "Done: "+msg

    def UpdateExport(self, conf_path, exp_expr):
        update_export_method = self.dbusobj.get_dbus_method("UpdateExport",
                                                            self.dbus_interface)
        try:
            msg = update_export_method(conf_path, exp_expr)
        except dbus.exceptions.DBusException as ex:
            return False, ex

        return True, "Done: "+msg

    def RemoveExport(self, exp_id):
        rm_export_method = self.dbusobj.get_dbus_method("RemoveExport",
                                                        self.dbus_interface)
        try:
            rm_export_method(int(exp_id))
        except dbus.exceptions.DBusException as ex:
            return False, ex
        return True, "Done"

    def DisplayExport(self, exp_id):
        display_export_method = self.dbusobj.get_dbus_method("DisplayExport",
                                                             self.dbus_interface)
        try:
            id_, fullpath, pseudopath, tag, clients_array = \
                display_export_method(int(exp_id))
        except dbus.exceptions.DBusException as ex:
            return False, ex, []

        export_clients = []
        for client in clients_array:
            c_ = ExportClient(Client_type=client[0],
                              CIDR_version=client[1],
                              CIDR_address=client[2],
                              CIDR_mask=client[3],
                              CIDR_proto=client[4],
                              Anonymous_uid=client[5],
                              Anonymous_gid=client[6],
                              Expire_time_attr=client[7],
                              Options=client[8],
                              Set=client[9])

            export_clients.append(c_)

        return True, "Done", [id_, fullpath, pseudopath, tag, export_clients]

    def ShowExports(self):
        show_export_method = self.dbusobj.get_dbus_method("ShowExports",
                                                          self.dbus_interface)
        try:
            reply = show_export_method()
        except dbus.exceptions.DBusException as ex:
            return False, ex, []

        time = reply[0]
        export_array = reply[1]

        ts_ = (time[0], time[1])
        exports = []
        for export in export_array:
            ex = export
            lasttime = ex[10]
            exp = Export(ExportID=ex[0],
                         ExportPath=str(ex[1]),
                         HasNFSv3=ex[2],
                         HasMNT=ex[3],
                         HasNLM4=ex[4],
                         HasRQUOTA=ex[5],
                         HasNFSv40=ex[6],
                         HasNFSv41=ex[7],
                         HasNFSv42=ex[8],
                         Has9P=ex[9],
                         LastTime=(lasttime[0],
                                   lasttime[1]))
            exports.append(exp)
        return True, "Done", [ts_, exports]

class AdminInterface():
    '''
    org.ganesha.nfsd.admin interface
    '''
    def __init__(self, service, path, interface):
        self.dbus_service_name = service
        self.dbus_path = path
        self.dbus_interface = interface

        self.bus = dbus.SystemBus()
        try:
            self.dbusobj = self.bus.get_object(self.dbus_service_name,
                                               self.dbus_path)
        except:
            sys.exit("Error: Can't talk to ganesha service on d-bus." \
                     " Looks like Ganesha is down")

    def grace(self, ipaddr):
        grace_method = self.dbusobj.get_dbus_method("grace",
                                                    self.dbus_interface)
        try:
            reply = grace_method(ipaddr)
        except dbus.exceptions.DBusException as ex:
            return False, ex

        status = reply[0]
        msg = reply[1]
        return status, msg

    def shutdown(self):
        shutdown_method = self.dbusobj.get_dbus_method("shutdown",
                                                       self.dbus_interface)
        try:
            reply = shutdown_method()
        except dbus.exceptions.DBusException as ex:
            return False, ex

        status = reply[0]
        msg = reply[1]
        return status, msg

    def purge_netgroups(self):
        method = self.dbusobj.get_dbus_method("purge_netgroups",
                                              self.dbus_interface)
        try:
            reply = method()
        except dbus.exceptions.DBusException as ex:
            return False, ex

        status = reply[0]
        msg = reply[1]
        return status, msg

    def purge_idmap(self):
        method = self.dbusobj.get_dbus_method("purge_idmapper_cache",
                                              self.dbus_interface)
        try:
            reply = method()
        except dbus.exceptions.DBusException as ex:
            return False, ex

        status = reply[0]
        msg = reply[1]
        return status, msg

    def purge_gids(self):
        method = self.dbusobj.get_dbus_method("purge_gids",
                                              self.dbus_interface)
        try:
            reply = method()
        except dbus.exceptions.DBusException as ex:
            return False, ex

        status = reply[0]
        msg = reply[1]
        return status, msg

    def GetAll(self):
        method = self.dbusobj.get_dbus_method("GetAll",
                                              "org.freedesktop.DBus.Properties")
        try:
            dictionary = method(self.dbus_interface)
        except dbus.exceptions.DBusException as ex:
            return False, ex, {}

        prop_dict = {}
        for key in dictionary.keys():
            prop_dict[key] = dictionary[key]
        return True, "Done", prop_dict


IDMapper = namedtuple('IDMapper', ['Name', 'UID', 'HasGID', 'GID'])

FileSys = namedtuple('FileSys', ['Path', 'MajorDevId', 'MinorDevId'])

class CacheMgr():
    '''
    org.ganesha.nfsd.cachemgr
    '''
    def __init__(self, service, path, interface):
        self.dbus_service_name = service
        self.dbus_path = path
        self.dbus_interface = interface

        self.bus = dbus.SystemBus()
        try:
            self.dbusobj = self.bus.get_object(self.dbus_service_name,
                                               self.dbus_path)
        except:
            sys.exit("Error: Can't talk to ganesha service on d-bus." \
                     " Looks like Ganesha is down")

    def ShowFileSys(self):
        show_filesys_method = self.dbusobj.get_dbus_method("showfs",
                                                           self.dbus_interface)
        try:
            reply = show_filesys_method()
        except dbus.exceptions.DBusException as ex:
            return False, ex, []

        time = reply[0]
        fs_array = reply[1]

        ts_ = (time[0], time[1])

        fss = []
        for fs_ in fs_array:
            filesys1 = FileSys(Path=str(fs_[0]),
                               MajorDevId=fs_[1],
                               MinorDevId=fs_[2])
            fss.append(filesys1)
        return True, "Done", [ts_, fss]


    def ShowIdmapper(self):
        show_id_method = self.dbusobj.get_dbus_method("showidmapper",
                                                      self.dbus_interface)
        try:
            reply = show_id_method()
        except dbus.exceptions.DBusException as ex:
            return False, ex, []

        time = reply[0]
        id_array = reply[1]

        ts_ = (time[0], time[1])

        ids = []
        for entry in id_array:
            entry1 = IDMapper(Name=str(entry[0]),
                              UID=entry[1],
                              HasGID=entry[2],
                              GID=entry[3])
            ids.append(entry1)
        return True, "Done", [ts_, ids]


LOGGER_PROPS = 'org.ganesha.nfsd.log.component'

class LogManager():
    '''
    org.ganesha.nfsd.log.component
    '''

    def __init__(self, service, path, interface):
        self.dbus_service_name = service
        self.dbus_path = path
        self.dbus_interface = interface

        self.bus = dbus.SystemBus()
        try:
            self.dbusobj = self.bus.get_object(self.dbus_service_name,
                                               self.dbus_path)
        except:
            sys.exit("Error: Can't talk to ganesha service on d-bus. " \
                     " Looks like Ganesha is down")

    def GetAll(self):
        getall_method = self.dbusobj.get_dbus_method("GetAll",
                                                     self.dbus_interface)
        try:
            dictionary = getall_method(LOGGER_PROPS)
        except dbus.exceptions.DBusException as ex:
            return False, ex, {}

        prop_dict = {}
        for key in dictionary.keys():
            prop_dict[key] = dictionary[key]
        return True, "Done", prop_dict

    def Get(self, prop):
        get_method = self.dbusobj.get_dbus_method("Get",
                                                  self.dbus_interface)
        try:
            level = get_method(LOGGER_PROPS, prop)
        except dbus.exceptions.DBusException as ex:
            return False, ex, 0

        return True, "Done", level

    def Set(self, prop, setval):
        set_method = self.dbusobj.get_dbus_method("Set",
                                                  self.dbus_interface)
        try:
            set_method(LOGGER_PROPS, prop, setval)
        except dbus.exceptions.DBusException as ex:
            return False, ex

        return True, "Done"
