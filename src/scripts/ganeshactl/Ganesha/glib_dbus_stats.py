#!/usr/bin/python2

# You must initialize the gobject/dbus support for threading
# before doing anything.
from __future__ import print_function
import gobject
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
            stats_dict[export_id] = stats_op(int(export_id))
            return stats_dict
    def v3io_stats(self, export_id):
        stats_op =  self.exportmgrobj.get_dbus_method("GetNFSv3IO",
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
    def deleg_stats(self, ip):
        stats_op = self.clientmgrobj.get_dbus_method("GetDelegations",
                          self.dbus_clientstats_name)
        return DelegStats(stats_op(ip))
    def list_clients(self):
        stats_op = self.clientmgrobj.get_dbus_method("ShowClients",
                          self.dbus_clientmgr_name)
        return Clients(stats_op())

class Clients():
    def __init__(self, clients):
        self._clients = clients
    def __str__(self):
        output = ("\nTimestamp: " + time.ctime(self._clients[0][0]) +
                  str(self._clients[0][1]) + " nsecs" +
                  "\nClient List:\n" )
        for client in self._clients[1]:
            output += ("\n\nAddress: " + client[0] +
                       "\nNFSv3 stats available: " + str(client[1]) +
                       "\nMNT stats available: " + str(client[2]) +
                       "\nNLM4 stats available: " + str(client[3]) +
                       "\nRQUOTA stats available: " + str(client[4]) +
                       "\nNFSv4.0 stats available " + str(client[5]) +
                       "\nNFSv4.1 stats available: " + str(client[6]) +
                       "\nNFSv4.2 stats available: " + str(client[7]) +
                       "\n9P stats available: " + str(client[8]) )
        return output
class DelegStats():
    def __init__(self, stats):
        self.status = stats[1]
        if stats[1] == "OK":
            self.timestamp = (stats[2][0], stats[2][1])
            self.curr_deleg = stats[3][0]
            self.curr_recall = stats[3][1]
            self.fail_recall = stats[3][2]
            self.num_revokes = stats[3][3]
    def __str__(self):
        if self.status != "OK":
            return ("GANESHA RESPONSE STATUS: " + self.status)
        else:
            return ( "GANESHA RESPONSE STATUS: " + self.status +
                     "\nTimestamp: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs" +
                     "\nCurrent Delegations: " + str(self.curr_deleg) +
                     "\nCurrent Recalls: " + str(self.curr_recall) +
                     "\nCurrent Failed Recalls: " + str(self.fail_recall) +
                     "\nCurrent Number of Revokes: " + str(self.num_revokes) )

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
                "\nPath: " + self.path +
                "\nNFSv3 stats available: " + str(self.nfsv3_stats_avail) +
                "\nNFSv4.0 stats available: " + str(self.nfsv40_stats_avail) +
                "\nNFSv4.1 stats available: " + str(self.nfsv41_stats_avail) +
                "\nNFSv4.2 stats available: " + str(self.nfsv42_stats_avail) +
                "\nMNT stats available: " + str(self.mnt_stats_avail) +
                "\nNLMv4 stats available: " + str(self.nlmv4_stats_avail) +
                "\nRQUOTA stats available: " + str(self.rquota_stats_avail) +
                "\n9p stats available: " + str(self._9p_stats_avail) + "\n")
class ExportStats():
    def __init__(self, exports):
        self.timestamp = (exports[0][0], exports[0][1])
        self.exports = {}
        for export in exports[1]:
            exportid = export[0]
            self.exports[exportid] = Export(export)
    def __str__(self):
        output = ( "Timestamp: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs")
        for exportid in self.exports:
            output += str(self.exports[exportid])
        return output
    def exportids(self):
        return self.exports.keys()

class GlobalStats():
    def __init__(self, stats):
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
        output += ("Timestamp: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs" +
                "\nTotal NFSv3 ops: " + str(self.nfsv3_total) +
                "\nTotal NFSv4.0 ops: " + str(self.nfsv40_total) +
                "\nTotal NFSv4.1 ops: " + str(self.nfsv41_total) +
                "\nTotal NFSv4.2 ops: " + str(self.nfsv42_total))
        return output

