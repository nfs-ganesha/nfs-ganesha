#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Copyright (C) 2014 IBM
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
# Author: Marc Eshel <eshel@us.ibm.com>

from __future__ import print_function
import sys
import time
import json
import logging

# Create a system bus object.
import dbus

# ============================================================================
# Module-level Constants for NFS Protocol Parsing
# ============================================================================

# Protocol names for I/O operations
NFS_IO_PROTO_NAMES = ['nfsv3', 'nfsv40', 'nfsv41', 'nfsv42']

# Operation names for I/O statistics
NFS_IO_OP_NAMES = ['read', 'write', 'other', 'layout']

# Stats count for different protocol versions (I/O operations)
NFS_IO_STATS_COUNT_V3_V40 = 3   # NFSv3 and NFSv4.0: read, write, other
NFS_IO_STATS_COUNT_V41_V42 = 4  # NFSv4.1 and NFSv4.2: read, write, other, layout

# Protocol names for all operations
NFS_ALLOPS_PROTO_NAMES = ['nfsv3', 'nlmv4', 'nfsv4', 'nfsv4_compounds']

# Headers for all operations statistics
NFS_ALLOPS_HEADERS = [
    ['total', 'errors', 'dups'],              # NFSv3
    ['total', 'errors', 'dups'],              # NLMv4
    ['total', 'errors'],                      # NFSv4
    ['total', 'errors', 'op_in_compounds']    # NFSv4 Compounds
]

def dbus_to_std(v):
    ctor = None

    if (isinstance(v, dbus.UInt16)
        or isinstance(v, dbus.UInt32)
        or isinstance(v, dbus.UInt64)
        or isinstance(v, int)):
        ctor = int
    elif isinstance(v, dbus.Boolean):
        ctor = bool
    elif isinstance(v, dbus.Double):
        ctor = float
    elif isinstance(v, dbus.String):
        ctor = str

    assert ctor is not None, "{}".format(v.__class__.__name__)
    return ctor(v)

def timestr(t):
    timestamp = t[0] + float(t[1]) / 1e9
    return time.strftime('%FT%TZ', time.gmtime(timestamp))

class Report():
    def __init__(self, result):
        self.result = result

    def json(self):
        output = self.report()
        return json.dumps(output)

    def report(self):
        report, success = self._header(self.result)
        if not success:
            return report

        self.fill_report(report)
        return report

    def fill_report(self, report):
        pass

    def _header(self, result):
        # Unfortunately, there is no established way to read the response
        # from the various endpoint. This tries to cover the most common
        # cases, if you are in a specific case, feel free to implement the
        # custom logic separately.
        def is_timestamp_struct(t):
            return (isinstance(result[2], dbus.Struct)
                    and len(result[2]) == 2
                    and isinstance(result[2][0], dbus.UInt64)
                    and isinstance(result[2][1], dbus.UInt64))

        header = {
            'status': {},
        }

        if not isinstance(result[0], dbus.Boolean):
            # The header is missing the status field so we just return the time
            header['status']['time'] = timestr(result[0])
            success = True
            return header, success

        status = result[0]
        success = status
        if not success:
            header['status']['error'] = result[1]
        elif result[2] and is_timestamp_struct(result[2]):
            header['status']['time'] = timestr(result[2])

        return header, success

def report_key_value(values, report):
    for i in range(int(len(values) / 2)):
        key = str(values[i*2 + 0])
        value = values[i*2 + 1]
        report[key] = dbus_to_std(value)

class RetrieveExportStats():
    def __init__(self):
        self.dbus_service_name = "org.ganesha.nfsd"
        self.dbus_exportstats_name = "org.ganesha.nfsd.exportstats"
        self.dbus_exportmgr_name = "org.ganesha.nfsd.exportmgr"
        self.export_interface = "/org/ganesha/nfsd/ExportMgr"

        self.bus = dbus.SystemBus()
        self.exportmgrobj = self.bus.get_object(self.dbus_service_name,
                                                self.export_interface)

    # NFSv3/NFSv4/NLM/MNT/QUOTA stats over all exports
    def fast_stats(self):
        stats_op = self.exportmgrobj.get_dbus_method("GetFastOPS",
                                                     self.dbus_exportstats_name)
        return FastStats(stats_op())
    # NFSv3/NFSv40/NFSv41/NFSv42/NLM4/MNTv1/MNTv3/RQUOTA totalled over all exports
    def global_stats(self):
        stats_op = self.exportmgrobj.get_dbus_method("GetGlobalOPS",
                                                     self.dbus_exportstats_name)
        return GlobalStats(stats_op())
    # mdcache stats
    def inode_stats(self):
        stats_op = self.exportmgrobj.get_dbus_method("ShowMDCache",
                                                     self.dbus_exportstats_name)
        return InodeStats(stats_op())
    # list of all exports
    def export_stats(self):
        stats_op = self.exportmgrobj.get_dbus_method("ShowExports",
                                                     self.dbus_exportmgr_name)
        return ExportStats(stats_op())

    # NFSv3/NFSv4/NLM/MNT/QUOTA stats totalled for a single export
    def total_stats(self, export_id):
        stats_op = self.exportmgrobj.get_dbus_method("GetTotalOPS",
                                                     self.dbus_exportstats_name)
        stats_dict = {}
        if export_id < 0:
            export_list = self.export_stats()
            for exportid in export_list.exportids():
                stats_dict[exportid] = stats_op(exportid)
        else:
            stats_dict[export_id] = stats_op(int(export_id))
        return TotalStats(stats_dict)

    def io_stats(self, stats_op, export_id):
        stats_dict = {}
        if export_id < 0:
            export_list = self.export_stats()
            for exportid in export_list.exportids():
                stats_dict[exportid] = stats_op(exportid)
            return stats_dict
        else:
            stats_dict[export_id] = stats_op(export_id)
            return stats_dict
    def v3io_stats(self, export_id):
        stats_op = self.exportmgrobj.get_dbus_method("GetNFSv3IO",
                                                     self.dbus_exportstats_name)
        return ExportIOv3Stats(self.io_stats(stats_op, export_id))
    def v4io_stats(self, export_id):
        stats_op = self.exportmgrobj.get_dbus_method("GetNFSv40IO",
                                                     self.dbus_exportstats_name)
        return ExportIOv4Stats(self.io_stats(stats_op, export_id))
    def v41io_stats(self, export_id):
        stats_op = self.exportmgrobj.get_dbus_method("GetNFSv41IO",
                                                     self.dbus_exportstats_name)
        return ExportIOv41Stats(self.io_stats(stats_op, export_id))
    def v42io_stats(self, export_id):
        stats_op = self.exportmgrobj.get_dbus_method("GetNFSv42IO",
                                                     self.dbus_exportstats_name)
        return ExportIOv42Stats(self.io_stats(stats_op, export_id))
    def iomon_stats(self, export_id):
        stats_op = self.exportmgrobj.get_dbus_method("GetNFSIOMon",
                                                     self.dbus_exportstats_name)
        return ExportIOMonStats(self.io_stats(stats_op, export_id))
    def pnfs_stats(self, export_id):
        stats_op = self.exportmgrobj.get_dbus_method("GetNFSv41Layouts",
                                                     self.dbus_exportstats_name)
        stats_dict = {}
        if export_id < 0:
            export_list = self.export_stats()
            for exportid in export_list.exportids():
                stats_dict[exportid] = stats_op(exportid)
            return PNFSStats(stats_dict)
        else:
            stats_dict[export_id] = stats_op(int(export_id))
            return PNFSStats(stats_dict)

    # Reset the statistics counters for all
    def reset_stats(self):
        stats_state = self.exportmgrobj.get_dbus_method("ResetStats",
                                                        self.dbus_exportstats_name)
        return StatsReset(stats_state())

    # fsal stats
    def fsal_stats(self, fsal):
        stats_op = self.exportmgrobj.get_dbus_method("GetFSALStats",
                                                     self.dbus_exportstats_name)
        return DumpFSALStats(stats_op(fsal))

    # enable stats
    def enable_stats(self, stat_type):
        stats_state = self.exportmgrobj.get_dbus_method("EnableStats",
                                                        self.dbus_exportstats_name)
        return StatsEnable(stats_state(stat_type))

    # disable stats
    def disable_stats(self, stat_type):
        stats_state = self.exportmgrobj.get_dbus_method("DisableStats",
                                                        self.dbus_exportstats_name)
        return StatsDisable(stats_state(stat_type))

    # status
    def status_stats(self):
        stats_state = self.exportmgrobj.get_dbus_method("StatusStats",
                                                        self.dbus_exportstats_name)
        return StatsStatus(stats_state())
    # v3_full
    def v3_full_stats(self):
        stats_state = self.exportmgrobj.get_dbus_method("GetFULLV3Stats",
                                                        self.dbus_exportstats_name)
        return DumpFULLV3Stats(stats_state())
    # v4_full
    def v4_full_stats(self):
        stats_state = self.exportmgrobj.get_dbus_method("GetFULLV4Stats",
                                                        self.dbus_exportstats_name)
        return DumpFULLV4Stats(stats_state())
    # authentication
    def auth_stats(self):
        stats_state = self.exportmgrobj.get_dbus_method("GetAuthStats",
                                                        self.dbus_exportstats_name)
        return DumpAuth(stats_state())
    # Export Details
    def export_details_stats(self, export_id):
        stats_op = self.exportmgrobj.get_dbus_method("GetExportDetails",
                                 self.dbus_exportstats_name)
        return ExportDetails(stats_op(export_id))


