#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
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
import os
import sys
import time
from Ganesha.ganesha_mgr_utils import ClientMgr
from Ganesha.ganesha_mgr_utils import ExportMgr
from Ganesha.ganesha_mgr_utils import AdminInterface
from Ganesha.ganesha_mgr_utils import LogManager
from Ganesha.ganesha_mgr_utils import CacheMgr
from Ganesha.ganesha_mgr_utils import CondLogManager

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

    def purge_idmapper(self):
        print("Purging idmapper cache")
        status, msg = self.admin.purge_idmapper()
        self.status_message(status, msg)

    def purge_idmapper_negative(self):
        print("Purging idmapper negative cache")
        status, msg = self.admin.purge_idmapper_negative()
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

    def showidmapper_users(self):
        print("Show idmapper users cache")
        status, errormsg, reply = self.cachemgr.ShowIdmapperUsers()
        if status == True:
            _ts = reply[0]
            ids = reply[1]
            self.proc_id(_ts, ids)
        else:
            self.status_message(status, errormsg)

    def showidmapper_groups(self):
        print("Show idmapper groups cache")
        status, errormsg, reply = self.cachemgr.ShowIdmapperGroups()
        if status == True:
            _ts = reply[0]
            ids = reply[1]
            self.proc_gid(_ts, ids)
        else:
            self.status_message(status, errormsg)

    def showidmapper_uid2grp(self):
        print("Show idmapper uid2grp cache")
        status, errormsg, reply = self.cachemgr.ShowIdmapperUid2grp()
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

    def proc_gid(self, _ts, ids):
        print("Timestamp: ", time.ctime(_ts[0]), _ts[1], " nsecs")
        if len(ids) == 0:
            print("No entries in idmapper cache")
        else:
            print("Idmapper cache:")
            print(" Name,  GID")
            for entry in ids:
                print(" %s,  %s" % (entry.Name, entry.GID))


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


def exit_try_help(msg):
    """Exit with error, printing message and suggesting a help command."""
    sys.exit(f'{msg}. Try "{prog} help" for more info')


def exit_option_not_supported(action):
    """Exit with error, indicating that option is not supported for action."""
    sys.exit(f'"{prog} {action} {sys.argv[2]}" is not supported')


def _parse_cond_config_flags(args):
    """Parse flag-style arguments for set log conditional_config.

    Returns (comp_level_pairs, clients, export_ids, policy, error).
      comp_level_pairs : list of ([component, ...], level_str) tuples
      clients          : list of IP/CIDR strings
      export_ids       : list of export-ID strings
      policy           : policy string or None
      error            : error message string, or None on success
    """
    comp_level_pairs = []
    clients = []
    export_ids = []
    policy = None
    pending_components = None
    i = 0
    while i < len(args):
        flag = args[i]
        if flag == '--components':
            if i + 1 >= len(args):
                return None, None, None, None, "--components requires a value"
            if pending_components is not None:
                return None, None, None, None, \
                    "--components given without a following --level for the previous group"
            pending_components = [c.strip() for c in args[i + 1].split(',')
                                  if c.strip()]
            i += 2
        elif flag == '--level':
            if i + 1 >= len(args):
                return None, None, None, None, "--level requires a value"
            if pending_components is None:
                return None, None, None, None, \
                    "--level given without a preceding --components"
            comp_level_pairs.append((pending_components, args[i + 1].strip()))
            pending_components = None
            i += 2
        elif flag == '--clients':
            if i + 1 >= len(args):
                return None, None, None, None, "--clients requires a value"
            clients = [c.strip() for c in args[i + 1].split(',') if c.strip()]
            i += 2
        elif flag == '--export-ids':
            if i + 1 >= len(args):
                return None, None, None, None, "--export-ids requires a value"
            export_ids = [e.strip() for e in args[i + 1].split(',')
                          if e.strip()]
            i += 2
        elif flag == '--policy':
            if i + 1 >= len(args):
                return None, None, None, None, "--policy requires a value"
            policy = args[i + 1].strip()
            i += 2
        else:
            return None, None, None, None, f"Unknown option: {flag}"
    if pending_components is not None:
        return None, None, None, None, \
            "--components given without a following --level"
    return comp_level_pairs, clients, export_ids, policy, None