class InodeStats():
    def __init__(self, stats):
        self.status = stats[1]
        if stats[1] != "OK":
            return
        self.timestamp = (stats[2][0], stats[2][1])
        self.cache_requests = stats[3][1]
        self.cache_hits = stats[3][3]
        self.cache_miss = stats[3][5]
        self.cache_conflict = stats[3][7]
        self.cache_add = stats[3][9]
        self.cache_mapping = stats[3][11]
    def __str__(self):
        if self.status != "OK":
            return "No NFS activity, GANESHA RESPONSE STATUS: " + self.status
        return ( "Timestamp: " + time.ctime(self.timestamp[0]) + str(self.timestamp[1]) + " nsecs" +
                 "\nInode Cache Requests: " + str(self.cache_requests) +
                 "\nInode Cache Hits: " + str(self.cache_hits) +
                 "\nInode Cache Misses: " + str(self.cache_miss) +
                 "\nInode Cache Conflicts:: " + str(self.cache_conflict) +
                 "\nInode Cache Adds: " + str(self.cache_add) +
                 "\nInode Cache Mapping: " + str(self.cache_mapping) )

class FastStats():
    def __init__(self, stats):
        self.stats = stats
    def __str__(self):
        if not self.stats[0]:
            return "No NFS activity, GANESHA RESPONSE STATUS: " + self.stats[1]
        else:
            if self.stats[1] != "OK":
                output = self.stats[1]+ "\n"
            else:
                output = ""
            output += ("Timestamp: " + time.ctime(self.stats[2][0]) + str(self.stats[2][1]) + " nsecs" +
                      "\nGlobal ops:\n" )
            # NFSv3, NFSv4, NLM, MNT, QUOTA self.stats
            for i in range(0,len(self.stats[3])-1):
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
            output += ( "\nEXPORT %s:" % (key) +
                        "\n\t\trequested\ttransferred\t     total\t    errors\t   latency\tqueue wait" +
                        "\nREADv3: " )
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
                       "\n\t\trequested\ttransferred\t     total\t    errors\t   latency\tqueue wait" +
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
        self.stats = stats
    def __str__(self):
        output = ""
        for key in self.stats:
            if not self.stats[key][0]:
                return "No NFS activity, GANESHA RESPONSE STATUS: " + self.stats[key][1]
            if self.stats[key][1] != "OK":
                output += self.stats[key][1] + "\n"
            output += ("Total stats for export id: " + str(key) +
                      "\nTimestamp: " + time.ctime(self.stats[key][2][0]) +
                      str(self.stats[key][2][1]) + " nsecs\n")
            for i in range(0,len(self.stats[key][3])-1, 2):
                output += "%s: %s\n" % (self.stats[key][3][i], self.stats[key][3][i+1])
        return output

class PNFSStats():
    def __init__(self, stats):
        self.stats = stats
    def __str__(self):
        for key in self.stats:
            if self.stats[key][1] != "OK":
                return "No NFS activity, GANESHA RESPONSE STATUS: \n" + self.stats[key][1]
            output = ("Total stats for export id" + str(key) +
                      "\nTimestamp: " + time.ctime(self.stats[key][2][0]) +
                      str(self.stats[key][2][1]) + " nsecs" +
                      "\nStatistics for:" + str(exports[1]) + "\n\t\ttotal\terrors\tdelays" +
                      + "getdevinfo ")
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
		output += time.ctime(self.status[3][1][0]) + str(self.status[3][1][1]) + " nsecs"
	    else:
		 output += "Stats counting for FSAL is currently disabled"
	    return output

class DumpFSALStats():
    def __init__(self, stats):
        self.stats = stats
    def __str__(self):
	output = ""
	if not self.stats[0]:
	    return ("GANESHA RESPONSE STATUS: " + self.stats[1])
	else:
	    output += ("Timestamp: " + time.ctime(self.stats[2][0]) + str(self.stats[2][1]) + " nsecs\n")
	    if self.stats[3] == "GPFS":
		output += "FSAL Name - GPFS\n"
	    	if self.stats[5] != "OK":
		    output += "No stats available for display"
		    return output
	    	else:
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