class RetrieveClientStats():
    def __init__(self):
        self.dbus_service_name = "org.ganesha.nfsd"
        self.dbus_clientstats_name = "org.ganesha.nfsd.clientstats"
        self.dbus_clientmgr_name = "org.ganesha.nfsd.clientmgr"
        self.client_interface = "/org/ganesha/nfsd/ClientMgr"

        self.bus = dbus.SystemBus()
        self.clientmgrobj = self.bus.get_object(self.dbus_service_name,
                                                self.client_interface)

    # delegation stats related to a single client ip
    def deleg_stats(self, ip_):
        stats_op = self.clientmgrobj.get_dbus_method("GetDelegations",
                                                     self.dbus_clientstats_name)
        return DelegStats(stats_op(ip_))
    def list_clients(self):
        stats_op = self.clientmgrobj.get_dbus_method("ShowClients",
                                                     self.dbus_clientmgr_name)
        return ClientStats(stats_op())
    # Clients specific stats
    def client_io_ops_stats(self, ip):
        stats_op = self.clientmgrobj.get_dbus_method("GetClientIOops",
                          self.dbus_clientstats_name)
        return ClientIOops(stats_op(ip))
    def all_clients_io_ops_stats(self):
        stats_op = self.clientmgrobj.get_dbus_method("GetAllClientIOops",
                          self.dbus_clientstats_name)
        return AllClientsIOops(stats_op())
    def client_all_ops_stats(self, ip):
        stats_op = self.clientmgrobj.get_dbus_method("GetClientAllops",
                          self.dbus_clientstats_name)
        return ClientAllops(stats_op(ip))
    def all_clients_all_ops_stats(self):
        stats_op = self.clientmgrobj.get_dbus_method("GetAllClientAllops",
                          self.dbus_clientstats_name)
        return AllClientsAllops(stats_op())


class ClientStats(Report):
    def __init__(self, stats):
        super().__init__(stats)
        self.stats = stats
        self.clients = {}
        for client in stats[1]:
            clientaddr = client[0]
            self.clients[clientaddr] = Client(client)

    def fill_report(self, report):
        report['clients'] = []

        for addr, client in self.clients.items():
            client_report = {
                'addr': dbus_to_std(client.clientaddr),
                'is_connected': dbus_to_std(client.is_connected),
                'nfsv3_stats': dbus_to_std(client.nfsv3_stats_avail),
                'nfsv40_stats': dbus_to_std(client.nfsv40_stats_avail),
                'nfsv41_stats': dbus_to_std(client.nfsv41_stats_avail),
                'nfsv42_stats': dbus_to_std(client.nfsv42_stats_avail),
                'mnt_stats': dbus_to_std(client.mnt_stats_avail),
                'nlmv4_stats': dbus_to_std(client.nlmv4_stats_avail),
                'rquota_stats': dbus_to_std(client.rquota_stats_avail),
                '9p_stats': dbus_to_std(client._9p_stats_avail),
            }
            report['clients'].append(client_report)

        return report['clients']

    def __str__(self):
        output = ("\nTimestamp: " + time.ctime(self.stats[0][0]) +
                  str(self.stats[0][1]) + " nsecs" +
                  "\nClient List:\n")
        for add, client in self.clients.items():
            output += str(client)
        return output


class ProtocolsStats(object):
    def __init__(self, protocols):
        # init default protocol stats
        self.protocols_stats = dict({'NFSv3': 0,
                                     'NFSv40': 0,
                                     'NFSv41': 0,
                                     'NFSv42': 0,
                                     'MNT': 0,
                                     'NLMv4': 0,
                                     'RQUOTA': 0,
                                     '9P': 0})
        self._update_protocols_stats(protocols)

    def _update_protocols_stats(self, protocols):
        for protocol in protocols:
            name, enabled = protocol[0], protocol[1]
            self.protocols_stats[name] = enabled


class Client(ProtocolsStats):
    def __init__(self, client):
        super().__init__(client[2])
        self.clientaddr = client[0]
        self.is_connected = client[1]
        self.nfsv3_stats_avail = self.protocols_stats['NFSv3']
        self.nfsv40_stats_avail = self.protocols_stats['NFSv40']
        self.nfsv41_stats_avail = self.protocols_stats['NFSv41']
        self.nfsv42_stats_avail = self.protocols_stats['NFSv42']
        self.mnt_stats_avail = self.protocols_stats['MNT']
        self.nlmv4_stats_avail = self.protocols_stats['NLMv4']
        self.rquota_stats_avail = self.protocols_stats['RQUOTA']
        self._9p_stats_avail = self.protocols_stats['9P']

    def __str__(self):
        return ("\nClient Address: " + str(self.clientaddr) +
                "\n\tNFSv3 stats available: " + str(self.nfsv3_stats_avail) +
                "\n\tNFSv4.0 stats available: " + str(self.nfsv40_stats_avail) +
                "\n\tNFSv4.1 stats available: " + str(self.nfsv41_stats_avail) +
                "\n\tNFSv4.2 stats available: " + str(self.nfsv42_stats_avail) +
                "\n\tMNT stats available: " + str(self.mnt_stats_avail) +
                "\n\tNLMv4 stats available: " + str(self.nlmv4_stats_avail) +
                "\n\tRQUOTA stats available: " + str(self.rquota_stats_avail) +
                "\n\t9P stats available: " + str(self._9p_stats_avail) + "\n")


class DelegStats(Report):
    def __init__(self, stats):
        super().__init__(stats)

        self.curtime = time.time()
        self.status = stats[1]
        if stats[1] == "OK":
            self.timestamp = (stats[2][0], stats[2][1])
            self.curr_deleg = stats[3][0]
            self.tot_recalls = stats[3][1]
            self.fail_recall = stats[3][2]
            self.num_revokes = stats[3][3]

    def fill_report(self, report):
        report['curr_deleg_grants'] = dbus_to_std(self.curr_deleg);
        report['tot_recalls'] = dbus_to_std(self.tot_recalls);
        report['failed_recalls'] = dbus_to_std(self.fail_recall);
        report['num_revokes'] = dbus_to_std(self.num_revokes);

    def __str__(self):
        if self.status != "OK":
            return "GANESHA RESPONSE STATUS: " + self.status
        self.starttime = self.timestamp[0] + self.timestamp[1] / 1e9
        self.duration = self.curtime - self.starttime
        return ("GANESHA RESPONSE STATUS: " + self.status +
                "\nStats collected since: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs \n" +
                "\nDuration: " + "%.10f" % self.duration + " seconds" +
                "\nCurrent Delegations: " + str(self.curr_deleg) +
                "\nCurrent Recalls: " + str(self.tot_recalls) +
                "\nCurrent Failed Recalls: " + str(self.fail_recall) +
                "\nCurrent Number of Revokes: " + str(self.num_revokes))

class ClientIOops(Report):
    def __init__(self, stats):
        super().__init__(stats)

        self.stats = stats
        self.status = stats[1]
        if stats[1] == "OK" and len(stats) > 2:
            # Check if stats[2] is a timestamp tuple (struct with 2 elements)
            try:
                # Verify it's not a boolean and has the right structure
                if (not isinstance(stats[2], (bool, dbus.Boolean)) and
                    hasattr(stats[2], '__len__') and len(stats[2]) == 2):
                    self.timestamp = (int(stats[2][0]), int(stats[2][1]))
                else:
                    # No valid timestamp, use current time
                    import time
                    self.timestamp = (int(time.time()), 0)
                    logging.debug("ClientIOops: Invalid timestamp at stats[2], using current time. stats[2]=%s type=%s",
                                stats[2], type(stats[2]))
            except (TypeError, IndexError, ValueError) as e:
                import time
                self.timestamp = (int(time.time()), 0)
                logging.debug("ClientIOops: Exception getting timestamp: %s", e)
        else:
            import time
            self.timestamp = (int(time.time()), 0)

    def report(self):
        """Override report() to handle the custom format with status/error/timestamp"""
        header = {
            'status': {},
        }

        if self.status != "OK":
            header['status']['success'] = False
            header['status']['error'] = self.status
            return header

        header['status']['success'] = True
        header['status']['time'] = timestr(self.timestamp)

        self.fill_report(header)
        return header

    def fill_report(self, report):
        proto_table = [
            'nfsv3',
            'nfsv40',
            'nfsv41',
            'nfsv42'
        ]
        op_table = [
            'read',
            'write',
            'other',
            'layout'
        ]
        stats = self.result[3:]

        def op_stats(stats):
            counters = [
                'total',
                'errors',
                'transferred'
            ]

            result = {}
            # Only iterate over the actual number of fields in stats
            for i in range(min(len(stats), len(counters))):
                result[counters[i]] = dbus_to_std(stats[i])

            return result

        i = 0
        for proto in proto_table:
            # Checks that the current stat field is a boolean
            # indicating if stats are available for the given protocol.
            available = stats[i]
            i += 1
            assert isinstance(available, dbus.Boolean)

            if not available:
                continue

            # Now greedily takes stats until we reach the next boolean (and so
            # the next protocol).
            ops = iter(op_table)
            found_stats = []
            report[proto] = {}
            while i < len(stats) and not isinstance(stats[i], dbus.Boolean):
                op_name = next(ops)
                report[proto][op_name] = op_stats(stats[i])
                i += 1

    def __str__(self):
        output = ""
        cnt = 3
        if self.status != "OK":
            return ("GANESHA RESPONSE STATUS: " + self.status)
        else:
            output += "\nClient last active at: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs"
            j = 0
            while j<4:
                # Convert dbus.Boolean to Python bool for proper evaluation
                proto_available = bool(self.stats[cnt]) if cnt < len(self.stats) else False
                if proto_available:
                    # Double-check: verify ALL expected stat structs exist and are valid
                    # For NFSv3/NFSv4.0: need 3 stats (read, write, other)
                    # For NFSv4.1/NFSv4.2: need 4 stats (read, write, other, layout)
                    expected_stats = 3 if j < 2 else 4
                    all_stats_valid = True
                    proto_names = ['NFSv3', 'NFSv4.0', 'NFSv4.1', 'NFSv4.2']
                    for check_i in range(expected_stats):
                        stat_idx = cnt + 1 + check_i
                        if stat_idx >= len(self.stats):
                            logging.debug("%s validation: stat_idx %d >= len(stats) %d",
                                        proto_names[j], stat_idx, len(self.stats))
                            all_stats_valid = False
                            break
                        if isinstance(self.stats[stat_idx], (bool, dbus.Boolean)):
                            logging.debug("%s validation: stats[%d] is Boolean: %s",
                                        proto_names[j], stat_idx, self.stats[stat_idx])
                            all_stats_valid = False
                            break

                    if not all_stats_valid:
                        logging.debug("%s: Not all stats valid, treating as unavailable (cnt=%d, expected=%d)",
                                    proto_names[j], cnt, expected_stats)
                        # Not all expected stats are valid, treat protocol as unavailable
                        proto_available = False
                    else:
                        logging.debug("%s: All %d stats valid at cnt=%d", proto_names[j], expected_stats, cnt)

                if proto_available:
                    i = 0
                    if j==0:
                        output += "\n\t\tNFSv3:"
                    if j==1:
                        output += "\n\t\tNFSv4.0:"
                    if j==2:
                        output += "\n\t\tNFSv4.1:"
                    if j==3:
                        output += "\n\t\tNFSv4.2:"
                    output += "\n\t     total \t errors     transferred"
                    while  i<4:
                        if i==0:
                            output += "\nREAD :"
                        if i==1:
                            output += "\nWRITE:"
                        if i==2:
                            output += "\nOther:"
                        if i==3:
                            output += "\nLayout:"
                        output += "%12d" % self.stats[cnt+i+1][0]
                        output += " %12d" % self.stats[cnt+i+1][1]
                        if i<2:
                            output += " %12d" % self.stats[cnt+i+1][2]
                        if (i==2 and j<2):
                            i += 1
                        i += 1
                    if j<2:
                        cnt += 4
                    else:
                        cnt += 5
                else:
                    if j==0:
                        output += "\n\tNo NFSv3 activity"
                    if j==1:
                        output += "\n\tNo NFSv4.0 activity"
                    if j==2:
                        output += "\n\tNo NFSv4.1 activity"
                    if j==3:
                        output += "\n\tNo NFSv4.2 activity"
                    # Check if stats structs are present after unavailable protocol
                    # GetAllClientIOops: stats always present (check if next element is a struct)
                    # GetClientIOops: stats not present (check if next element is a boolean)
                    if (cnt+1) < len(self.stats) and not isinstance(self.stats[cnt+1], (bool, dbus.Boolean)):
                        # Stats are present even though protocol unavailable (GetAllClientIOops case)
                        if j<2:
                            cnt += 4  # Skip: 1 boolean + 3 stats
                        else:
                            cnt += 5  # Skip: 1 boolean + 4 stats
                    else:
                        # Stats not present (GetClientIOops case)
                        cnt += 1  # Skip: just the boolean
                j += 1
            return output

