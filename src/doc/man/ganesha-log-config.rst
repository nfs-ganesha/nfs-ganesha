===================================================================
ganesha-log-config -- NFS Ganesha Log Configuration File
===================================================================

.. program:: ganesha-log-config


SYNOPSIS
==========================================================

| /etc/ganesha/ganesha.conf

DESCRIPTION
==========================================================

NFS-Ganesha reads the configuration data from:
| /etc/ganesha/ganesha.conf

This file lists NFS-Ganesha Log config options.

LOG {}
--------------------------------------------------------------------------------
Default_log_level(token,default EVENT)

The log levels are:

NULL, FATAL, MAJ, CRIT, WARN, EVENT,
INFO, DEBUG, MID_DEBUG, M_DBG,
FULL_DEBUG, F_DBG

RPC_Debug_Flags(uint32, range 0 to UINT32_MAX, default 7)
    Debug flags for TIRPC (default 7 matches log level default EVENT).

LOG { COMPONENTS {} }
--------------------------------------------------------------------------------
**Default_log_level(token,default EVENT)**
    These entries are of the form:
        COMPONENT = LEVEL;

    The components are:
        ALL, LOG, LOG_EMERG, MEMLEAKS, FSAL, NFSPROTO,
        NFS_V4, EXPORT, FILEHANDLE, DISPATCH, CACHE_INODE,
        CACHE_INODE_LRU, HASHTABLE, HASHTABLE_CACHE, DUPREQ,
        INIT, MAIN, IDMAPPER, NFS_READDIR, NFS_V4_LOCK,
        CONFIG, CLIENTID, SESSIONS, PNFS, RW_LOCK, NLM, RPC,
        NFS_CB, THREAD, NFS_V4_ACL, STATE, 9P, 9P_DISPATCH,
        FSAL_UP, DBUS

    Some synonyms are:
        FH = FILEHANDLE
        HT = HASHTABLE
        INODE_LRU = CACHE_INODE_LRU
        INODE = CACHE_INODE
        DISP = DISPATCH
        LEAKS = MEMLEAKS
        NFS3 = NFSPROTO
        NFS4 = NFS_V4
        HT_CACHE = HASHTABLE_CACHE
        NFS_STARTUP = INIT
        NFS4_LOCK = NFS_V4_LOCK
        NFS4_ACL = NFS_V4_ACL
        9P_DISP = 9P_DISPATCH

    The log levels are:
        NULL, FATAL, MAJ, CRIT, WARN, EVENT,
        INFO, DEBUG, MID_DEBUG, M_DBG,
        FULL_DEBUG, F_DBG

        default EVENT

LOG { FACILITY {} }
--------------------------------------------------------------------------------
**name(string, no default)**

**destination(string, no default, must be supplied)**

**max_level(token,default FULL_DEBUG)**
    The log levels are:

        NULL, FATAL, MAJ, CRIT, WARN, EVENT,
        INFO, DEBUG, MID_DEBUG, M_DBG,
        FULL_DEBUG, F_DBG

**headers(token, values [none, component, all], default all)**

**enable(token, values [idle, active, default], default idle)**

LOG { FORMAT {} }
--------------------------------------------------------------------------------
date_format(enum,default ganesha)
    Possible values:
        ganesha, true, local, 8601, ISO-8601,
        ISO 8601, ISO, syslog, syslog_usec,
        false, none, user_defined

time_format(enum,default ganesha)
    Possible values:
        ganesha, true, local, 8601, ISO-8601,
        ISO 8601, ISO, syslog, syslog_usec,
        false, none, user_defined

**user_date_format(string, no default)**

**user_time_format(string, no default)**

**EPOCH(bool, default true)**

**CLIENTIP(bool, default false)**

**HOSTNAME(bool, default true)**

**PROGNAME(bool, default true)**

**PID(bool, default true)**

**THREAD_NAME(bool, default true)**

**FILE_NAME(bool, default true)**

**LINE_NUM(bool, default true)**

**FUNCTION_NAME(bool, default true)**

**COMPONENT(bool, default true)**

**LEVEL(bool, default true)**

See also
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