def _parse_reset_flags(args):
    """Parse flag-style arguments for reset log conditional_config.

    Returns (components, clients, export_ids, reset_policy, error).
      components   : list of component name strings (level always reset to default)
      clients      : list of IP/CIDR strings to remove
      export_ids   : list of export-ID strings to remove
      reset_policy : True if --policy flag was given, False otherwise
      error        : error message string, or None on success
    """
    components = []
    clients = []
    export_ids = []
    reset_policy = False
    i = 0
    while i < len(args):
        flag = args[i]
        if flag == '--components':
            if i + 1 >= len(args):
                return None, None, None, None, "--components requires a value"
            components = [c.strip() for c in args[i + 1].split(',')
                          if c.strip()]
            i += 2
        elif flag == '--clients':
            if i + 1 >= len(args):
                return None, None, None, None, "--clients requires a value"
            clients = [c.strip() for c in args[i + 1].split(',') if c.strip()]
            i += 2
        elif flag == '--export-ids':
            if i + 1 >= len(args):
                return None, None, None, None, "--export-ids requires a value"
            export_ids = [e.strip() for e in args[i + 1].split(',')
                          if e.strip()]
            i += 2
        elif flag == '--policy':
            reset_policy = True
            i += 1
        else:
            return None, None, None, None, f"Unknown option: {flag}"
    return components, clients, export_ids, reset_policy, None


def _normalize_cond_policy(policy_str):
    """Normalise policy to MATCH_ANY or MATCH_ALL; return None if invalid."""
    upper = policy_str.upper()
    if upper in ('ANY', 'MATCH_ANY'):
        return 'MATCH_ANY'
    if upper in ('ALL', 'MATCH_ALL'):
        return 'MATCH_ALL'
    return None


