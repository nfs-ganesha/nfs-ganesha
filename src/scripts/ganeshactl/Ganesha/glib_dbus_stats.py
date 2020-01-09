#!/usr/bin/python3

# You must initialize the gobject/dbus support for threading
# before doing anything.
from __future__ import print_function
try:
    import gobject
except ImportError:
    from gi.repository import GObject as gobject
import sys
import time

gobject.threads_init()

from dbus import glib
glib.init_threads()

# Create a session bus.
import dbus

class RetrieveExportStats():
    def __init__(self):
        self.dbus_service_name = "org.ganesha.nfsd"
        self.dbus_exportstats_name = "org.ganesha.nfsd.exportstats"
        self.dbus_exportmgr_name = "org.ganesha.nfsd.exportmgr"
        self.export_interface = "/org/ganesha/nfsd/ExportMgr"

        self.bus = dbus.SystemBus()
        try:
            self.exportmgrobj = self.bus.get_object(self.dbus_service_name,
                                                    self.export_interface)
        except:
            print("Error: Can't talk to ganesha service on d-bus. Looks like Ganesha is down")
            sys.exit()

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
    # cache inode stats
    def inode_stats(self):
        stats_op = self.exportmgrobj.get_dbus_method("ShowCacheInode",
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
        try:
            self.clientmgrobj = self.bus.get_object(self.dbus_service_name,
                                                    self.client_interface)
        except:
            print("Error: Can't talk to ganesha service on d-bus. Looks like Ganesha is down")
            sys.exit()

    # delegation stats related to a single client ip
    def deleg_stats(self, ip_):
        stats_op = self.clientmgrobj.get_dbus_method("GetDelegations",
                                                     self.dbus_clientstats_name)
        return DelegStats(stats_op(ip_))
    def list_clients(self):
        stats_op = self.clientmgrobj.get_dbus_method("ShowClients",
                                                     self.dbus_clientmgr_name)
        return Clients(stats_op())
    # Clients specific stats
    def client_io_ops_stats(self, ip):
        stats_op = self.clientmgrobj.get_dbus_method("GetClientIOops",
                          self.dbus_clientstats_name)
        return ClientIOops(stats_op(ip))
    def client_all_ops_stats(self, ip):
        stats_op = self.clientmgrobj.get_dbus_method("GetClientAllops",
                          self.dbus_clientstats_name)
        return ClientAllops(stats_op(ip))


class Clients():
    def __init__(self, clients):
        self._clients = clients
    def __str__(self):
        output = ("\nTimestamp: " + time.ctime(self._clients[0][0]) +
                  str(self._clients[0][1]) + " nsecs" +
                  "\nClient List:\n")
        for client in self._clients[1]:
            output += ("\nAddress: " + client[0] +
                       "\n\tNFSv3 stats available: " + str(client[1]) +
                       "\n\tMNT stats available: " + str(client[2]) +
                       "\n\tNLM4 stats available: " + str(client[3]) +
                       "\n\tRQUOTA stats available: " + str(client[4]) +
                       "\n\tNFSv4.0 stats available " + str(client[5]) +
                       "\n\tNFSv4.1 stats available: " + str(client[6]) +
                       "\n\tNFSv4.2 stats available: " + str(client[7]) +
                       "\n\t9P stats available: " + str(client[8]))
        return output

class DelegStats():
    def __init__(self, stats):
        self.curtime = time.time()
        self.status = stats[1]
        if stats[1] == "OK":
            self.timestamp = (stats[2][0], stats[2][1])
            self.curr_deleg = stats[3][0]
            self.curr_recall = stats[3][1]
            self.fail_recall = stats[3][2]
            self.num_revokes = stats[3][3]
    def __str__(self):
        if self.status != "OK":
            return "GANESHA RESPONSE STATUS: " + self.status
        self.starttime = self.timestamp[0] + self.timestamp[1] / 1e9
        self.duration = self.curtime - self.starttime
        return ("GANESHA RESPONSE STATUS: " + self.status +
                "\nStats collected since: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs \n" +
                "\nDuration: " + "%.10f" % self.duration + " seconds" +
                "\nCurrent Delegations: " + str(self.curr_deleg) +
                "\nCurrent Recalls: " + str(self.curr_recall) +
                "\nCurrent Failed Recalls: " + str(self.fail_recall) +
                "\nCurrent Number of Revokes: " + str(self.num_revokes))

class ClientIOops():
    def __init__(self, stats):
        self.stats = stats
        self.status = stats[1]
        if stats[1] == "OK":
            self.timestamp = (stats[2][0], stats[2][1])
    def __str__(self):
        output = ""
        cnt = 3
        if self.status != "OK":
            return ("GANESHA RESPONSE STATUS: " + self.status)
        else:
            output += "\nClient last active at: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs"
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
                    cnt += 1
                j += 1
            return output

class ClientAllops():
    def __init__(self, stats):
        self.stats = stats
        self.status = stats[1]
        if stats[1] == "OK":
            self.timestamp = (stats[2][0], stats[2][1])
    def __str__(self):
        output = ""
        cnt = 3
        if self.status != "OK":
            return ("GANESHA RESPONSE STATUS: " + self.status)
        else:
            output += "\nClient last active at: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs"
            if self.stats[cnt]:
                output += "\n\t\tNFSv3 Operations"
                output += "\nOp Name    \t\t total \t errors     dups"
                tot_len = len(self.stats[cnt+1])
                i=0
                while (i+4) <= tot_len:
                    output += "\n" + (self.stats[cnt+1][i+0]).ljust(21)
                    output += " %s" % (str(self.stats[cnt+1][i+1]).rjust(9))
                    output += " %s" % (str(self.stats[cnt+1][i+2]).rjust(9))
                    output += " %s" % (str(self.stats[cnt+1][i+3]).rjust(9))
                    i += 4
                cnt += 2
            else:
                output += "\n\tNo NFSv3 activity"
                cnt += 1
            if self.stats[cnt]:
                output += "\n\t\tNLMv4 Operations"
                output += "\nOp Name    \t\t total \t errors     dups"
                tot_len = len(self.stats[cnt+1])
                i=0
                while (i+4) <= tot_len:
                    output += "\n" + (self.stats[cnt+1][i+0]).ljust(21)
                    output += " %s" % (str(self.stats[cnt+1][i+1]).rjust(9))
                    output += " %s" % (str(self.stats[cnt+1][i+2]).rjust(9))
                    output += " %s" % (str(self.stats[cnt+1][i+3]).rjust(9))
                    i += 4
                cnt += 2
            else:
                output += "\n\tNo NLMv4 activity"
                cnt += 1
            if self.stats[cnt]:
                output += "\n\t\tNFSv4 Operations"
                output += "\nOp Name    \t\t total \t errors"
                tot_len = len(self.stats[cnt+1])
                i=0
                while (i+3) <= tot_len:
                    output += "\n" + (self.stats[cnt+1][i+0]).ljust(21)
                    output += " %s" % (str(self.stats[cnt+1][i+1]).rjust(9))
                    output += " %s" % (str(self.stats[cnt+1][i+2]).rjust(9))
                    i += 3
                cnt += 2
            else:
                output += "\n\tNo NFSv4 activity"
                cnt += 1
            if self.stats[cnt]:
                output += "\n\t\tNFSv4 Compound Operations"
                output += "\n      total \t errors \t Ops in compound\n"
                output += " %s" % (str(self.stats[cnt+1][0]).rjust(9))
                output += " %s" % (str(self.stats[cnt+1][1]).rjust(9))
                output += " %s" % (str(self.stats[cnt+1][2]).rjust(9))
                cnt += 2
            else:
                output += "\n\tNo NFSv4 compound ops"
                cnt += 1
            return output

class Export():
    def __init__(self, export):
        self.exportid = export[0]
        self.path = export[1]
        self.nfsv3_stats_avail = export[2]
        self.nfsv40_stats_avail = export[6]
        self.nfsv41_stats_avail = export[7]
        self.nfsv42_stats_avail = export[8]
        self.mnt_stats_avail = export[3]
        self.nlmv4_stats_avail = export[4]
        self.rquota_stats_avail = export[5]
        self._9p_stats_avail = export[9]
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
                "\n\t9p stats available: " + str(self._9p_stats_avail) + "\n")

