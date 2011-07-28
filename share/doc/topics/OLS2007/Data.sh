# source this in your /bin/bash script

# define functions to be anything that needs doing across the papers...
# BEFORE sourcing this data.

# You'll need do_prep(), called before any data;
#             do_wrapup(), called at the end;
#        and  do_work(), called for each paper.
#
# See Utils/Buildstatus.sh for an example of how to use this setup,
# or the Showtree.sh script.

do_prep


PAPER_ID=1
TITLE="The Price of Safety: Evaluating IOMMU Performance"
BIO_ID="2531"
CONTENT_KEY="56"
AUTHOR="Muli  Ben-Yehuda"
RECEIVED=1

do_work

PAPER_ID=2
TITLE="Linux on Cell Broadband Engine status update"
BIO_ID="768"
CONTENT_KEY="177"
AUTHOR="Arnd  Bergmann"
RECEIVED=1

do_work

PAPER_ID=3
TITLE="Linux Kernel Debugging on Google-sized clusters"
BIO_ID="1697"
CONTENT_KEY="241"
AUTHOR="Martin  Bligh"
RECEIVED=1

do_work

PAPER_ID=4
TITLE="Ltrace internals"
BIO_ID="2532"
CONTENT_KEY="82"
AUTHOR="Rodrigo Rubira  Branco"
RECEIVED=1

do_work

PAPER_ID=5
TITLE="Evaluating effects of cache memory compression on embedded systems using Linux kernel 2.6.x"
BIO_ID="2548"
CONTENT_KEY="100"
AUTHOR="Anderson Farias Briglia"
RECEIVED=1

do_work

PAPER_ID=6
TITLE="ACPI in Linux -- Myths vs. Reality"
BIO_ID="222"
CONTENT_KEY="24"
AUTHOR="Len  Brown_1"
RECEIVED=1

do_work

PAPER_ID=7
TITLE="Cool Hand Linux -- Thermal Extensions for Linux Handhelds"
BIO_ID="222"
CONTENT_KEY="122"
AUTHOR="Len  Brown_2"
RECEIVED=1

do_work

PAPER_ID=8
TITLE="Asynchronous system calls"
BIO_ID="1287"
CONTENT_KEY="213"
AUTHOR="Zachary A Brown_Z"
RECEIVED=1

do_work

PAPER_ID=9
TITLE="Frysk 1, Kernel 0?"
BIO_ID="2543"
CONTENT_KEY="72"
AUTHOR="Andrew  Cagney"
RECEIVED=1

# do_work
# 
# PAPER_ID=10
# TITLE="MySQL Server Performance Tuning, under the Microscope"
# BIO_ID="1372"
# CONTENT_KEY="235"
# AUTHOR="Colin  Charles"
# ECEIVED

do_work

PAPER_ID=11
TITLE="Keeping Kernel Performance from Regressions"
BIO_ID="1755"
CONTENT_KEY="168"
AUTHOR="Tim  Chen"
RECEIVED=1

do_work

# presentation-only
# PAPER_ID=12
# TITLE="The kernel report"
# BIO_ID="2"
# CONTENT_KEY="152"
# AUTHOR="Jonathan  Corbet"
# XECEIVED=0
## 
## do_work

PAPER_ID=13
TITLE="Breaking the Chains: Using LinuxBIOS to liberate embedded X86 processors"
BIO_ID="877"
CONTENT_KEY="45"
AUTHOR="Jordan H Crouse"
RECEIVED=1

do_work

PAPER_ID=14
TITLE="The 7 dwarves: debugging information beyond gdb"
BIO_ID="2584"
CONTENT_KEY="173"
AUTHOR="Arnaldo Carvalho de Melo"
RECEIVED=1

do_work

PAPER_ID=15
TITLE="GANESHA, a multi-usage with large cache NFSv4 server. "
BIO_ID="2536"
CONTENT_KEY="62"
AUTHOR="Philippe  Deniel"
RECEIVED=1

do_work

PAPER_ID=75
TITLE="Why Virtualization Fragmentation Sucks"
BIO_ID="1789"
CONTENT_KEY="260"
AUTHOR="Justin M Forbes"
RECEIVED=1

do_work

PAPER_ID=16
TITLE="A new network filesystem is born: comparison of SMB2, CIFS, and NFS"
BIO_ID="971"
CONTENT_KEY="126"
AUTHOR="Steve  French"
RECEIVED=1

do_work