class AllClientsIOops(Report):
    """Report class for I/O operations statistics of all clients"""

    def __init__(self, stats):
        super().__init__(stats)
        self.stats = stats
        self.clients = {}
        self.client_count = 0
        self.status = stats[0] if stats else False
        self.error_msg = None

        # Check if stats are enabled
        if not self.status or (len(stats) > 1 and stats[1] != "OK"):
            self.error_msg = stats[1] if len(stats) > 1 else "Unknown error"
            return

        # Parse array of client stats
        # stats format: [status, error_msg, timestamp, [(client_ip, timestamp, nfsv3_available, ...), ...]]
        if len(stats) > 3 and stats[3]:
            for client_data in stats[3]:
                try:
                    if not client_data or len(client_data) < 2:
                        continue

                    client_ip = str(client_data[0])
                    # Reconstruct format expected by ClientIOops
                    # ClientIOops expects: [status, error_msg, timestamp, nfsv3_available, ...]
                    # We have: [client_ip, timestamp, nfsv3_available, ...]
                    # Insert status and error_msg before the rest of the data, keeping DBus types
                    client_stats = [dbus.Boolean(True), dbus.String("OK")] + list(client_data[1:])
                    self.clients[client_ip] = ClientIOops(client_stats)
                    self.client_count += 1
                except (IndexError, TypeError, ValueError) as e:
                    logging.warning("Failed to parse client I/O ops data: %s", e)
                    continue

    def fill_report(self, report):
        """Generate JSON report for all clients' I/O operations.

        Note: GetAllClientIOops returns stats structures even when protocol is unavailable,
        unlike GetClientIOops which only includes stats when available.
        """
        if not self.status:
            report['error'] = self.error_msg
            return

        report['total_clients'] = self.client_count
        report['clients'] = {}

        if len(self.stats) > 3 and self.stats[3]:
            for client_data in self.stats[3]:
                try:
                    client_ip = str(client_data[0])
                    timestamp = (int(client_data[1][0]), int(client_data[1][1]))

                    client_report = {
                        'status': {
                            'success': True,
                            'time': timestr(timestamp)
                        }
                    }

                    idx = 2  # Start after client_ip and timestamp

                    for proto_idx, proto_name in enumerate(NFS_IO_PROTO_NAMES):
                        proto_available = bool(client_data[idx])
                        idx += 1

                        if proto_available:
                            stats_count = NFS_IO_STATS_COUNT_V3_V40 if proto_idx < 2 else NFS_IO_STATS_COUNT_V41_V42

                            # Only include protocol in output if it has non-zero stats
                            has_real_stats = any(
                                any(int(client_data[idx + i][j]) != 0 for j in range(len(client_data[idx + i])))
                                for i in range(stats_count)
                            )

                            if has_real_stats:
                                client_report[proto_name] = {}
                                for i in range(stats_count):
                                    stat_struct = client_data[idx + i]
                                    op_name = NFS_IO_OP_NAMES[i]
                                    client_report[proto_name][op_name] = {
                                        'total': int(stat_struct[0]),
                                        'errors': int(stat_struct[1]),
                                    }
                                    if len(stat_struct) > 2:
                                        client_report[proto_name][op_name]['transferred'] = int(stat_struct[2])

                        # In GetAllClientIOops, stats are always present (even if zeros)
                        stats_to_skip = NFS_IO_STATS_COUNT_V3_V40 if proto_idx < 2 else NFS_IO_STATS_COUNT_V41_V42
                        idx += stats_to_skip

                    report['clients'][client_ip] = client_report
                except (IndexError, TypeError, ValueError) as e:
                    logging.warning("Failed to format client for JSON: %s", e)
                    continue

    def __str__(self):
        """Generate text output for all clients' I/O operations."""
        if not self.status:
            return f"\nGANESHA RESPONSE STATUS: {self.error_msg}"

        if not self.clients:
            return "\nNo clients connected"

        output = f"\nTotal clients: {self.client_count}\n"
        for idx, ip in enumerate(sorted(self.clients.keys()), 1):
            client_stats = self.clients[ip]
            output += "\n" + "="*70
            output += f"\nClient {idx}/{self.client_count}: {ip}"
            output += "\n" + "="*70
            try:
                output += str(client_stats)
            except Exception as e:
                logging.warning("Failed to format client %s stats: %s", ip, str(e))
                output += f"\n  Error formatting stats: {str(e)}"
            output += "\n"
        return output

class AllClientsAllops(Report):
    """Report class for all operations statistics of all clients"""

    def __init__(self, stats):
        super().__init__(stats)
        self.stats = stats
        self.clients = {}
        self.client_count = 0
        self.status = stats[0] if stats else False
        self.error_msg = None

        # Check if stats are enabled
        if not self.status or (len(stats) > 1 and stats[1] != "OK"):
            self.error_msg = stats[1] if len(stats) > 1 else "Unknown error"
            return

        # Parse array of client stats
        # stats format: [status, error_msg, timestamp, [(client_ip, timestamp, ...), ...]]
        if len(stats) > 3 and stats[3]:
            for client_data in stats[3]:
                try:
                    if not client_data or len(client_data) < 2:
                        continue

                    client_ip = str(client_data[0])
                    # Reconstruct format expected by ClientAllops
                    # ClientAllops expects: [status, error_msg, timestamp, nfsv3_available, ...]
                    # We have: [client_ip, timestamp, nfsv3_available, ...]
                    # Insert status and error_msg before the rest of the data, keeping DBus types
                    client_stats = [dbus.Boolean(True), dbus.String("OK")] + list(client_data[1:])
                    self.clients[client_ip] = ClientAllops(client_stats)
                    self.client_count += 1
                except (IndexError, TypeError, ValueError) as e:
                    logging.warning("Failed to parse client all ops data: %s", e)
                    continue

    def fill_report(self, report):
        if not self.status:
            report['error'] = self.error_msg
            return

        report['total_clients'] = self.client_count
        report['clients'] = {}
        for ip in sorted(self.clients.keys()):
            client_stats = self.clients[ip]
            client_report = client_stats.report()
            # Remove the outer status wrapper since we know it's OK
            if 'status' in client_report:
                del client_report['status']
            report['clients'][ip] = client_report

    def __str__(self):
        if not self.status:
            return f"\nGANESHA RESPONSE STATUS: {self.error_msg}"

        if not self.clients:
            return "\nNo clients connected"

        output = f"\nTotal clients: {self.client_count}\n"
        for idx, ip in enumerate(sorted(self.clients.keys()), 1):
            client_stats = self.clients[ip]
            output += "\n" + "="*70
            output += f"\nClient {idx}/{self.client_count}: {ip}"
            output += "\n" + "="*70
            output += str(client_stats)
            output += "\n"
        return output