class ExportStats():
    def __init__(self, exports):
        self.curtime = time.time()
        self.timestamp = (exports[0][0], exports[0][1])
        self.exports = {}
        for export in exports[1]:
            exportid = export[0]
            self.exports[exportid] = Export(export)
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

class ExportDetails():
    def __init__(self, stats):
        self.stats = stats
        self.status = stats[1]
        if stats[1] == "OK":
            self.timestamp = (stats[2][0], stats[2][1])
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

class GlobalStats():
    def __init__(self, stats):
        self.curtime = time.time()
        self.success = stats[0]
        self.status = stats[1]
        if self.success:
            self.timestamp = (stats[2][0], stats[2][1])
            self.nfsv3_total = stats[3][1]
            self.nfsv40_total = stats[3][3]
            self.nfsv41_total = stats[3][5]
            self.nfsv42_total = stats[3][7]
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

class InodeStats():
    def __init__(self, stats):
        self.stats = stats
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


class FastStats():
    def __init__(self, stats):
        self.curtime = time.time()
        self.stats = stats
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
            for i in range(0, len(self.stats[3])-1):
                if ":" in str(self.stats[3][i]):
                    output += self.stats[3][i] + "\n"
                elif str(self.stats[3][i]).isdigit():
                    output += "\t%s" % (str(self.stats[3][i]).rjust(8)) + "\n"
                else:
                    output += "%s: " % (self.stats[3][i].ljust(20))
        return output