class ManageCondLogs():

    def __init__(self, parent=None):
        self.condlogmgr = CondLogManager(SERVICE,
                                         '/org/ganesha/nfsd/admin',
                                         'org.freedesktop.DBus.Properties')

    def set(self, prop, value):
        print("Set log %s to %s" % (prop, value))
        status, msg = self.condlogmgr.Set(prop, value)
        self.status_message(status, msg)

    def get(self, prop):
        print("Get property %s" % (prop))
        status, msg, level = self.condlogmgr.Get(prop)
        if status == True:
            self.show_loglevel(level)
        else:
            self.status_message(status, msg)

    def show_loglevel(self, level):
        print("Log level: %s"% (str(level)))

    def status_message(self, status, errormsg):
        print("Returns: status = %s, %s" % (str(status), errormsg))

    def print_components(self, properties):
        for prop in properties:
           print(str(prop))

    def show_conditional_clients(self):
        print("Show conditional logging clients")
        status, errormsg, clients = self.condlogmgr.ShowConditionalLogClientList()
        if status:
            print("Status: %s" % (errormsg))
            if len(clients) == 0:
                print("No conditional logging clients")
            else:
                print("Conditional logging clients:")
                for client in clients:
                    print("  %s" % (client))
        else:
            self.status_message(status, errormsg)

    def show_conditional_exports(self):
        print("Show conditional logging exports")
        status, errormsg, export_ids = self.condlogmgr.ShowConditionalLogExportList()
        if status:
            print("Status: %s" % (errormsg))
            if len(export_ids) == 0:
                print("No conditional logging exports")
            else:
                print("Conditional logging exports:")
                for export_id in export_ids:
                    print("  %d" % (export_id))
        else:
            self.status_message(status, errormsg)

    def show_conditional_match_policy(self):
        print("Show conditional logging match policy")
        status, errormsg = self.condlogmgr.ShowMatchPolicy()
        if status:
            print("%s" % (errormsg))
        else:
            self.status_message(status, errormsg)

    def update_conditional_match_policy(self, policy):
        print("Update conditional logging match policy to %s" % (policy))
        status, errormsg = self.condlogmgr.ChangeMatchPolicy(policy)
        print("%s" % (errormsg))

    def add_conditional_client(self, ipaddr):
        print("Add conditional logging client %s" % (ipaddr))
        status, errormsg = self.condlogmgr.ClientEnable(ipaddr)
        print("%s" % (errormsg))

    def add_conditional_export(self, export_id):
        print("Add conditional logging export %s" % (export_id))
        status, errormsg = self.condlogmgr.ExportEnable(export_id)
        print("%s" % (errormsg))

    def remove_conditional_client(self, ipaddr):
        print("Remove conditional logging client %s" % (ipaddr))
        status, errormsg = self.condlogmgr.ClientDisable(ipaddr)
        print("%s" % (errormsg))

    def remove_conditional_export(self, export_id):
        print("Remove conditional logging export %s" % (export_id))
        status, errormsg = self.condlogmgr.ExportDisable(export_id)
        print("%s" % (errormsg))

    # ------------------------------------------------------------------
    # New composite commands
    # ------------------------------------------------------------------

    def set_conditional_config(self, raw_args):
        """Handle: ganesha_mgr set log conditional_config [OPTIONS]"""
        comp_level_pairs, clients, export_ids, policy, err = \
            _parse_cond_config_flags(raw_args)
        if err:
            sys.exit(f"set log conditional_config: {err}")
        if not comp_level_pairs and not clients and not export_ids \
                and policy is None:
            sys.exit("set log conditional_config requires at least one option. "
                     f'Try "{prog} help" for more info')

        failed = False

        # 1. Component / level pairs (in order given)
        for comps, level in comp_level_pairs:
            for comp in comps:
                status, msg = self.condlogmgr.Set(comp, level)
                if status:
                    print(f"  set {comp} = {level}: OK")
                else:
                    print(f"  set {comp} = {level}: FAILED ({msg})")
                    failed = True

        # 2. Clients
        for client in clients:
            status, msg = self.condlogmgr.ClientEnable(client)
            if status:
                print(f"  client {client}: OK")
            else:
                print(f"  client {client}: FAILED ({msg})")
                failed = True

        # 3. Export IDs
        for eid in export_ids:
            status, msg = self.condlogmgr.ExportEnable(eid)
            if status:
                print(f"  export-id {eid}: OK")
            else:
                print(f"  export-id {eid}: FAILED ({msg})")
                failed = True

        # 4. Policy
        if policy is not None:
            normalised = _normalize_cond_policy(policy)
            if normalised is None:
                print(f"  policy {policy}: FAILED (invalid value, "
                      "use ANY/MATCH_ANY or ALL/MATCH_ALL)")
                failed = True
            else:
                status, msg = self.condlogmgr.ChangeMatchPolicy(normalised)
                if status:
                    print(f"  policy {normalised}: OK")
                else:
                    print(f"  policy {normalised}: FAILED ({msg})")
                    failed = True

        if failed:
            print("set log conditional_config: completed with errors (see above)")
        else:
            print("set log conditional_config: OK")

    def show_conditional_config(self):
        """Handle: ganesha_mgr show log conditional_config"""
        print("Conditional Logging Configuration")
        print("=" * 34)

        # Clients
        status, errormsg, clients = \
            self.condlogmgr.ShowConditionalLogClientList()
        if status:
            count = len(clients)
            print(f"\nClients ({count}):")
            if count == 0:
                print("  (none)")
            else:
                for client in clients:
                    print(f"  {client}")
        else:
            print(f"\nClients: ERROR ({errormsg})")

        # Exports
        status, errormsg, export_ids = \
            self.condlogmgr.ShowConditionalLogExportList()
        if status:
            count = len(export_ids)
            print(f"\nExports ({count}):")
            if count == 0:
                print("  (none)")
            else:
                for eid in export_ids:
                    print(f"  {eid}")
        else:
            print(f"\nExports: ERROR ({errormsg})")

        # Match policy
        status, errormsg = self.condlogmgr.ShowMatchPolicy()
        if status:
            # errormsg format: "Conditional logging current Match Policy: MATCH_ANY"
            policy_name = errormsg.rsplit(': ', 1)[-1].strip()
            print(f"\nMatch Policy : {policy_name}")
        else:
            print(f"\nMatch Policy : ERROR ({errormsg})")

        # Component log levels
        status, errormsg, prop_dict = self.condlogmgr.GetAll()
        print("\nComponent Log Levels:")
        if not status:
            print(f"  ERROR ({errormsg})")
        else:
            for comp in sorted(prop_dict.keys()):
                print(f"  {comp:<30} : {prop_dict[comp]}")

    def reset_conditional_config(self, raw_args):
        """Handle: ganesha_mgr reset log conditional_config [OPTIONS]"""
        if not raw_args:
            # Full atomic reset via the new D-Bus method
            print("Resetting all conditional logging configuration to defaults")
            status, errormsg = self.condlogmgr.Reset()
            print(f"  {errormsg}")
            return

        components, clients, export_ids, reset_policy, err = \
            _parse_reset_flags(raw_args)
        if err:
            sys.exit(f"reset log conditional_config: {err}")

        failed = False
        DEFAULT_COND_LEVEL = 'NIV_FULL_DEBUG'

        # Components: reset each to NIV_FULL_DEBUG
        for comp in components:
            status, msg = self.condlogmgr.Set(comp, DEFAULT_COND_LEVEL)
            if status:
                print(f"  reset {comp} -> {DEFAULT_COND_LEVEL}: OK")
            else:
                print(f"  reset {comp} -> {DEFAULT_COND_LEVEL}: FAILED ({msg})")
                failed = True

        # Clients: remove each
        for client in clients:
            status, msg = self.condlogmgr.ClientDisable(client)
            if status:
                print(f"  remove client {client}: OK")
            else:
                print(f"  remove client {client}: FAILED ({msg})")
                failed = True

        # Export IDs: remove each
        for eid in export_ids:
            status, msg = self.condlogmgr.ExportDisable(eid)
            if status:
                print(f"  remove export-id {eid}: OK")
            else:
                print(f"  remove export-id {eid}: FAILED ({msg})")
                failed = True

        # Policy: reset to MATCH_ANY
        if reset_policy:
            status, msg = self.condlogmgr.ChangeMatchPolicy('MATCH_ANY')
            if status:
                print("  reset policy -> MATCH_ANY: OK")
            else:
                print(f"  reset policy -> MATCH_ANY: FAILED ({msg})")
                failed = True

        if failed:
            print("reset log conditional_config: completed with errors (see above)")
        else:
            print("reset log conditional_config: OK")