class ClientAllops(Report):
    """Report class for all NFS operations of a single client.

    Handles two different data formats:
    - GetClientAllops: Single client query with flat struct format
    - GetAllClientAllops: All clients query with array of structs format
    """

    STATS_START_INDEX = 3  # Start after status, error_msg, timestamp

    def __init__(self, stats):
        super().__init__(stats)
        import time

        self.stats = stats
        self.status = stats[1]

        # Extract timestamp from stats[2] if valid, otherwise use current time
        if stats[1] == "OK" and len(stats) > 2:
            try:
                if (not isinstance(stats[2], (bool, dbus.Boolean)) and
                    hasattr(stats[2], '__len__') and len(stats[2]) == 2):
                    self.timestamp = (int(stats[2][0]), int(stats[2][1]))
                else:
                    self.timestamp = (int(time.time()), 0)
            except (TypeError, IndexError, ValueError):
                self.timestamp = (int(time.time()), 0)
        else:
            self.timestamp = (int(time.time()), 0)

    @staticmethod
    def _parse_flat_ops_stats(stats, header, span):
        """Parse flat struct format from GetClientAllops.

        Format: [op_name, val1, val2, ..., op_name, val1, val2, ...]
        """
        result = {}
        op_count = int(len(stats) / span)
        for op_idx in range(op_count):
            name = dbus_to_std(stats[op_idx * span])
            counters = {}
            for i in range(len(header)):
                counter_idx = op_idx * span + i + 1
                counters[header[i]] = dbus_to_std(stats[counter_idx])
            result[name] = counters
        return result

    @staticmethod
    def _parse_array_ops_stats(stats, header):
        """Parse array of structs format from GetAllClientAllops.

        Format: [struct(op_name, val1, val2, ...), struct(...), ...]
        """
        result = {}
        for op_struct in stats:
            name = dbus_to_std(op_struct[0])
            counters = {}
            for i in range(len(header)):
                counters[header[i]] = dbus_to_std(op_struct[i+1])
            result[name] = counters
        return result

    def report(self):
        """Override report() to handle the custom format with status/error/timestamp"""
        header = {
            'status': {},
        }

        if self.status != "OK":
            header['status']['success'] = False
            header['status']['error'] = self.status
            return header

        header['status']['success'] = True
        header['status']['time'] = timestr(self.timestamp)

        self.fill_report(header)
        return header

    def fill_report(self, report):
        """Generate JSON report for client's all operations.

        Handles both GetClientAllops and GetAllClientAllops data formats.
        """
        cnt = self.STATS_START_INDEX
        for proto_idx, proto in enumerate(NFS_ALLOPS_PROTO_NAMES):
            header = NFS_ALLOPS_HEADERS[proto_idx]

            if cnt >= len(self.stats):
                break

            stats_available = bool(self.stats[cnt])
            cnt += 1

            if stats_available:
                if cnt >= len(self.stats):
                    break

                proto_stats = self.stats[cnt]
                result = {}

                # NFSv4 compounds have unique format (not an array of operations)
                if proto == 'nfsv4_compounds':
                    if isinstance(proto_stats, (dbus.Struct, tuple)):
                        # GetClientAllops: struct with 3 summary values
                        for i in range(len(header)):
                            result[header[i]] = dbus_to_std(proto_stats[i])
                        cnt += 1
                    else:
                        # GetAllClientAllops: 3 separate uint64 values
                        result[header[0]] = dbus_to_std(proto_stats)
                        result[header[1]] = dbus_to_std(self.stats[cnt+1]) if (cnt+1) < len(self.stats) else 0
                        result[header[2]] = dbus_to_std(self.stats[cnt+2]) if (cnt+2) < len(self.stats) else 0
                        cnt += 3
                # Other protocols (NFSv3, NLMv4, NFSv4) have operation arrays
                elif isinstance(proto_stats, (dbus.Array, list)):
                    if len(proto_stats) > 0 and isinstance(proto_stats[0], (dbus.Struct, tuple)):
                        # GetAllClientAllops: array of operation structs
                        result = self._parse_array_ops_stats(proto_stats, header)
                    cnt += 1
                elif isinstance(proto_stats, (dbus.Struct, tuple)) and len(proto_stats) > 0:
                    if isinstance(proto_stats[0], dbus.String):
                        # GetClientAllops: flat struct with alternating name/values
                        span = len(header) + 1
                        result = self._parse_flat_ops_stats(proto_stats, header, span)
                    cnt += 1
                else:
                    cnt += 1

                if result:
                    report[proto] = result
            else:
                # Protocol not available - skip stats if present
                if cnt < len(self.stats):
                    next_elem = self.stats[cnt]
                    if proto == 'nfsv4_compounds':
                        # GetAllClientAllops has 3 uint64 zeros, GetClientAllops has nothing
                        if not isinstance(next_elem, (bool, dbus.Boolean)):
                            cnt += 3
                    else:
                        # GetAllClientAllops has empty array, GetClientAllops has nothing
                        if isinstance(next_elem, (dbus.Array, list)):
                            cnt += 1

    def __str__(self):
        output = ""
        cnt = 3
        if self.status != "OK":
            return ("GANESHA RESPONSE STATUS: " + self.status)
        else:
            output += "\nClient last active at: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs"
            # Convert dbus.Boolean to Python bool for proper evaluation
            if bool(self.stats[cnt]) if cnt < len(self.stats) else False:
                output += "\n\t\tNFSv3 Operations"
                output += "\nOp Name    \t\t total \t errors     dups"
                # Check if stats[cnt+1] is a list/array, not a boolean
                if (cnt+1) < len(self.stats) and not isinstance(self.stats[cnt+1], (bool, dbus.Boolean)):
                    stats_data = self.stats[cnt+1]
                    # Check if it's an array of structs (GetAllClientAllops) or a single struct (GetClientAllops)
                    if isinstance(stats_data, (dbus.Array, list)) and len(stats_data) > 0 and isinstance(stats_data[0], (dbus.Struct, tuple)):
                        # GetAllClientAllops format - array of structs
                        for op_struct in stats_data:
                            if len(op_struct) >= 4:
                                output += "\n" + str(op_struct[0]).ljust(21)
                                output += " %s" % (str(op_struct[1]).rjust(9))
                                output += " %s" % (str(op_struct[2]).rjust(9))
                                output += " %s" % (str(op_struct[3]).rjust(9))
                    else:
                        # GetClientAllops format - single struct with flat data
                        tot_len = len(stats_data)
                        i=0
                        while (i+4) <= tot_len:
                            output += "\n" + str(stats_data[i+0]).ljust(21)
                            output += " %s" % (str(stats_data[i+1]).rjust(9))
                            output += " %s" % (str(stats_data[i+2]).rjust(9))
                            output += " %s" % (str(stats_data[i+3]).rjust(9))
                            i += 4
                cnt += 2
            else:
                output += "\n\tNo NFSv3 activity"
                # Check if we need to skip stats array for GetAllClientAllops
                if (cnt+1) < len(self.stats) and isinstance(self.stats[cnt+1], (dbus.Array, list)):
                    # Empty array present (GetAllClientAllops)
                    cnt += 2  # Skip boolean + array
                else:
                    # No array present (GetClientAllops)
                    cnt += 1
            if bool(self.stats[cnt]) if cnt < len(self.stats) else False:
                output += "\n\t\tNLMv4 Operations"
                output += "\nOp Name    \t\t total \t errors     dups"
                # Check if stats[cnt+1] is a list/array, not a boolean
                if (cnt+1) < len(self.stats) and not isinstance(self.stats[cnt+1], (bool, dbus.Boolean)):
                    stats_data = self.stats[cnt+1]
                    # Check if it's an array of structs (GetAllClientAllops) or a single struct (GetClientAllops)
                    if isinstance(stats_data, (dbus.Array, list)) and len(stats_data) > 0 and isinstance(stats_data[0], (dbus.Struct, tuple)):
                        # GetAllClientAllops format - array of structs
                        for op_struct in stats_data:
                            if len(op_struct) >= 4:
                                output += "\n" + str(op_struct[0]).ljust(21)
                                output += " %s" % (str(op_struct[1]).rjust(9))
                                output += " %s" % (str(op_struct[2]).rjust(9))
                                output += " %s" % (str(op_struct[3]).rjust(9))
                    else:
                        # GetClientAllops format - single struct with flat data
                        tot_len = len(stats_data)
                        i=0
                        while (i+4) <= tot_len:
                            output += "\n" + str(stats_data[i+0]).ljust(21)
                            output += " %s" % (str(stats_data[i+1]).rjust(9))
                            output += " %s" % (str(stats_data[i+2]).rjust(9))
                            output += " %s" % (str(stats_data[i+3]).rjust(9))
                            i += 4
                cnt += 2
            else:
                output += "\n\tNo NLMv4 activity"
                # Check if we need to skip stats array for GetAllClientAllops
                if (cnt+1) < len(self.stats) and isinstance(self.stats[cnt+1], (dbus.Array, list)):
                    # Empty array present (GetAllClientAllops)
                    cnt += 2  # Skip boolean + array
                else:
                    # No array present (GetClientAllops)
                    cnt += 1
            if bool(self.stats[cnt]) if cnt < len(self.stats) else False:
                output += "\n\t\tNFSv4 Operations"
                output += "\nOp Name    \t\t total \t errors"
                # Check if stats[cnt+1] is a list/array, not a boolean
                if (cnt+1) < len(self.stats) and not isinstance(self.stats[cnt+1], (bool, dbus.Boolean)):
                    stats_data = self.stats[cnt+1]
                    # Check if it's an array of structs (GetAllClientAllops) or a single struct (GetClientAllops)
                    if isinstance(stats_data, (dbus.Array, list)) and len(stats_data) > 0 and isinstance(stats_data[0], (dbus.Struct, tuple)):
                        # GetAllClientAllops format - array of structs
                        for op_struct in stats_data:
                            if len(op_struct) >= 3:
                                output += "\n" + str(op_struct[0]).ljust(21)
                                output += " %s" % (str(op_struct[1]).rjust(9))
                                output += " %s" % (str(op_struct[2]).rjust(9))
                    else:
                        # GetClientAllops format - single struct with flat data
                        tot_len = len(stats_data)
                        i=0
                        while (i+3) <= tot_len:
                            output += "\n" + str(stats_data[i+0]).ljust(21)
                            output += " %s" % (str(stats_data[i+1]).rjust(9))
                            output += " %s" % (str(stats_data[i+2]).rjust(9))
                            i += 3
                cnt += 2
            else:
                output += "\n\tNo NFSv4 activity"
                # Check if we need to skip stats array for GetAllClientAllops
                if (cnt+1) < len(self.stats) and isinstance(self.stats[cnt+1], (dbus.Array, list)):
                    # Empty array present (GetAllClientAllops)
                    cnt += 2  # Skip boolean + array
                else:
                    # No array present (GetClientAllops)
                    cnt += 1
            if bool(self.stats[cnt]) if cnt < len(self.stats) else False:
                output += "\n\t\tNFSv4 Compound Operations"
                output += "\n      total \t errors \t Ops in compound"
                # For GetAllClientAllops: compounds are 3 separate uint64 values
                # For GetClientAllops: compounds are in a struct
                if (cnt+1) < len(self.stats):
                    next_elem = self.stats[cnt+1]
                    if isinstance(next_elem, (dbus.Struct, tuple)):
                        # GetClientAllops format - stats in a struct
                        output += "\n %s" % (str(dbus_to_std(next_elem[0])).rjust(9))
                        output += " %s" % (str(dbus_to_std(next_elem[1])).rjust(9))
                        output += " %s" % (str(dbus_to_std(next_elem[2])).rjust(9))
                        cnt += 2  # Skip boolean + struct
                    elif isinstance(next_elem, (dbus.Array, list)):
                        # This shouldn't happen for compounds, but handle it gracefully
                        # This means we hit an array from a previous protocol that wasn't properly skipped
                        output += "\n      (Error: unexpected array in compound stats)"
                        cnt += 1
                    else:
                        # GetAllClientAllops format - 3 separate uint64 values
                        # Make sure we have enough elements
                        if (cnt+3) < len(self.stats):
                            output += "\n %s" % (str(int(self.stats[cnt+1])).rjust(9))
                            output += " %s" % (str(int(self.stats[cnt+2])).rjust(9))
                            output += " %s" % (str(int(self.stats[cnt+3])).rjust(9))
                            cnt += 4  # Skip boolean + 3 uint64 values
                        else:
                            output += "\n      (Error: insufficient data for compound stats)"
                            cnt += 1
                else:
                    cnt += 1
            else:
                output += "\n\tNo NFSv4 compound ops"
                # Check if we need to skip stats for GetAllClientAllops
                # For GetAllClientAllops when compounds unavailable, there are 3 uint64 zeros after the boolean
                if (cnt+1) < len(self.stats) and not isinstance(self.stats[cnt+1], (bool, dbus.Boolean)):
                    # Check if it's an array (shouldn't be for compounds when unavailable)
                    if isinstance(self.stats[cnt+1], (dbus.Array, list)):
                        # This is wrong - compounds don't have arrays
                        cnt += 1
                    else:
                        # Stats present even though unavailable (GetAllClientAllops)
                        cnt += 4  # Skip boolean + 3 uint64 values
                else:
                    # No stats present (GetClientAllops)
                    cnt += 1
            return output

