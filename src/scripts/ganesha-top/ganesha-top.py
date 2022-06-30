#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Copyright (C) SUSE, 2022
# Author: Vicente Cheng <vicente.cheng@suse.com>
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
import os
import sys
import curses
import getopt
import argparse
import subprocess

import psutil
import dbus
from datetime import datetime
from Ganesha.ganesha_mgr_utils import AdminInterface
from Ganesha.glib_dbus_stats import Export, Client

GaneshaProcess = 'ganesha.nfsd'
GaneshaService = 'org.ganesha.nfsd'
DefaultInterval = 5  # seconds

DEFAULT_CONTENT_POS_Y = 7  # default y position


def enbale_all_stats():
    try:
        subprocess.run(["ganesha_stats.py", "enable", "all"],
                       stdout=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        sys.exit("Enable ALL stats Error {}".format(e))


def get_ganesha_pid():
    try:
        return int(subprocess.run(["pidof", "-s", GaneshaProcess],
                                  stdout=subprocess.PIPE).stdout)
    except subprocess.CalledProcessError as e:
        sys.exit("NFS-Ganesha is not running, please check it again.\n {}"
                 .format(e))


def setup_color():
    curses.start_color()
    curses.init_pair(1, curses.COLOR_CYAN, curses.COLOR_BLACK)
    curses.init_pair(2, curses.COLOR_BLUE, curses.COLOR_BLACK)
    curses.init_pair(3, curses.COLOR_YELLOW, curses.COLOR_BLACK)
    curses.init_pair(4, curses.COLOR_GREEN, curses.COLOR_BLACK)
    curses.init_pair(5, curses.COLOR_WHITE, curses.COLOR_BLACK)
    curses.init_pair(6, curses.COLOR_BLACK, curses.COLOR_MAGENTA)
    curses.init_pair(7, curses.COLOR_BLACK, curses.COLOR_GREEN)
    curses.init_pair(8, curses.COLOR_MAGENTA, curses.COLOR_BLACK)
    curses.init_pair(9, curses.COLOR_GREEN, curses.COLOR_BLACK)
    curses.init_pair(10, curses.COLOR_RED, curses.COLOR_BLACK)
    curses.init_pair(11, curses.COLOR_BLACK, curses.COLOR_WHITE)


class SingleTon:
    _instance = None

    def __new__(cls, *args, **kwargs):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance


class ClientInterface(SingleTon):
    def __init__(self):
        self.dbus_clientstats_name = "org.ganesha.nfsd.clientstats"
        self.dbus_clientmgr_name = "org.ganesha.nfsd.clientmgr"
        self.client_interface = "/org/ganesha/nfsd/ClientMgr"

        self.dbus = dbus.SystemBus()
        self.mgr = self.dbus.get_object(GaneshaService,
                                        self.client_interface)

    def show_clients(self):
        dbus_op = self.mgr.get_dbus_method("ShowClients",
                                           self.dbus_clientmgr_name)
        return dbus_op

    def get_client_io_ops(self):
        dbus_op = self.mgr.get_dbus_method("GetClientIOops",
                                           self.dbus_clientstats_name)
        return dbus_op


class ClientMgr(SingleTon):
    def __init__(self):
        self.interface = ClientInterface()
        self.clients = dict()
        self.client_io = dict()
        self.preload_client_info()

    @property
    def NumOfClients(self):
        return len(self.clients.keys())

    def preload_client_info(self):
        stats_op = self.interface.show_clients()
        self.parse_client_stats(stats_op())

    def parse_client_stats(self, stats):
        for client in stats[1]:
            clientaddr = client[0]
            self.clients[clientaddr] = Client(client)

    def get_total_client_ops(self):
        stats_op = self.interface.get_client_io_ops()
        for client_addr in self.clients.keys():
            stats = stats_op(client_addr)
            if stats[1] != "OK":
                continue
            io_content = stats[3:]
            nfsver_cnt = 0
            content = dict({'Read': 0, 'Write': 0, 'Others': 0})
            # each protocol has 4 colume when is enable
            # io_content start from NFSv3, skip it.
            while nfsver_cnt < 4:
                if io_content[0]:
                    if nfsver_cnt == 0:  # mean v3
                        io_content = io_content[4:]
                        continue
                    else:
                        content['Read'] = io_content[1][0]
                        content['Write'] = io_content[2][0]
                        content['Others'] = io_content[3][0]
                    io_content = io_content[5:]
                else:
                    io_content = io_content[1:]
                nfsver_cnt += 1
            self.client_io[client_addr] = content
        return self.client_io


class ExportInterface(SingleTon):
    def __init__(self):
        self.dbus_exportstats_name = "org.ganesha.nfsd.exportstats"
        self.dbus_exportmgr_name = "org.ganesha.nfsd.exportmgr"
        self.export_interface = "/org/ganesha/nfsd/ExportMgr"

        self.dbus = dbus.SystemBus()
        self.mgr = self.dbus.get_object(GaneshaService,
                                        self.export_interface)

    def show_exports(self):
        dbus_op = self.mgr.get_dbus_method("ShowExports",
                                           self.dbus_exportmgr_name)
        return dbus_op

    def get_global_ops(self):
        dbus_op = self.mgr.get_dbus_method("GetGlobalOPS",
                                           self.dbus_exportstats_name)
        return dbus_op

    def show_cache_inode(self):
        dbus_op = self.mgr.get_dbus_method("ShowCacheInode",
                                           self.dbus_exportstats_name)
        return dbus_op

    def get_full_v4_stats(self):
        dbus_op = self.mgr.get_dbus_method("GetFULLV4Stats",
                                           self.dbus_exportstats_name)
        return dbus_op

    def get_total_ops(self):
        dbus_op = self.mgr.get_dbus_method("GetTotalOPS",
                                           self.dbus_exportstats_name)
        return dbus_op


class ExportMgr(SingleTon):
    def __init__(self):
        self.interface = ExportInterface()
        self.exports = dict()
        self.export_stats = dict()
        self.global_ops = dict()
        self.cache_lru_info = dict()
        self.v4_detail_stats = dict()
        self.preload_export_info()
        self.preload_global_ops()
        self.preload_cache_lru()

    @property
    def NumOfExports(self):
        return len(self.exports.keys())

    @property
    def GlobalV3OPS(self):
        return self.global_ops['v3']

    @property
    def GlobalV40OPS(self):
        return self.global_ops['v4.0']

    @property
    def GlobalV41OPS(self):
        return self.global_ops['v4.1']

    @property
    def GlobalV42OPS(self):
        return self.global_ops['v4.2']

    @property
    def FDUsage(self):
        return self.cache_lru_info['FDUsage']

    @property
    def NumOfChunk(self):
        return self.cache_lru_info['chunks']

    @property
    def NumOfOpenedFD(self):
        return self.cache_lru_info['openedFD']

    @property
    def NumOfLRUEntry(self):
        return self.cache_lru_info['entries']

    def preload_cache_lru(self):
        stats_op = self.interface.show_cache_inode()
        self.parse_cache_stats(stats_op())

    def preload_export_info(self):
        stats_op = self.interface.show_exports()
        self.parse_export_stats(stats_op())

    def preload_global_ops(self):
        stats_op = self.interface.get_global_ops()
        self.parse_global_ops(stats_op())

    def parse_cache_stats(self, stats):
        self.cache_lru_info['FDUsage'] = stats[4][5]
        self.cache_lru_info['openedFD'] = stats[4][1]
        self.cache_lru_info['entries'] = stats[4][7]
        self.cache_lru_info['chunks'] = stats[4][9]

    def parse_export_stats(self, stats):
        for export in stats[1]:
            if export[0] == 0:  # pseudo root should not be count
                continue
            exportid = export[0]
            self.exports[exportid] = Export(export)

    def parse_global_ops(self, stats):
        self.global_ops['v3'] = -1
        self.global_ops['v4.0'] = -1
        self.global_ops['v4.1'] = -1
        self.global_ops['v4.2'] = -1
        if stats[0]:
            self.global_ops['v3'] = stats[3][1]
            self.global_ops['v4.0'] = stats[3][3]
            self.global_ops['v4.1'] = stats[3][5]
            self.global_ops['v4.2'] = stats[3][7]

    def show_v4_full_stats(self):
        stats_op = self.interface.get_full_v4_stats()
        stats = stats_op()
        self.v4_detail_stats['Status'] = ""
        self.v4_detail_stats['Details'] = dict()
        if not stats[0]:
            self.v4_detail_stats['Status'] = "Unable to fetch NFSv4 stats."
        else:
            if stats[4] != "OK":
                self.v4_detail_stats['Status'] = \
                    "No stats available for display"
            else:
                self.v4_detail_stats['Status'] = "OK"
                cnt = 0
                while cnt < len(stats[3]):
                    content = dict()
                    content['TotalOPs'] = stats[3][cnt][1]
                    content['Error'] = stats[3][cnt][2]
                    content['Max'] = "{:12.6f}".format(stats[3][cnt][5])
                    content['Min'] = "{:12.6f}".format(stats[3][cnt][4])
                    content['Avg'] = "{:12.6f}".format(stats[3][cnt][3])
                    self.v4_detail_stats['Details'][str(stats[3][cnt][0])] = \
                        content
                    cnt += 1
        return self.v4_detail_stats

    def get_total_export_stats(self):
        stats_op = self.interface.get_total_ops()
        for export_id in self.exports.keys():
            if export_id == 0:
                continue
            self.export_stats[export_id] = stats_op(export_id)
        return self.export_stats


class GaneshaPSInfo(SingleTon):
    def __init__(self):
        pid = get_ganesha_pid()
        self.ps = psutil.Process(pid)
        self.memory_info = self.ps.memory_full_info()

    @property
    def status(self):
        return "Running" if self.ps.is_running() else "Not Running"

    @property
    def rsize(self):
        return self.memory_info.rss/1024

    @property
    def vsize(self):
        return self.memory_info.vms/1024

    @property
    def swapsize(self):
        return self.memory_info.swap/1024

    @property
    def cpu_usage(self):
        return self.ps.cpu_percent()


# Default is KB
def convert_memory_size(size):
    if size/1024 > 1024:  # convert to GB
        return "{:5.2f} G".format(size/1024/1024)
    if size/1024 > 100:  # convert to MB
        return "{:5.2f} M".format(size/1024)
    else:
        return "{} K".format(int(size))


def generate_version():
    dbus_admin_name = "org.ganesha.nfsd.admin"
    admin_interface = "/org/ganesha/nfsd/admin"
    admin = AdminInterface(GaneshaService,
                           admin_interface,
                           dbus_admin_name)
    s, _, versions = admin.GetAll()
    if s:
        version = versions['VERSION_RELEASE']
        git_hash = versions['VERSION_GIT_HEAD']
        vers = "V{}({})".format(version, git_hash)
        return vers
    else:
        return "Unknown"


def draw_header(stdscr, height, width):
    # Get header content
    ps_info = GaneshaPSInfo()
    exportmgr = ExportMgr()
    clientmgr = ClientMgr()

    version = generate_version()

    status = ps_info.status
    rsize = convert_memory_size(ps_info.rsize)
    vsize = convert_memory_size(ps_info.vsize)
    swapsize = convert_memory_size(ps_info.swapsize)
    cpu_usage = ps_info.cpu_usage

    num_of_exports = exportmgr.NumOfExports
    num_of_clients = clientmgr.NumOfClients
    global_v40_ops = exportmgr.GlobalV40OPS
    global_v41_ops = exportmgr.GlobalV41OPS
    global_v42_ops = exportmgr.GlobalV42OPS

    fd_usage = exportmgr.FDUsage
    num_of_opened_fds = exportmgr.NumOfOpenedFD
    num_of_lru_entry = exportmgr.NumOfLRUEntry
    num_of_chunk = exportmgr.NumOfChunk

    stdscr.attron(curses.color_pair(1))
    stdscr.addstr(0, 0, "Version: {}".format(version))
    stdscr.addstr(1, 0, "Status: {}".format(status))
    stdscr.addstr(1, 20, "| RSIZE: {}".format(rsize))
    stdscr.addstr(1, 40, "| VSIZE: {}".format(vsize))
    stdscr.addstr(1, 60, "| SWAP: {}".format(swapsize))
    stdscr.addstr(1, 80, "| CPU: {}%".format(cpu_usage))
    stdscr.addstr(2, 0, "Exports: {}".format(num_of_exports))
    stdscr.addstr(2, 20, "| Clients: {}".format(num_of_clients))
    stdscr.addstr(2, 40, "| V4.0 OPs: {}".format(global_v40_ops))
    stdscr.addstr(2, 60, "| V4.1 OPs: {}".format(global_v41_ops))
    stdscr.addstr(2, 80, "| V4.2 OPs: {}".format(global_v42_ops))
    stdscr.addstr(3, 0, "FD Status: {}".format(fd_usage))
    stdscr.addstr(3, 40, "| Opened FD: {}".format(num_of_opened_fds))
    stdscr.addstr(3, 60, "| LRU Entries: {}".format(num_of_lru_entry))
    stdscr.addstr(3, 80, "| Dir Chunks: {}".format(num_of_chunk))
    stdscr.attroff(curses.color_pair(1))

    # generate divider
    divider = ("{}".format(" ")*width)
    stdscr.attron(curses.color_pair(11))
    stdscr.addstr(5, 0, divider)
    stdscr.attroff(curses.color_pair(11))


def draw_footbar(stdscr, height, width, key):
    # content
    current_time = datetime.now()
    time_str = current_time.strftime("%Y-%m-%d %H:%M:%S")
    footbar_str = ("Press 'q' to exit | Press 'h' for help | {} | Last key: {}"
                   .format(time_str, key))

    # Rendering footbar
    stdscr.attron(curses.color_pair(11))
    stdscr.addstr(height-1, 0, footbar_str)
    stdscr.addstr(height-1, len(footbar_str),
                  " " * (width - len(footbar_str) - 1))
    stdscr.attroff(curses.color_pair(11))


def draw_client_page(stdscr, screen_opt):
    stdscr.clear()
    height, width = stdscr.getmaxyx()
    draw_header(stdscr, height, width)
    draw_client_stats(stdscr)
    draw_footbar(stdscr, height, width, screen_opt)


def draw_client_stats(stdscr):
    mgr = ClientMgr()
    client_stats = mgr.get_total_client_ops()
    pos_y = DEFAULT_CONTENT_POS_Y
    title = " ----- Client IO OPs (Only counted with NFSv4) ----- "
    stdscr.attron(curses.color_pair(5))
    stdscr.attron(curses.A_BOLD)
    stdscr.addstr(pos_y, 0, title)
    stdscr.attroff(curses.color_pair(5))
    stdscr.attroff(curses.A_BOLD)
    pos_y += 1
    stdscr.attron(curses.color_pair(9))
    stdscr.attron(curses.A_BOLD)
    for client_addr, stat in client_stats.items():
        content = ""
        content = "Client Addr: {:>23}".format(client_addr)
        content += " | Read OPs: {:>12}".format(stat['Read'])
        content += " | Write OPs: {:>12}".format(stat['Write'])
        content += " | Others OPs: {:>12}".format(stat['Others'])
        stdscr.addstr(pos_y, 0, content)
        pos_y += 1
    stdscr.attroff(curses.color_pair(9))
    stdscr.attroff(curses.A_BOLD)


def draw_export_page(stdscr, screen_opt):
    stdscr.clear()
    height, width = stdscr.getmaxyx()
    draw_header(stdscr, height, width)
    draw_export_stats(stdscr, width)
    draw_footbar(stdscr, height, width, screen_opt)


def draw_export_stats(stdscr, width):
    mgr = ExportMgr()
    export_stats = mgr.get_total_export_stats()
    pos_y = DEFAULT_CONTENT_POS_Y
    title = " ----- Export OPs (Only counted with NFSv4) ----- "
    stdscr.attron(curses.color_pair(5))
    stdscr.attron(curses.A_BOLD)
    stdscr.addstr(pos_y, 0, title)
    stdscr.attroff(curses.color_pair(5))
    stdscr.attroff(curses.A_BOLD)
    pos_y += 1
    stdscr.attron(curses.color_pair(9))
    stdscr.attron(curses.A_BOLD)
    for exportid, stat in export_stats.items():
        content = ""
        content = "EXPORTID: {:>10} ".format(exportid)
        content += "| NFSv4.0 OP: {:>10} ".format(stat[3][3])
        content += "| NFSv4.1 OP: {:>10} ".format(stat[3][5])
        content += "| NFSv4.2 OP: {:>10} ".format(stat[3][7])
        stdscr.addstr(pos_y, 0, content)
        pos_y += 1
    stdscr.attroff(curses.color_pair(9))
    stdscr.attroff(curses.A_BOLD)


def draw_help_page(stdscr, screen_opt):
    stdscr.clear()
    height, width = stdscr.getmaxyx()
    draw_header(stdscr, height, width)
    draw_help_content(stdscr)
    draw_footbar(stdscr, height, width, screen_opt)


def draw_help_content(stdscr):
    content = ""
    content += "The following control keys are shown:\n"
    content += "    'c' - show all client simple info\n"
    content += "    'd' - show default page info (v4 full stats)\n"
    content += "    'e' - show all export simple info\n"
    content += "    'q' - exit\n"

    stdscr.attron(curses.color_pair(9))
    stdscr.attron(curses.A_BOLD)
    stdscr.addstr(DEFAULT_CONTENT_POS_Y, 0, content)
    stdscr.attroff(curses.color_pair(9))
    stdscr.attroff(curses.A_BOLD)


def draw_v4_full_stats(stdscr):
    mgr = ExportMgr()
    v4_full_result = mgr.show_v4_full_stats()
    pos_y = DEFAULT_CONTENT_POS_Y
    result = ""
    if v4_full_result['Status'] != "OK":
        result += v4_full_result['Status']
    else:
        title = " ----- NFSv4 Detailed statistics: ----- \n"
        stdscr.attron(curses.color_pair(5))
        stdscr.attron(curses.A_BOLD)
        stdscr.addstr(pos_y, 0, title)
        stdscr.attroff(curses.color_pair(5))
        stdscr.attroff(curses.A_BOLD)
        pos_y += 1
        result += ("{:25} | {:^15} | {:^8} | {:^12} | {:^12} | {:^12}"
                   .format("OP NAME", "Total OPs", "Errors",
                           "Average", "Minimum", "Maxmum"))
        for op_name, content in v4_full_result['Details'].items():
            result += "\n"
            result += "{:25} | ".format(op_name)
            result += "{:15} | ".format(content["TotalOPs"])
            result += "{:8} | ".format(content["Error"])
            result += "{:12} | ".format(content["Avg"])
            result += "{:12} | ".format(content["Min"])
            result += "{:12} | ".format(content["Max"])

    stdscr.attron(curses.color_pair(9))
    stdscr.attron(curses.A_BOLD)
    stdscr.addstr(pos_y, 0, result)
    stdscr.attroff(curses.color_pair(9))
    stdscr.attroff(curses.A_BOLD)


def draw_default_page(stdscr, screen_opt):
    stdscr.clear()
    height, width = stdscr.getmaxyx()

    draw_header(stdscr, height, width)
    draw_v4_full_stats(stdscr)
    draw_footbar(stdscr, height, width, screen_opt)


def draw_menu(stdscr, interval):

    def opt_not_valid(opt):
        return opt != ord('q') and opt != ord('h') and opt != ord('e') and \
               opt != ord('c') and opt != ord('d')

    screen_opt = 0
    stdscr.clear()
    stdscr.refresh()

    # Start colors in curses
    setup_color()

    while (1):

        if screen_opt == ord('q'):
            break
        elif screen_opt == ord('h'):
            draw_help_page(stdscr, screen_opt)
        elif screen_opt == ord('c'):
            draw_client_page(stdscr, screen_opt)
        elif screen_opt == ord('d'):
            draw_default_page(stdscr, screen_opt)
        elif screen_opt == ord('e'):
            draw_export_page(stdscr, screen_opt)
        else:
            draw_default_page(stdscr, screen_opt)

        screen_opt_prev = screen_opt

        stdscr.refresh()
        stdscr.nodelay(1)
        stdscr.timeout(interval)

        # Wait for next input
        screen_opt = stdscr.getch()
        if screen_opt == -1 or opt_not_valid(screen_opt):
            screen_opt = screen_opt_prev


def main(argv):
    interval = DefaultInterval
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--interval", type=int, default=DefaultInterval,
                        help="refresh interval in seconds (Default: 5)")
    args = parser.parse_args()

    interval = args.interval * 1000 # convert to seconds

    pid = get_ganesha_pid()
    enbale_all_stats()
    curses.wrapper(draw_menu, interval)


if __name__ == "__main__":
    main(sys.argv[1:])