# Main
if __name__ == '__main__':
    exportmgr = ShowExports()
    clientmgr = ManageClients()
    ganesha = ServerAdmin()
    logmgr = ManageLogs()
    cachemgr = ManageCache()
    condlogmgr = ManageCondLogs()
    prog = os.path.basename(sys.argv[0])

    USAGE = f"""
{prog} command [OPTIONS]
COMMANDS
   add:
      client ipaddr: Adds the client with the given IP
      export conf expr:
         Adds an export from the given config file that contains
         the given expression
         Example:
         add export /etc/ganesha/gpfs.conf "EXPORT(Export_ID=77)"
      conditional_clients ipaddr: Adds a client to conditional logging (IP/CIDR)
      conditional_exports export_id: Adds an export to conditional logging
   remove:
      client ipaddr: Removes the client with the given IP
      export export_id: Removes the export with the given id
      conditional_clients ipaddr: Removes a client from conditional logging (IP/CIDR)
      conditional_exports export_id: Removes an export from conditional logging
   update:
      export conf expr:
         Updates an export from the given config file that contains
         the given expression
         Example:
         update export /etc/ganesha/gpfs.conf "EXPORT(Export_ID=77)"
      conditional_match_policy policy:
         Updates the conditional logging match policy (ANY or ALL)
         Example:
         update conditional_match_policy ANY
   display:
      export export_id: Displays the export with the given ID.
         export_id must be positive number
         Example:
         display export 10
   purge:
      netgroups: Purges netgroups cache
      idmapper: Purges idmapper cache
      idmapper_negative: Purges idmapper negative cache
      gids: Purges gids cache
   show:
      clients: Displays the current clients
      version: Displays ganesha release information
      posix_fs: Displays the mounted POSIX filesystems
      exports: Displays all current exports
      idmapper_users: Displays the idmapper users cache
      idmapper_groups: Displays the idmapper groups cache
      idmapper_uid2grp: Displays the idmapper uid to groups cache
      conditional_clients: Displays the conditional logging client list
      conditional_exports: Displays the conditional logging export list
      conditional_match_policy: Displays the conditional logging match policy
   grace:
      ipaddr: Begins grace for the given IP
   trim:
      enable: Enable malloc trim
      disable: Disable malloc trim
      call: Call malloc trim
      status: Get current malloc trim status
   get:
      log component: Gets the log level for the given component
      log conditional component:
         Gets the log level for the given conditional component
   set:
      log component level:
         Sets the given log level to the given component
      log conditional component level:
         Sets the given log level to the given conditional component
      log conditional_config [OPTIONS]:
         Composite command to configure conditional logging in a single call.
         Options (all optional, at least one required):
           --components <c1,c2,...> --level <LEVEL>
              Set the conditional log level for the listed components.
              Repeatable: each --components/--level pair is processed in order.
              Component names may omit the COMPONENT_ prefix.
           --clients <ip1,ip2,...>
              Add IP/CIDR addresses to the conditional logging client list.
           --export-ids <id1,id2,...>
              Add export IDs to the conditional logging export list.
           --policy <ANY|ALL|MATCH_ANY|MATCH_ALL>
              Set the conditional logging match policy.
         Example:
           set log conditional_config --components "FSAL,DISPATCH" --level FULL_DEBUG \\
             --components ALL --level EVENT --clients 10.0.0.1 --export-ids 1,2 --policy ANY
   show:
      log conditional_config:
         Display full conditional logging configuration (clients, exports,
         policy, and all component log levels).
   reset:
      log conditional_config [OPTIONS]:
         With no options: atomically reset ALL conditional logging
         configuration to defaults (clients cleared, exports cleared,
         all component levels -> NIV_FULL_DEBUG, policy -> MATCH_ANY).
         With options: selectively reset only the specified items.
           --components <c1,c2,...>
              Reset the listed components to NIV_FULL_DEBUG.
           --clients <ip1,ip2,...>
              Remove the listed clients from the conditional logging list.
           --export-ids <id1,id2,...>
              Remove the listed export IDs from the conditional logging list.
           --policy
              Reset the match policy to MATCH_ANY (boolean flag, no value).
         Options may be combined.
   getall:
      logs: Prints all log components
   shutdown: Shuts down the ganesha nfs server

"""
    if len(sys.argv) < 2:
        exit_try_help("Too few arguments")

    # add
    elif sys.argv[1] == "add":
        if len(sys.argv) == 2:
            exit_try_help("add requires client, export, conditional_clients or conditional_exports option")
        if sys.argv[2] == "client":
            if len(sys.argv) < 4:
                exit_try_help("add client requires an IP")
            clientmgr.addclient(sys.argv[3])
        elif sys.argv[2] == "export":
            if len(sys.argv) < 5:
                exit_try_help("add export requires a config file and an expression")
            exportmgr.addexport(sys.argv[3], sys.argv[4])
        elif sys.argv[2] == "conditional_clients":
            if len(sys.argv) < 4:
                exit_try_help("add conditional_clients requires an IP or CIDR")
            condlogmgr.add_conditional_client(sys.argv[3])
        elif sys.argv[2] == "conditional_exports":
            if len(sys.argv) < 4:
                exit_try_help("add conditional_exports requires an export ID")
            condlogmgr.add_conditional_export(sys.argv[3])
        else:
            exit_option_not_supported("add")

    # remove
    elif sys.argv[1] == "remove":
        if len(sys.argv) == 2:
            exit_try_help("remove requires client, export, conditional_clients or conditional_exports option")
        if sys.argv[2] == "client":
            if len(sys.argv) < 4:
                exit_try_help("remove client requires an IP")
            clientmgr.removeclient(sys.argv[3])
        elif sys.argv[2] == "export":
            if len(sys.argv) < 4:
                exit_try_help("remove export requires an export ID")
            exportmgr.removeexport(sys.argv[3])
        elif sys.argv[2] == "conditional_clients":
            if len(sys.argv) < 4:
                exit_try_help("remove conditional_clients requires an IP or CIDR")
            client_arg = sys.argv[3]
            # Refuse to run if shell likely expanded unquoted '*' (multiple args or
            # single arg that does not look like an IP/CIDR). Do not print the
            # expanded value or call the backend.
            if len(sys.argv) > 4:
                print('The shell expanded "*" to multiple arguments.',
                      'To remove the match-any client, quote the asterisk:',
                      '  ganesha_mgr remove conditional_clients \'*\'',
                      sep='\n')
                sys.exit(1)
            if client_arg != '*' and '.' not in client_arg and '/' not in client_arg:
                print('The argument does not look like an IP or CIDR; the shell may have expanded "*".',
                      'To remove the match-any client, quote the asterisk:',
                      '  ganesha_mgr remove conditional_clients \'*\'',
                      sep='\n')
                sys.exit(1)
            condlogmgr.remove_conditional_client(client_arg)
        elif sys.argv[2] == "conditional_exports":
            if len(sys.argv) < 4:
                exit_try_help("remove conditional_exports requires an export ID")
            condlogmgr.remove_conditional_export(sys.argv[3])
        else:
            exit_option_not_supported("remove")

    # update
    elif sys.argv[1] == "update":
        if len(sys.argv) < 3:
            exit_try_help("update requires export or conditional_match_policy option")
        if sys.argv[2] == "export":
            if len(sys.argv) < 5:
                exit_try_help("update export requires a config file and an expression")
            exportmgr.updateexport(sys.argv[3], sys.argv[4])
        elif sys.argv[2] == "conditional_match_policy":
            if len(sys.argv) < 4:
                exit_try_help("update conditional_match_policy requires a policy (e.g. ANY or ALL)")
            condlogmgr.update_conditional_match_policy(sys.argv[3])
        else:
            exit_option_not_supported("update")

    # display
    elif sys.argv[1] == "display":
        if len(sys.argv) < 4:
            exit_try_help("display export requires an export ID")
        elif not sys.argv[3].isdigit():
            exit_try_help("export ID must be positive number")
        if sys.argv[2] == "export":
            exportmgr.displayexport(sys.argv[3])
        else:
            exit_option_not_supported("display")

    # purge
    elif sys.argv[1] == "purge":
        if len(sys.argv) < 3:
            exit_try_help("purge requires a cache name to purge")
        if sys.argv[2] == "netgroups":
            ganesha.purge_netgroups()
        elif sys.argv[2] == "idmapper":
            ganesha.purge_idmapper()
        elif sys.argv[2] == "idmapper_negative":
            ganesha.purge_idmapper_negative()
        elif sys.argv[2] == "gids":
            ganesha.purge_gids()
        else:
            exit_option_not_supported("purge")

    # show
    elif sys.argv[1] == "show":
        if len(sys.argv) < 3:
            exit_try_help("show requires an option")
        if sys.argv[2] == "clients":
            clientmgr.showclients()
        elif sys.argv[2] == "log":
            if len(sys.argv) < 4 or sys.argv[3] != "conditional_config":
                exit_try_help('show log requires "conditional_config"')
            condlogmgr.show_conditional_config()
        elif sys.argv[2] == "version":
            ganesha.show_version()
        elif sys.argv[2] == "exports":
            exportmgr.showexports()
        elif sys.argv[2] == "posix_fs":
            cachemgr.showfs()
        elif sys.argv[2] == "idmapper_users":
            cachemgr.showidmapper_users()
        elif sys.argv[2] == "idmapper_groups":
            cachemgr.showidmapper_groups()
        elif sys.argv[2] == "idmapper_uid2grp":
            cachemgr.showidmapper_uid2grp()
        elif sys.argv[2] == "conditional_clients":
            condlogmgr.show_conditional_clients()
        elif sys.argv[2] == "conditional_exports":
            condlogmgr.show_conditional_exports()
        elif sys.argv[2] == "conditional_match_policy":
            condlogmgr.show_conditional_match_policy()
        else:
            exit_option_not_supported("show")

    # reset
    elif sys.argv[1] == "reset":
        if len(sys.argv) < 4:
            exit_try_help('reset requires "log conditional_config" [OPTIONS]')
        if sys.argv[2] == "log":
            if sys.argv[3] == "conditional_config":
                condlogmgr.reset_conditional_config(sys.argv[4:])
            else:
                exit_option_not_supported("reset log")
        else:
            exit_option_not_supported("reset")

    # grace
    elif sys.argv[1] == "grace":
        if len(sys.argv) < 3:
            exit_try_help("grace requires an IP")
        ganesha.grace(sys.argv[2])

    # trim (malloc trim)
    elif sys.argv[1] == "trim":
        if len(sys.argv) < 3:
            exit_try_help("trim requires enable/disable/call/status arg")

        if sys.argv[2] == 'enable':
            ganesha.trim_enable()
        elif sys.argv[2] == 'disable':
            ganesha.trim_disable()
        elif sys.argv[2] == 'status':
            ganesha.trim_status()
        elif sys.argv[2] == 'call':
            ganesha.trim_call()
        else:
            exit_option_not_supported("trim")

    # set
    elif sys.argv[1] == "set":
        if len(sys.argv) < 3:
            exit_try_help("set requires log option")
        if sys.argv[2] == "log":
            if len(sys.argv) < 4:
                exit_try_help("set log requires a sub-command")
            if sys.argv[3] == "conditional":
                if len(sys.argv) < 6:
                    msg = 'set log conditional requires '
                    msg += 'a component and a log level.'
                    msg += 'Try "ganesha_mgr.py help" for more info'
                    sys.exit(msg)
                condlogmgr.set(sys.argv[4], sys.argv[5])
            elif sys.argv[3] == "conditional_config":
                condlogmgr.set_conditional_config(sys.argv[4:])
            else:
                if len(sys.argv) < 5:
                    exit_try_help("set log requires a component and a log level")
                logmgr.set(sys.argv[3], sys.argv[4])
        else:
            exit_option_not_supported("set")

    # get
    elif sys.argv[1] == "get":
        if len(sys.argv) < 4:
            exit_try_help("get log requires a component")
        if sys.argv[2] == "log":
            if sys.argv[3] == "conditional":
                if len(sys.argv) < 5:
                    msg = 'get log conditional requires a component. '
                    msg += 'Try "ganesha_mgr.py help" for more info'
                    sys.exit(msg)
                condlogmgr.get(sys.argv[4])
            else:
                logmgr.get(sys.argv[3])
        else:
            exit_option_not_supported("get")

    # getall
    elif sys.argv[1] == "getall":
        if len(sys.argv) < 3:
            exit_try_help("getall requires a component")
        if sys.argv[2] == "logs":
            logmgr.getall()
        else:
            exit_option_not_supported("getall")

    # others
    elif sys.argv[1] == "shutdown":
        ganesha.shutdown()
    elif sys.argv[1] == "help":
        print(USAGE)

    else:
        exit_try_help("Unknown/missing command")