class Export(ProtocolsStats):
    def __init__(self, export):
        super().__init__(export[2])
        self.exportid = export[0]
        self.path = export[1]
        self.nfsv3_stats_avail = self.protocols_stats['NFSv3']
        self.nfsv40_stats_avail = self.protocols_stats['NFSv40']
        self.nfsv41_stats_avail = self.protocols_stats['NFSv41']
        self.nfsv42_stats_avail = self.protocols_stats['NFSv42']
        self.mnt_stats_avail = self.protocols_stats['MNT']
        self.nlmv4_stats_avail = self.protocols_stats['NLMv4']
        self.rquota_stats_avail = self.protocols_stats['RQUOTA']
        self._9p_stats_avail = self.protocols_stats['9P']

    def __str__(self):
        return ("\nExport id: " + str(self.exportid) +
                "\n\tPath: " + self.path +
                "\n\tNFSv3 stats available: " + str(self.nfsv3_stats_avail) +
                "\n\tNFSv4.0 stats available: " + str(self.nfsv40_stats_avail) +
                "\n\tNFSv4.1 stats available: " + str(self.nfsv41_stats_avail) +
                "\n\tNFSv4.2 stats available: " + str(self.nfsv42_stats_avail) +
                "\n\tMNT stats available: " + str(self.mnt_stats_avail) +
                "\n\tNLMv4 stats available: " + str(self.nlmv4_stats_avail) +
                "\n\tRQUOTA stats available: " + str(self.rquota_stats_avail) +
                "\n\t9P stats available: " + str(self._9p_stats_avail) + "\n")


class ExportStats(Report):
    def __init__(self, stats):
        super().__init__(stats)

        self.stats = stats
        self.curtime = time.time()
        self.timestamp = (stats[0][0], stats[0][1])
        self.exports = {}
        for export in stats[1]:
            exportid = export[0]
            self.exports[exportid] = Export(export)

    def fill_report(self, report):
        report['exports'] = []

        for exportid, export in self.exports.items():
            export_report = {
                'id': dbus_to_std(exportid),
                'path': dbus_to_std(export.path),
                'nfsv3_stats': dbus_to_std(export.nfsv3_stats_avail),
                'nfsv40_stats': dbus_to_std(export.nfsv40_stats_avail),
                'nfsv41_stats': dbus_to_std(export.nfsv41_stats_avail),
                'nfsv42_stats': dbus_to_std(export.nfsv42_stats_avail),
                'mnt_stats': dbus_to_std(export.mnt_stats_avail),
                'nlmv4_stats': dbus_to_std(export.nlmv4_stats_avail),
                'rquota_stats': dbus_to_std(export.rquota_stats_avail),
                '9p_stats': dbus_to_std(export._9p_stats_avail),
            }
            report['exports'].append(export_report)

    def __str__(self):
        self.starttime = self.timestamp[0] + self.timestamp[1] / 1e9
        self.duration = self.curtime - self.starttime
        output = ("Export Stats \n" +
                  "Stats collected since: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs" +
                  "\nDuration: " + "%.10f" % self.duration + " seconds")
        for exportid in self.exports:
            output += str(self.exports[exportid])
        return output

    def exportids(self):
        return self.exports.keys()

class ExportDetails(Report):
    def __init__(self, stats):
        super().__init__(stats)

        self.stats = stats
        self.status = stats[1]
        if stats[1] == "OK":
            self.timestamp = (stats[2][0], stats[2][1])

    def fill_report(self, report):
        proto_table = [
            'nfsv3',
            'nfsv40',
            'nfsv41',
            'nfsv42'
        ]
        op_table = [
            'read',
            'write',
            'other',
            'layout'
        ]
        stats = self.result[3:]

        def op_stats(stats):
            counters = [
                'total',
                'errors',
                'latency',
                'transferred'
            ]

            result = {}
            for i in range(len(stats)):
                result[counters[i]] = dbus_to_std(stats[i])

            return result

        i = 0
        for proto in proto_table:
            # Checks that the current stat field is a boolean
            # indicating if stats are available for the given protocol.
            available = stats[i]
            i += 1
            assert isinstance(available, dbus.Boolean)

            if not available:
                continue

            # Now greedily takes stats until we reach the next boolean (and so
            # the next protocol).
            ops = iter(op_table)
            found_stats = []
            report[proto] = {}
            while i < len(stats) and not isinstance(stats[i], dbus.Boolean):
                op_name = next(ops)
                report[proto][op_name] = op_stats(stats[i])
                i += 1

    def __str__(self):
        output = ""
        cnt = 3
        if self.status != "OK":
            return ("GANESHA RESPONSE STATUS: " + self.status)
        else:
            output += "\nExport last accessed at: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs"
            output += "\n   Latency is in milliseconds for Read/Write/Other Operations"
            j = 0
            while j<4:
                if self.stats[cnt]:
                    i = 0
                    if j==0:
                        output += "\n\t\tNFSv3:"
                    if j==1:
                        output += "\n\t\tNFSv4.0:"
                    if j==2:
                        output += "\n\t\tNFSv4.1:"
                    if j==3:
                        output += "\n\t\tNFSv4.2:"
                    output += "\n\t     total \t errors      latency/delays     transferred"
                    while  i<4:
                        if i==0:
                            output += "\nREAD :"
                        if i==1:
                            output += "\nWRITE:"
                        if i==2:
                            output += "\nOther:"
                        if i==3:
                            output += "\nLayout:"
                        output += "%12d" % self.stats[cnt+i+1][0]
                        output += " %12d" % self.stats[cnt+i+1][1]
                        if (j>1 and i==3):
                            output += " %12d" % self.stats[cnt+i+1][2]
                        else:
                            output += " %18.6f" % self.stats[cnt+i+1][2]
                        if i<2:
                            output += " %12d" % self.stats[cnt+i+1][3]
                        if (i==2 and j<2):
                            i += 1
                        i += 1
                    if j<2:
                        cnt += 4
                    else:
                        cnt += 5
                else:
                    if j==0:
                        output += "\n\tNo NFSv3 activity"
                    if j==1:
                        output += "\n\tNo NFSv4.0 activity"
                    if j==2:
                        output += "\n\tNo NFSv4.1 activity"
                    if j==3:
                        output += "\n\tNo NFSv4.2 activity"
                    cnt += 1
                j += 1
            return output

class GlobalStats(Report):
    def __init__(self, stats):
        super().__init__(stats)

        self.curtime = time.time()
        self.success = stats[0]
        self.status = stats[1]
        if self.success:
            self.timestamp = (stats[2][0], stats[2][1])
            self.nfsv3_total = stats[3][1]
            self.nfsv40_total = stats[3][3]
            self.nfsv41_total = stats[3][5]
            self.nfsv42_total = stats[3][7]

    def fill_report(self, report):
        report_key_value(self.result[3], report)

    def __str__(self):
        output = ""
        if not self.success:
            return "No NFS activity, GANESHA RESPONSE STATUS: " + self.status
        if self.status != "OK":
            output += self.status + "\n"
        self.starttime = self.timestamp[0] + self.timestamp[1] / 1e9
        self.duration = self.curtime - self.starttime
        output += ("Global Stats \n" +
                   "Stats collected since: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs" +
                   "\nDuration: " + "%.10f" % self.duration + " seconds" +
                   "\nTotal NFSv3 ops: " + str(self.nfsv3_total) +
                   "\nTotal NFSv4.0 ops: " + str(self.nfsv40_total) +
                   "\nTotal NFSv4.1 ops: " + str(self.nfsv41_total) +
                   "\nTotal NFSv4.2 ops: " + str(self.nfsv42_total))
        return output

class InodeStats(Report):
    def __init__(self, stats):
        super().__init__(stats)
        self.stats = stats

    def fill_report(self, report):
        values = self.result[3] + self.result[4]
        report_key_value(values, report)

    def __str__(self):
        output = ""
        if self.stats[1] != "OK":
            return "No NFS activity, GANESHA RESPONSE STATUS: " + self.stats[1]
        else:
            output += "\nTimestamp: " + time.ctime(self.stats[2][0]) + str(self.stats[2][1]) + " nsecs\n"
            output += "\nInode Cache statistics"
            output += "\n" + (self.stats[3][0]).ljust(25) + "%s" % (str(self.stats[3][1]).rjust(20))
            output += "\n" + (self.stats[3][2]).ljust(25) + "%s" % (str(self.stats[3][3]).rjust(20))
            output += "\n" + (self.stats[3][4]).ljust(25) + "%s" % (str(self.stats[3][5]).rjust(20))
            output += "\n" + (self.stats[3][6]).ljust(25) + "%s" % (str(self.stats[3][7]).rjust(20))
            output += "\n" + (self.stats[3][8]).ljust(25) + "%s" % (str(self.stats[3][9]).rjust(20))
            output += "\n" + (self.stats[3][10]).ljust(25) + "%s" % (str(self.stats[3][11]).rjust(20))
            output += "\n\nLRU Utilization Data"
            output += "\n" + (self.stats[4][0]).ljust(25) + "%s" % (str(self.stats[4][1]).rjust(20))
            output += "\n" + (self.stats[4][2]).ljust(25) + "%s" % (str(self.stats[4][3]).rjust(20))
            output += "\n" + (self.stats[4][4]).ljust(25) + (self.stats[4][5]).ljust(30)
            output += "\n" + (self.stats[4][6]).ljust(25) + "%s" % (str(self.stats[4][7]).rjust(20))
            output += "\n" + (self.stats[4][8]).ljust(25) + "%s" % (str(self.stats[4][9]).rjust(20))
        return output