PAPER_ID=17
TITLE="Supporting the Allocation of Large Contiguous Regions of Memory"
BIO_ID="1747"
CONTENT_KEY="80"
AUTHOR="Mel  Gorman"
RECEIVED=1

do_work

PAPER_ID=18
TITLE="Kernel Scalability---Expanding the Horizon Beyond Fine Grain Locks"
BIO_ID="2564"
CONTENT_KEY="222"
AUTHOR="Corey D Gough"
RECEIVED=1

do_work

PAPER_ID=19
TITLE="Kdump: Smarter, Easier, Trustier"
BIO_ID="978"
CONTENT_KEY="4"
AUTHOR="Vivek  Goyal"
RECEIVED=1

do_work

## PAPER_ID=20
## TITLE="Power managed memory, Implications and challenges"
## BIO_ID="193"
## CONTENT_KEY="0"
## AUTHOR="mark  gross"
## XECEIVED=0
## 
## do_work

PAPER_ID=21
TITLE="Using KVM to run Xen guests without Xen"
BIO_ID="1752"
CONTENT_KEY="110"
AUTHOR="Ryan A Harper"
RECEIVED=1

do_work

PAPER_ID=22
TITLE="Djprobe - probing kernel with the smallest overhead."
BIO_ID="2475"
CONTENT_KEY="97"
AUTHOR="Masami  Hiramatsu"
RECEIVED=1

do_work

# PAPER_ID=23
# TITLE="It Pays to Think Different - another approach to reduce boot time and system load"
# BIO_ID="20"
# CONTENT_KEY="0"
# AUTHOR="Dirk  Hohndel"
# XECEIVED=0
# 
# do_work

PAPER_ID=24
TITLE="Desktop integration of Bluetooth"
BIO_ID="894"
CONTENT_KEY="156"
AUTHOR="Marcel  Holtmann"
RECEIVED=1

do_work

PAPER_ID=25
TITLE="How virtualization makes power management different"
BIO_ID="2544"
CONTENT_KEY="77"
AUTHOR="Yu  Ke"
RECEIVED=1

do_work

PAPER_ID=26
TITLE="Ptrace, Utrace, Uprobes: Lightweight, Dynamic Tracing of User Apps"
BIO_ID="70"
CONTENT_KEY="215"
AUTHOR="James A. Keniston"
RECEIVED=1

do_work

PAPER_ID=27
TITLE="kvm: the Kernel-based Virtual Machine for Linux"
BIO_ID="2230"
CONTENT_KEY="10"
AUTHOR="Avi  Kivity"
RECEIVED=1

do_work

# Formerly Kir Kolyshkin; change of speakers.
PAPER_ID=28
TITLE="Resource management: the Beancounters"
BIO_ID="2192"
CONTENT_KEY="29"
AUTHOR="Denis Lunev"
RECEIVED=1

do_work

PAPER_ID=29
TITLE="Linux Telephony"
BIO_ID="1139"
CONTENT_KEY="68"
AUTHOR="Paul  Komkoff"
RECEIVED=1

do_work

PAPER_ID=30
TITLE="Linux Kernel development, who is doing it, what are they doing, and who is sponsoring it (with pretty graphs and a few posters)"
BIO_ID="206"
CONTENT_KEY="8"
AUTHOR="Greg  Kroah-Hartman"
RECEIVED=1

do_work

PAPER_ID=31
TITLE="Implementing Democracy, a large scale cross-platform desktop application"
BIO_ID="2297"
CONTENT_KEY="238"
AUTHOR="Christopher James Lahey"
RECEIVED=1

do_work

PAPER_ID=32
TITLE="Extreme High Performance Computing or Why Microkernels suck"
BIO_ID="866"
CONTENT_KEY="214"
AUTHOR="Christoph H Lameter"
RECEIVED=1

do_work

PAPER_ID=33
TITLE="Performance and Availability Characterization for Linux Servers "
BIO_ID="2500"
CONTENT_KEY="18"
AUTHOR="Vasily  Linkov"
RECEIVED=1

do_work

PAPER_ID=34
TITLE="Turning the Page on hugetlb interfaces"
BIO_ID="1737"
CONTENT_KEY="154"
AUTHOR="Adam G. Litke"
RECEIVED=1

do_work

PAPER_ID=35
TITLE="Manageable virtual appliances"
BIO_ID="2567"
CONTENT_KEY="140"
AUTHOR="David  Lutterkort"
RECEIVED=1

do_work

## PAPER_ID=36
## TITLE="Kernel memory leak detector"
## BIO_ID="1716"
## CONTENT_KEY="0"
## AUTHOR="Catalin  Marinas"
## XECEIVED=0
## 
## do_work