class ExportIOv3Stats():
    def __init__(self, stats):
        self.stats = stats
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

class ExportIOv4Stats():
    def __init__(self, stats):
        self.stats = stats
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

class TotalStats():
    def __init__(self, stats):
        self.curtime = time.time()
        self.stats = stats
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

class PNFSStats():
    def __init__(self, stats):
        self.curtime = time.time()
        self.stats = stats
    def __str__(self):
        output = "PNFS stats for export(s)"
        for key in self.stats:
            if self.stats[key][1] != "OK":
                return "No NFS activity, GANESHA RESPONSE STATUS: \n" + self.stats[key][1]
            self.starttime = self.stats[key][2][0] + self.stats[key][2][1] / 1e9
            self.duration = self.curtime - self.starttime
            output += ("\nStats collected since: " + time.ctime(self.stats[key][2][0]) + str(self.stats[key][2][1]) + " nsecs" +
                       "\nDuration: " + "%.10f" % self.duration + " seconds\n")
            output += "\nStatistics for:" + str(exports[1]) +" export id: "+ str(key)
            output += "\n\t\ttotal\terrors\tdelays" + "getdevinfo "
            for stat in self.stats[key][3]:
                output += "\t" + stat
            output += "layout_get "
            for stat in self.stats[key][4]:
                output += "\t" + stat
            output += "layout_commit "
            for stat in self.stats[key][5]:
                output += "\t" + stat
            output += "layout_return "
            for stat in self.stats[key][6]:
                output += "\t" + stat
            output += "recall "
            for stat in self.stats[key][7]:
                output += "\t" + stat
        return output

class StatsReset():
    def __init__(self, status):
        self.status = status
    def __str__(self):
        if self.status[1] != "OK":
            return "Failed to reset statistics, GANESHA RESPONSE STATUS: " + self.status[1]
        else:
            return "Successfully resetted statistics counters"

class StatsStatus():
    def __init__(self, status):
        self.status = status
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


class DumpFSALStats():
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

class DumpAuth():
    def __init__(self, stats):
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
                   "\n\tMin Latency: " + str(self.wbmin))
        return output


class DumpFULLV3Stats():
    def __init__(self, status):
        self.curtime = time.time()
        self.stats = status
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

class DumpFULLV4Stats():
    def __init__(self, status):
        self.curtime = time.time()
        self.stats = status
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