class FastStats(Report):
    def __init__(self, stats):
        super().__init__(stats)

        self.curtime = time.time()
        self.stats = stats

    def fill_report(self, report):
        stats = self.stats[3]
        current_category = None
        current_op = None

        for i in range(0, len(stats)):
            if FastStats.is_header_string(stats[i]):
                category = str(stats[i])
                name = str(category)[:-1].strip()
                report[name] = {}
                current_category = name
            else:
                assert current_category is not None

                if FastStats.is_stat_value(stats[i]):
                    assert current_op is not None
                    report[current_category][current_op] = int(stats[i])
                    current_op = None
                else:
                    current_op = str(stats[i])

    def __str__(self):
        if not self.stats[0]:
            return "No NFS activity, GANESHA RESPONSE STATUS: " + self.stats[1]
        else:
            if self.stats[1] != "OK":
                output = self.stats[1]+ "\n"
            else:
                output = ""
            self.starttime = self.stats[2][0] + self.stats[2][1] / 1e9
            self.duration = self.curtime - self.starttime
            output += ("Fast stats" +
                       "\nStats collected since: " + time.ctime(self.stats[2][0]) + str(self.stats[2][1]) + " nsecs" +
                       "\nDuration: " + "%.10f" % self.duration + " seconds" +
                       "\nGlobal ops:\n")
            # NFSv3, NFSv4, NLM, MNT, QUOTA self.stats
            for i in range(len(self.stats[3])):
                if FastStats.is_header_string(self.stats[3][i]):
                    output += self.stats[3][i] + "\n"
                elif FastStats.is_stat_value(self.stats[3][i]):
                    output += "\t%s" % (str(self.stats[3][i]).rjust(8)) + "\n"
                else:
                    output += "%s: " % (self.stats[3][i].ljust(20))
        return output

    def is_header_string(x):
        return (isinstance(x, dbus.String)
                and x[-1] == ':')

    def is_stat_value(x):
        return isinstance(x, dbus.UInt64)

class ExportIOv3Stats(Report):
    def __init__(self, stats):
        super().__init__(stats)
        self.stats = stats

    def report(self):
        return export_io_stats_report(self._header, self.result)

    def __str__(self):
        output = ""
        for key in self.stats:
            if not self.stats[key][0]:
                output += "EXPORT %s: %s\n" % (key, self.stats[key][1])
                continue
            if self.stats[key][1] != "OK":
                output += self.stats[key][1] + "\n"
            output += ("\nEXPORT %s:" % (key) +
                       "\n\t\trequested\ttransferred\t     total\t    errors\t   latency" +
                       "\nREADv3: ")
            for stat in self.stats[key][3]:
                output += "\t" + str(stat).rjust(8)
            output += "\nWRITEv3: "
            for stat in self.stats[key][4]:
                output += "\t" + str(stat).rjust(8)
        return output

class ExportIOv4Stats(Report):
    def __init__(self, stats):
        super().__init__(stats)
        self.stats = stats

    def report(self):
        return export_io_stats_report(self._header, self.result)

    def __str__(self):
        output = ""
        for key in self.stats:
            if not self.stats[key][0]:
                output += "\nEXPORT %s: %s\n" % (key, self.stats[key][1])
                continue
            if self.stats[key][1] != "OK":
                output += self.stats[key][1] + "\n"
            output += ("EXPORT %s:" % (key) +
                       "\n\t\trequested\ttransferred\t     total\t    errors\t   latency" +
                       "\nREADv4: ")
            for stat in self.stats[key][3]:
                output += "\t" + str(stat).rjust(8)
            output += "\nWRITEv4: "
            for stat in self.stats[key][4]:
                output += "\t" + str(stat).rjust(8)
            output += "\n\n"
        return output

class ExportIOv41Stats(Report):
    def __init__(self, stats):
        super().__init__(stats)
        self.stats = stats

    def report(self):
        return export_io_stats_report(self._header, self.result)

    def __str__(self):
        output = ""
        for key in self.stats:
            if not self.stats[key][0]:
                output += "\nEXPORT %s: %s\n" % (key, self.stats[key][1])
                continue
            if self.stats[key][1] != "OK":
                output += self.stats[key][1] + "\n"
            output += ("EXPORT %s:" % (key) +
                       "\n\t\trequested\ttransferred\t     total\t    errors\t   latency" +
                       "\nREADv41: ")
            for stat in self.stats[key][3]:
                output += "\t" + str(stat).rjust(8)
            output += "\nWRITEv41: "
            for stat in self.stats[key][4]:
                output += "\t" + str(stat).rjust(8)
            output += "\n\n"
        return output

class ExportIOv42Stats(Report):
    def __init__(self, stats):
        super().__init__(stats)
        self.stats = stats

    def report(self):
        return export_io_stats_report(self._header, self.result)

    def __str__(self):
        output = ""
        for key in self.stats:
            if not self.stats[key][0]:
                output += "\nEXPORT %s: %s\n" % (key, self.stats[key][1])
                continue
            if self.stats[key][1] != "OK":
                output += self.stats[key][1] + "\n"
            output += ("EXPORT %s:" % (key) +
                       "\n\t\trequested\ttransferred\t     total\t    errors\t   latency" +
                       "\nREADv42: ")
            for stat in self.stats[key][3]:
                output += "\t" + str(stat).rjust(8)
            output += "\nWRITEv42: "
            for stat in self.stats[key][4]:
                output += "\t" + str(stat).rjust(8)
            output += "\n\n"
        return output

class ExportIOMonStats(Report):
    def __init__(self, stats):
        super().__init__(stats)
        self.stats = stats

    def report(self):
        return export_io_stats_report(self._header, self.result)

    def __str__(self):
        output = ""
        for key in self.stats:
            if not self.stats[key][0]:
                output += "\nEXPORT %s: %s\n" % (key, self.stats[key][1])
                continue
            if self.stats[key][1] != "OK":
                output += self.stats[key][1] + "\n"
            output += ("EXPORT %s:" % (key) +
                       "\t    BW(MB/s)\t" +
                       "\nREAD: ")
            output += "\t\t" + str(self.stats[key][3][2]).rjust(8)
            output += "\nWRITE: "
            output += "\t\t" + str(self.stats[key][4][2]).rjust(8)
            output += "\nTotal: "
            output += "\t\t" + str(self.stats[key][3][2] + self.stats[key][4][2]).rjust(8)
            output += "\n\n"
        return output

def export_io_stats_report(header, stats):
    reports = []

    ops = [
        'read',
        'write',
    ]

    counters = [
        'requested',
        'transferred',
        'total',
        'errors',
        'latency',
    ]

    for exportid, export_stats in stats.items():
        report, success = header(export_stats)
        report['id'] = exportid

        if not success:
            reports.append(report)
            continue

        op_stat_start = 3
        for i, op in enumerate(ops):
            ops_stats = export_stats[op_stat_start + i]
            result = {}


            for j, counter in enumerate(counters):
                result[counter] = dbus_to_std(ops_stats[j])

            report[op] = result
        reports.append(report)
    return reports

class TotalStats(Report):
    def __init__(self, stats):
        super().__init__(stats)

        self.curtime = time.time()
        self.stats = stats

    def report(self):
        reports = []

        for exportid in self.result:
            export = self.result[exportid]
            report, success = super()._header(export)
            report['id'] = exportid
            reports.append(report)

            if not success:
                continue

            report_key_value(export[3], report)

        return reports

    def __str__(self):
        output = ""
        key = next(iter(self.stats))
        self.starttime = self.stats[key][2][0] + self.stats[key][2][1] / 1e9
        self.duration = self.curtime - self.starttime
        output += ("Total stats for export(s) \n" +
                   "Stats collected since: " + time.ctime(self.stats[key][2][0]) +
                   str(self.stats[key][2][1]) + " nsecs" +
                   "\nDuration: " + "%.10f" % self.duration + " seconds ")
        for key in self.stats:
            if not self.stats[key][0]:
                return "No NFS activity, GANESHA RESPONSE STATUS: " + self.stats[key][1]
            if self.stats[key][1] != "OK":
                output += self.stats[key][1] + "\n"
            output += "\nExport id: " + str(key)
            for i in range(0, len(self.stats[key][3])-1, 2):
                output += "\n\t%s: %s" % (self.stats[key][3][i], self.stats[key][3][i+1])
        return output

class PNFSStats(Report):
    def __init__(self, stats):
        super().__init__(stats)

        self.curtime = time.time()
        self.stats = stats

    def report(self):
        reports = []

        ops = [
            'layout_get',
            'layout_commit',
            'layout_return',
            'recall'
        ]

        counters = [
            'total',
            'errors',
            'delays'
        ]

        for exportid, export_stats in self.stats.items():
            report, success = self._header(export_stats)
            reports.append(report)

            if not success:
                continue

            stats_offset = 3
            for i, op in enumerate(ops):
                result = {}
                report[op] = result

                stats = export_stats[stats_offset + i]
                for i_counter, counter in enumerate(counters):
                    result[counter] = dbus_to_std(stats[i_counter])

        return reports

    def __str__(self):
        output = "PNFS stats for export(s)"
        for key in self.stats:
            if self.stats[key][1] != "OK":
                return "No NFS activity, GANESHA RESPONSE STATUS: \n" + self.stats[key][1]
            self.starttime = self.stats[key][2][0] + self.stats[key][2][1] / 1e9
            self.duration = self.curtime - self.starttime
            output += ("\nStats collected since: " + time.ctime(self.stats[key][2][0]) + str(self.stats[key][2][1]) + " nsecs" +
                       "\nDuration: " + "%.10f" % self.duration + " seconds\n")
            output += "\nStatistics for export id: "+ str(key)
            output += "\n\t\ttotal\terrors\tdelays"
            output += "\ngetdevinfo "
            for stat in self.stats[key][3]:
                output += "\t" + str(stat)
            output += "\nlayout_get "
            for stat in self.stats[key][4]:
                output += "\t" + str(stat)
            output += "\nlayout_commit "
            for stat in self.stats[key][5]:
                output += "\t" + str(stat)
            output += "\nlayout_return "
            for stat in self.stats[key][6]:
                output += "\t" + str(stat)
            output += "\nrecall \t"
            for stat in self.stats[key][7]:
                output += "\t" + str(stat)
        return output