PAPER_ID=37
TITLE="Everything is a virtual filesystem: libferris"
BIO_ID="0"
CONTENT_KEY="9"
AUTHOR="Ben Martin"
RECEIVED=1

do_work

PAPER_ID=38
TITLE="Unifying virtual drivers"
BIO_ID="2591"
CONTENT_KEY="219"
AUTHOR="Jon  Mason"
RECEIVED=1

do_work

PAPER_ID=39
TITLE="The New Ext4 Filesystem: current status and future plans"
BIO_ID="2565"
CONTENT_KEY="179"
AUTHOR="Avantika  Mathur"
RECEIVED=1

do_work

PAPER_ID=40
TITLE="Resource Control and Isolation: Adding Generic Process Containers to the Linux Kernel"
BIO_ID="2574"
CONTENT_KEY="147"
AUTHOR="Paul B Menage"
RECEIVED=1

do_work

PAPER_ID=41
TITLE="KvmFS: Virtual Machine Partitioning For Clusters and Grids"
BIO_ID="2559"
CONTENT_KEY="102"
AUTHOR="Andrey  Mirtchovski"
RECEIVED=1

do_work

PAPER_ID=42
TITLE="Linux-based Ultra Mobile PCs: Analysis of Networking stacks, File systems and Design Recommendations"
BIO_ID="2572"
CONTENT_KEY="155"
AUTHOR="Rajeev  Muralidhar"
RECEIVED=1

do_work

PAPER_ID=43
TITLE="Where is your application stuck"
BIO_ID="2476"
CONTENT_KEY="0"
AUTHOR="Shailabh  Nagar"
RECEIVED=1

do_work

PAPER_ID=44
TITLE="Trusted Secure Embedded Linux: From Hardware Root of Trust to Mandatory Access Control"
BIO_ID="2573"
CONTENT_KEY="162"
AUTHOR="Hadi  Nahari"
RECEIVED=1

do_work

PAPER_ID=45
TITLE="Hybrid-Virtualization --- Ideal Virtualization for Linux"
BIO_ID="527"
CONTENT_KEY="192"
AUTHOR="Jun  Nakajima"
RECEIVED=1

do_work

PAPER_ID=46
TITLE="Readahead: time-travel techniques for desktop and embedded systems"
BIO_ID="1027"
CONTENT_KEY="40"
AUTHOR="Michael  Opdenacker"
RECEIVED=1

do_work

PAPER_ID=47
TITLE="Semantic Patches for Collateral Evolutions in Device Drivers"
BIO_ID="2562"
CONTENT_KEY="194"
AUTHOR="Yoann  Padioleau"
RECEIVED=1

do_work

PAPER_ID=48
TITLE="cpuidle - Do nothing, efficiently..."
BIO_ID="77"
CONTENT_KEY="109"
AUTHOR="Venkatesh  Pallipadi"
RECEIVED=1

do_work

PAPER_ID=49
TITLE="My bandwidth is wider than yours: Ultrawideband, Wireless USB and WiNET in Linux"
BIO_ID="238"
CONTENT_KEY="43"
AUTHOR="Inaky  Perez-Gonzalez"
RECEIVED=1

do_work

PAPER_ID=50
TITLE="Zumastor Linux Storage Server"
BIO_ID="2311"
CONTENT_KEY="189"
AUTHOR="Daniel  Phillips"
RECEIVED=1

do_work

PAPER_ID=51
TITLE="Cleaning up the Linux desktop audio mess"
BIO_ID="2580"
CONTENT_KEY="158"
AUTHOR="Lennart  Poettering"
RECEIVED=1

do_work

PAPER_ID=52
TITLE="Linux-VServer: Resource Efficient OS-Level Virtualization"
BIO_ID="2595"
CONTENT_KEY="223"
AUTHOR="Herbert  Potzl"
RECEIVED=1

do_work

PAPER_ID=53
TITLE="Internals of the RT Patch"
BIO_ID="1406"
CONTENT_KEY="75"
AUTHOR="Steven  Rostedt"
RECEIVED=1

do_work

PAPER_ID=54
TITLE="lguest: Implementing the little Linux hypervisor"
BIO_ID="9"
CONTENT_KEY="245"
AUTHOR="Rusty  Russell"
RECEIVED=1

do_work

PAPER_ID=55
TITLE="ext4 online defragmentation"
BIO_ID="2471"
CONTENT_KEY="103"
AUTHOR="Takashi  Sato"
RECEIVED=1

do_work

PAPER_ID=56
TITLE="The Hiker Project: An Application Framework for Mobile Linux Devices"
BIO_ID="2566"
CONTENT_KEY="114"
AUTHOR="David  Schlesinger"
RECEIVED=1

do_work

################## dropped.... 
# PAPER_ID=57
# TITLE="With No Tears: Building A Mobile Linux Device"
# BIO_ID="4"
# CONTENT_KEY="0"
# AUTHOR="Tariq  Shureih"
# ECEIVED=
# 
# do_work

PAPER_ID=58
TITLE="Getting maximum mileage out of tickless"
BIO_ID="959"
CONTENT_KEY="225"
AUTHOR="Suresh  Siddha"
RECEIVED=1

do_work

PAPER_ID=59
TITLE="Containers: Challenges with memory resource controller and its performance"
BIO_ID="2546"
CONTENT_KEY="128"
AUTHOR="Balbir  Singh"
RECEIVED=1

do_work

PAPER_ID=60
TITLE="Kernel Support for Stackable File Systems"
BIO_ID="2596"
CONTENT_KEY="236"
AUTHOR="Josef  Sipek"
RECEIVED=1

do_work

PAPER_ID=61
TITLE="Linux rollout at Nortel"
BIO_ID="2439"
CONTENT_KEY="36"
AUTHOR="Ernest  Szeideman"
RECEIVED=1

do_work

# PAPER_ID=62
# TITLE="EDAC: What is an EDAC and where you stick it?"
# BIO_ID="2518"
# CONTENT_KEY="181"
# AUTHOR="Doug  Thompson"
# XECEIVED=0
# 
# do_work

PAPER_ID=63
TITLE="Request-based device-mapper and multipath dynamic load-balancing"
BIO_ID="1347"
CONTENT_KEY="163"
AUTHOR="Kiyoshi  Ueda"
RECEIVED=1

do_work

PAPER_ID=64
TITLE="Short-term solution for 3G networks in Linux: umtsmon"
BIO_ID="2185"
CONTENT_KEY="85"
AUTHOR="Klaas  vanGend"
RECEIVED=1

do_work

# PAPER_ID=65
# TITLE="Architecture of an in-kernel GSM TS 07.10 multiplex"
# BIO_ID="719"
# CONTENT_KEY="0"
# AUTHOR="Harald  Welte"
# XECEIVED=0
# 
# do_work

PAPER_ID=66
TITLE="The GFS2 Filesystem"
BIO_ID="2515"
CONTENT_KEY="37"
AUTHOR="Steven John Whitehouse"
RECEIVED=1

do_work

PAPER_ID=67
TITLE="Unified Driver Tracing Infrastructure"
BIO_ID="2550"
CONTENT_KEY="107"
AUTHOR="David   Wilder"
RECEIVED=1

do_work

## PAPER_ID=68
## TITLE="Why Virtualization Sucks - why 101 ways to do things is painful"
## BIO_ID="785"
## CONTENT_KEY="191"
## AUTHOR="Matthew S. Wilson"
## XECEIVED=0
## 
## do_work
## 
## PAPER_ID=69
## TITLE="Balancing Energy Consumption with Speed on Large Machines"
## BIO_ID="1699"
## CONTENT_KEY="119"
## AUTHOR="Darrick J Wong"
## XECEIVED=0
##
## do_work

PAPER_ID=70
TITLE="Linux readahead: less tricks for more"
BIO_ID="2510"
CONTENT_KEY="31"
AUTHOR="Fengguang  Wu"
RECEIVED=1

do_work

PAPER_ID=71
TITLE="Regression Test Framework and Kernel Execution Coverage"
BIO_ID="611"
CONTENT_KEY="129"
AUTHOR="Hiro  Yoshioka"
RECEIVED=1

do_work

PAPER_ID=72
TITLE="Enable PCI Express Advanced Error Reporting in Kernel"
BIO_ID="2096"
CONTENT_KEY="52"
AUTHOR="Yanmin  Zhang"
RECEIVED=1

do_work

PAPER_ID=73
TITLE="Linux network multiple hardware queues support"
BIO_ID="761"
CONTENT_KEY="73"
AUTHOR="Yi  Zhu"
RECEIVED=1

do_work

PAPER_ID=74
TITLE="Concurrent Pagecache"
BIO_ID="1665"
CONTENT_KEY="66"
AUTHOR="Peter  Zijlstra"
RECEIVED=1

do_work

do_wrapup