class StatsReset():
    def __init__(self, status):
        self.status = status

    def __str__(self):
        if self.status[1] != "OK":
            return "Failed to reset statistics, GANESHA RESPONSE STATUS: " + self.status[1]
        else:
            return "Successfully reset statistics counters"

class StatsStatus(Report):
    def __init__(self, status):
        super().__init__(status)

        self.status = status

    def fill_report(self, report):
        proto_table = [
            'nfs',
            'fsal',
            'nfsv3',
            'nfsv4',
            'auth',
            'client'
        ]

        for (i, status) in enumerate(self.result[2:]):
            enabled = dbus_to_std(status[0])

            result = {
                "enabled": enabled
            }
            report[proto_table[i]] = result

            if enabled:
                result["since"] = timestr(status[1])

    def __str__(self):
        output = ""
        if not self.status[0]:
            return "Unable to fetch current status of stats counting: " + self.status[1]
        else:
            if self.status[2][0]:
                output += "Stats counting for NFS server is enabled since: \n\t"
                output += time.ctime(self.status[2][1][0]) + str(self.status[2][1][1]) + " nsecs\n"
            else:
                output += "Stats counting for NFS server is currently disabled\n"
            if self.status[3][0]:
                output += "Stats counting for FSAL is enabled since: \n\t"
                output += time.ctime(self.status[3][1][0]) + str(self.status[3][1][1]) + " nsecs\n"
            else:
                output += "Stats counting for FSAL is currently disabled \n"
            if self.status[4][0]:
                output += "Stats counting for v3_full is enabled since: \n\t"
                output += time.ctime(self.status[4][1][0]) + str(self.status[4][1][1]) + " nsecs\n"
            else:
                output += "Stats counting for v3_full is currently disabled \n"
            if self.status[5][0]:
                output += "Stats counting for v4_full is enabled since: \n\t"
                output += time.ctime(self.status[5][1][0]) + str(self.status[5][1][1]) + " nsecs\n"
            else:
                output += "Stats counting for v4_full is currently disabled \n"
            if self.status[6][0]:
                output += "Stats counting for authentication is enabled since: \n\t"
                output += time.ctime(self.status[6][1][0]) + str(self.status[6][1][1]) + " nsecs\n"
            else:
                output += "Stats counting for authentication is currently disabled \n"
            if self.status[7][0]:
                output += "Stats counting of all ops for client is enabled since: \n\t"
                output += time.ctime(self.status[7][1][0]) + str(self.status[7][1][1]) + " nsecs\n"
            else:
                output += "Stats counting of all ops for client is currently disabled \n"
            return output


class DumpFSALStats(Report):
    def __init__(self, stats):
        self.curtime = time.time()
        self.stats = stats

    def __str__(self):
        output = ""
        if not self.stats[0]:
            return "GANESHA RESPONSE STATUS: " + self.stats[1]
        else:
            if self.stats[3] == "GPFS":
                if self.stats[5] != "OK":
                    output += "No stats available for display"
                    return output
                self.starttime = self.stats[2][0] + self.stats[2][1] / 1e9
                self.duration = self.curtime - self.starttime
                output += "FSAL stats for - GPFS \n"
                output += "Stats collected since: " + time.ctime(self.stats[2][0]) + str(self.stats[2][1]) + " nsecs\n"
                output += "Duration: " + "%.10f" % self.duration + " seconds\n"
                tot_len = len(self.stats[4])
                output += "FSAL Stats (response time in milliseconds): \n"
                output += "\tOp-Name         Total     Res:Avg         Min           Max"
                i = 0
                while (i+5) <= tot_len:
                    output += "\n" + (self.stats[4][i+0]).ljust(20)
                    output += " %s" % (str(self.stats[4][i+1]).rjust(8))
                    output += " %12.6f" % (self.stats[4][i+2])
                    output += " %12.6f" % (self.stats[4][i+3])
                    output += " %12.6f" % (self.stats[4][i+4])
                    i += 5
                return output

class StatsEnable():
    def __init__(self, status):
        self.status = status

    def __str__(self):
        if self.status[1] != "OK":
            return "Failed to enable statistics counting, GANESHA RESPONSE STATUS: " + self.status[1]
        else:
            return "Successfully enabled statistics counting"

class StatsDisable():
    def __init__(self, status):
        self.status = status

    def __str__(self):
        if self.status[1] != "OK":
            return "Failed to disable statistics counting, GANESHA RESPONSE STATUS: " + self.status[1]
        else:
            return "Successfully disabled statistics counting"

class DumpAuth(Report):
    def __init__(self, stats):
        super().__init__(stats)

        self.curtime = time.time()
        self.success = stats[0]
        self.status = stats[1]
        if self.success:
            self.timestamp = (stats[2][0], stats[2][1])
            self.gctotal = stats[3][0]
            self.gclatency = stats[3][1]
            self.gcmax = stats[3][2]
            self.gcmin = stats[3][3]
            self.wbtotal = stats[3][4]
            self.wblatency = stats[3][5]
            self.wbmax = stats[3][6]
            self.wbmin = stats[3][7]
            self.dnstotal = stats[3][8]
            self.dnslatency = stats[3][9]
            self.dnsmax = stats[3][10]
            self.dnsmin = stats[3][11]

    def fill_report(self, report):
        report["gc"] = {
            "total": dbus_to_std(self.gctotal),
            "latency": dbus_to_std(self.gclatency),
            "max": dbus_to_std(self.gcmax),
            "min": dbus_to_std(self.gcmin),
        }
        report["wb"] = {
            "total": dbus_to_std(self.wbtotal),
            "latency": dbus_to_std(self.wblatency),
            "max": dbus_to_std(self.wbmax),
            "min": dbus_to_std(self.wbmin),
        }
        report["dns"] = {
            "total": dbus_to_std(self.dnstotal),
            "latency": dbus_to_std(self.dnslatency),
            "max": dbus_to_std(self.dnsmax),
            "min": dbus_to_std(self.dnsmin)
        }

    def __str__(self):
        output = ""
        if not self.success:
            return "No auth activity, GANESHA RESPONSE STATUS: " + self.status
        if self.status != "OK":
            output += self.status + "\n"
        self.starttime = self.timestamp[0] + self.timestamp[1] / 1e9
        self.duration = self.curtime - self.starttime
        output += ("Authentication related stats" +
                   "\nStats collected since: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs"+
                   "\nDuration: " + "%.10f" % self.duration + " seconds\n" +
                   "\n\nGroup Cache" +
                   "\n\tTotal ops: " + str(self.gctotal) +
                   "\n\tAve Latency: " + str(self.gclatency) +
                   "\n\tMax Latency: " + str(self.gcmax) +
                   "\n\tMin Latency: " + str(self.gcmin) +
                   "\n\nWinbind" +
                   "\n\tTotal ops: " + str(self.wbtotal) +
                   "\n\tAve Latency: " + str(self.wblatency) +
                   "\n\tMax Latency: " + str(self.wbmax) +
                   "\n\tMin Latency: " + str(self.wbmin) +
                   "\n\nDNS" +
                   "\n\tTotal ops: " + str(self.dnstotal) +
                   "\n\tAve Latency: " + str(self.dnslatency) +
                   "\n\tMax Latency: " + str(self.dnsmax) +
                   "\n\tMin Latency: " + str(self.dnsmin))
        return output


class DumpFULLV3Stats(Report):
    def __init__(self, status):
        super().__init__(status)

        self.curtime = time.time()
        self.stats = status

    def fill_report(self, report):
        if self.result[4] == 'None':
            return

        for op_stats in self.result[3]:
            name = dbus_to_std(op_stats[0])
            report[name] = {
                "details": {
                    "total": dbus_to_std(op_stats[1]),
                    "error": dbus_to_std(op_stats[2]),
                    "dups": dbus_to_std(op_stats[3])
                },
                "latency": {
                    "average": dbus_to_std(op_stats[4]),
                    "min": dbus_to_std(op_stats[5]),
                    "max": dbus_to_std(op_stats[6])
                }
            }

    def __str__(self):
        output = ""
        if not self.stats[0]:
            return "Unable to fetch Detailed NFSv3 stats - " + self.stats[1]
        else:
            if self.stats[4] != "OK":
                output += "\n No stats available for display"
                return output
            self.starttime = self.stats[2][0] + self.stats[2][1] / 1e9
            self.duration = self.curtime - self.starttime
            output += "NFSv3 Detailed statistics \n"
            output += "Stats collected since: " + time.ctime(self.stats[2][0]) + str(self.stats[2][1]) + " nsecs\n"
            output += "Duration: " + "%.10f" % self.duration + " seconds\n"
            output += "\nOperation Details                         |  Operation Latency (in milliseconds)"
            output += "\n==========================================|========================================"
            output += "\nName            Total     Error      Dups |       Avg          Min           Max   "
            i = 0
            tot_len = len(self.stats[3])
            while i < tot_len:
                output += "\n" + (self.stats[3][i][0]).ljust(11)
                output += " %s" % (str(self.stats[3][i][1]).rjust(9))
                output += " %s" % (str(self.stats[3][i][2]).rjust(9))
                output += " %s |" % (str(self.stats[3][i][3]).rjust(9))
                output += " %12.6f" % (self.stats[3][i][4])
                output += " %12.6f" % (self.stats[3][i][5])
                output += " %12.6f" % (self.stats[3][i][6])
                i += 1
            return output

class DumpFULLV4Stats(Report):
    def __init__(self, status):
        super().__init__(status)

        self.curtime = time.time()
        self.stats = status

    def fill_report(self, report):
        if self.result[4] == 'None':
            return

        for op_stats in self.result[3]:
            name = dbus_to_std(op_stats[0])
            report[name] = {
                "details": {
                    "total": dbus_to_std(op_stats[1]),
                    "error": dbus_to_std(op_stats[2]),
                },
                "latency": {
                    "average": dbus_to_std(op_stats[3]),
                    "min": dbus_to_std(op_stats[4]),
                    "max": dbus_to_std(op_stats[5])
                }
            }

    def __str__(self):
        output = ""
        if not self.stats[0]:
            return "Unable to fetch Detailed NFSv4 stats - " + self.stats[1]
        else:
            if self.stats[4] != "OK":
                output += "\n No stats available for display"
                return output
            self.starttime = self.stats[2][0] + self.stats[2][1] / 1e9
            self.duration = self.curtime - self.starttime
            output += "NFSv4 Detailed statistics \n"
            output += "Stats collected since: " + time.ctime(self.stats[2][0]) + str(self.stats[2][1]) + " nsecs\n"
            output += "Duration: " + "%.10f" % self.duration + " seconds\n"
            output += "\nOperation Details                            |  Operation Latency (in milliseconds)"
            output += "\n=============================================|========================================"
            output += "\nName                        Total     Error  |       Avg          Min           Max   "
            i = 0
            tot_len = len(self.stats[3])
            while i < tot_len:
                output += "\n" + (self.stats[3][i][0]).ljust(23)
                output += " %s" % (str(self.stats[3][i][1]).rjust(9))
                output += " %s  |" % (str(self.stats[3][i][2]).rjust(9))
                output += " %12.6f" % (self.stats[3][i][3])
                output += " %12.6f" % (self.stats[3][i][4])
                output += " %12.6f" % (self.stats[3][i][5])
                i += 1
            return output

class RetrieveMemoryStats():
    def __init__(self):
        self.dbus_service_name = "org.ganesha.nfsd"
        self.dbus_memstats_name = "org.ganesha.nfsd.memstats"
        self.mem_interface = "/org/ganesha/nfsd/MemMgr"

        self.bus = dbus.SystemBus()
        self.memmgrobj = self.bus.get_object(self.dbus_service_name,
                                             self.mem_interface)

    # get memory stats (includes capture active/inactive status)
    def get_mem_stats(self):
        stats_state = self.memmgrobj.get_dbus_method("GetMemoryStats",
                                                     self.dbus_memstats_name)
        status_state = self.memmgrobj.get_dbus_method("GetMemoryStatsStatus",
                                                      self.dbus_memstats_name)
        status_result = status_state()
        if status_result[0]:
            capture_status = str(status_result[1])
        else:
            capture_status = "unknown"
        return DumpMemStats(stats_state(), capture_status)

    def get_mem_stats_status(self):
        stats_state = self.memmgrobj.get_dbus_method("GetMemoryStatsStatus",
                                                     self.dbus_memstats_name)
        return MemStatsCaptureStatus(stats_state())

class MemStatsCaptureStatus(Report):
    def __init__(self, stats):
        super().__init__(stats)
        self.success = stats[0]
        self.status = stats[1]

    def report(self):
        out = {'status': {'success': bool(self.success)}}
        if not self.success:
            out['status']['error'] = str(self.status)
        else:
            out['capture_status'] = str(self.status)
        return out

    def __str__(self):
        if not self.success:
            return "GANESHA RESPONSE STATUS: " + str(self.status)
        return "Memory statistics capture: " + str(self.status)

class DumpMemStats(Report):
    # Sent over D-Bus as uint64 but can be negative (see
    # gsh_mem_stats_get_stat_by_index_and_comp() / get_comp_memory_statistics()
    # in server_stats.c, which reinterpret a signed int64_t's bits as
    # uint64_t rather than changing the wire schema). Undo that on read.
    _SIGNED_FIELDS = {
        "Current_Active_Bytes", "Peak_Active_Bytes",
    }
    _STAT_KEYS = (
        "Lifetime_Alloc_Calls", "Lifetime_Free_Calls",
        "Lifetime_Alloc_Bytes", "Lifetime_Freed_Bytes",
        "Current_Active_Bytes", "Peak_Active_Bytes",
    )

    @staticmethod
    def _to_signed64(v):
        v = int(v)
        return v - (1 << 64) if v >= (1 << 63) else v

    def __init__(self, stats, capture_status=None):
        super().__init__(stats)

        self.success = stats[0]
        self.status = stats[1]
        self.capture_status = capture_status

    @staticmethod
    def _iter_comp_stats(stats):
        if stats is None:
            return
        for item in stats:
            try:
                if len(item) != 2:
                    continue
                yield str(item[0]), item[1]
            except TypeError:
                continue

    def report(self):
        report = {'status': {}}
        if self.status != "OK":
            report['status']['success'] = False
            report['status']['error'] = str(self.status)
            return report

        report['status']['success'] = True
        if self.capture_status is not None:
            report['capture_status'] = self.capture_status
        self.fill_report(report)
        return report

    def fill_report(self, report):
        def op_stats(stat_list):
            # a(st): (name, uint64) per field from the server
            out = {}
            for pair in stat_list:
                try:
                    if len(pair) != 2:
                        continue
                    k, v = pair[0], pair[1]
                except TypeError:
                    continue
                v = dbus_to_std(v)
                if str(k) in self._SIGNED_FIELDS:
                    v = self._to_signed64(v)
                out[str(k)] = v
            return out

        stats = self.result[2]
        components = {}
        total = {k: 0 for k in self._STAT_KEYS}

        for comp_name, stat_tuples in self._iter_comp_stats(stats):
            comp_data = op_stats(stat_tuples)
            components[comp_name] = comp_data
            for k in self._STAT_KEYS:
                if k in comp_data:
                    total[k] += comp_data[k]

        report['components'] = components
        report['TOTAL'] = total

    def __str__(self):
        output = ""
        if self.status != "OK":
            return ("GANESHA RESPONSE STATUS: " + self.status)
        else:
            stats = self.result[2]

            if self.capture_status is not None:
                output += (
                    "Memory statistics capture: "
                    + str(self.capture_status) + "\n\n"
                )

            # self.stats is expected to be the array (a(sa(st)))
            # So it's a list of tuples: (component_name, [(stat_name, value), ...])
            #stats_dict = {name: dict(stat_list) for name, stat_list in self.stats}
            stats_dict = {
                name: dict(stat_list)
                for name, stat_list in self._iter_comp_stats(stats)
            }

            total_l_alloc = total_l_free = 0
            total_l_ab = total_l_fb = 0
            total_cur = total_pk = 0
            k_lac = "Lifetime_Alloc_Calls"
            k_lfc = "Lifetime_Free_Calls"
            k_lab = "Lifetime_Alloc_Bytes"
            k_lfd = "Lifetime_Freed_Bytes"
            k_cur = "Current_Active_Bytes"
            k_pk = "Peak_Active_Bytes"

            _mem_labels = []
            _seen = set()
            for name in stats_dict:
                if name not in _seen:
                    _seen.add(name)
                    _mem_labels.append(name)
            w_comp = 10
            wn = 11
            w_byte = 14
            # Short tags = column headers; long names = gsh / D-Bus stat keys.
            output += (
                "Column tags (full gsh / D-Bus mem stat field names). "
                "L_ac/L_fc = call counts; all other columns = bytes.\n"
                "  L_ac=Lifetime_Alloc_Calls, L_fc=Lifetime_Free_Calls, "
                "L_ab=Lifetime_Alloc_Bytes, L_fd=Lifetime_Freed_Bytes, "
                "C_ab=Current_Active_Bytes, P_ab=Peak_Active_Bytes\n"
                "  C_ab can be negative (alloc/free mem_comp mismatch).\n"
                "All byte columns (row and TOTAL) use adaptive units - B/K/M/G, "
                "whichever keeps the value both compact and exact: values under "
                "1KiB print as exact bytes (so a small mismatch like -100 stays "
                "visible instead of rounding away to 0.00M), larger values print "
                "in the smallest unit that keeps them readable, e.g. 12.16M.\n"
            )

            def _hdr_line():
                h = f"{'Component':<{w_comp}}"
                h += f" {'L_ac':>{wn}} {'L_fc':>{wn}} {'L_ab':>{w_byte}} {'L_fd':>{w_byte}}"
                h += f" {'C_ab':>{w_byte}} {'P_ab':>{w_byte}}\n"
                return h

            def _hbytes(x):
                # Adaptive units: exact bytes below 1KiB (so small mismatches
                # like -100 stay visible), otherwise the smallest of K/M/G
                # that keeps the number readable. Sign is preserved throughout.
                if x is None:
                    return "n/a"
                ax = abs(x)
                for unit, div in (("G", 1024 ** 3), ("M", 1024 ** 2),
                                  ("K", 1024)):
                    if ax >= div:
                        return f"{x / div:.2f}{unit}"
                return f"{x}B"

            def _row(lbl, v):
                s = f"{(lbl + ':'):<{w_comp}}"
                s += f" {v[0]:>{wn}} {v[1]:>{wn}} {_hbytes(v[2]):>{w_byte}} {_hbytes(v[3]):>{w_byte}}"
                s += f" {_hbytes(v[4]):>{w_byte}} {_hbytes(v[5]):>{w_byte}}\n"
                return s

            def _row_total(v):
                s = f"{'TOTAL:':<{w_comp}}"
                s += f" {v[0]:>{wn}} {v[1]:>{wn}} {_hbytes(v[2]):>{w_byte}}"
                s += f" {_hbytes(v[3]):>{w_byte}}"
                s += f" {_hbytes(v[4]):>{w_byte}} {_hbytes(v[5]):>{w_byte}}\n"
                return s

            output += _hdr_line()

            for label in _mem_labels:
                if label in stats_dict:
                    stat_map = stats_dict[label]
                    try:
                        lac = int(stat_map[k_lac])
                        lfc = int(stat_map[k_lfc])
                        lab = int(stat_map[k_lab])
                        lfd = int(stat_map[k_lfd])
                        cur = self._to_signed64(stat_map[k_cur])
                        pk = self._to_signed64(stat_map[k_pk])
                        output += _row(
                            label,
                            [lac, lfc, lab, lfd, cur, pk],
                        )
                        total_l_alloc += lac
                        total_l_free += lfc
                        total_l_ab += lab
                        total_l_fb += lfd
                        total_cur += cur
                        total_pk += pk
                    except KeyError as e:
                        output += f"{(label + ':'):<{w_comp}} <Missing Stat: {e}>\n"
                else:
                    output += f"{(label + ':'):<{w_comp}} <Invalid Data>\n"

            outlen = w_comp + 2 * wn + 4 * w_byte + 12
            output += "-" * min(200, outlen) + "\n"
            output += _row_total(
                [
                    total_l_alloc,
                    total_l_free,
                    total_l_ab,
                    total_l_fb,
                    total_cur,
                    total_pk,
                ]
            )

        return output
