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

These options may be dynamically updated by issuing a SIGHUP to the ganesha.nfsd
process.

LOG {}
--------------------------------------------------------------------------------
Default_log_level(token,default EVENT)

   If this option is NOT set, the fall back log level will be that specified in
   the -N option on the command line if that is set, otherwise the fallback
   level is EVENT.

    If a SIGHUP is issued, any components not specified in LOG { COMPONENTS {} }
    will be reset to this value.

The log levels are:

NULL, FATAL, MAJ, CRIT, WARN, EVENT,
INFO, DEBUG, MID_DEBUG, M_DBG,
FULL_DEBUG, F_DBG

RPC_Debug_Flags(uint32, range 0 to UINT32_MAX, default 7)
    Debug flags for TIRPC (default 7 matches log level default EVENT).

    These flags are only used if the TIRPC component is set to DEBUG

LOG { COMPONENTS {} }
--------------------------------------------------------------------------------
**Default_log_level(token,default EVENT)**
    These entries are of the form:
        COMPONENT = LEVEL;

    The components are:
        ALL, LOG, MEMLEAKS, FSAL, NFSPROTO,
        NFS_V4, EXPORT, FILEHANDLE, DISPATCH, CACHE_INODE,
        CACHE_INODE_LRU, HASHTABLE, HASHTABLE_CACHE, DUPREQ,
        INIT, MAIN, IDMAPPER, NFS_READDIR, NFS_V4_LOCK,
        CONFIG, CLIENTID, SESSIONS, PNFS, RW_LOCK, NLM, RPC,
        TIRPC, NFS_CB, THREAD, NFS_V4_ACL, STATE, 9P,
        9P_DISPATCH, FSAL_UP, DBUS, NFS_MSK

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

        default none

    ALL is a special component that when set, sets all components to the
    specified value, overriding any that are explicitly set. Note that if
    ALL is then removed from the config and SIGHUP is issued, all components
    will revert to what is explicitly set, or Default_Log_Level if that is
    specified, or the original log level from the -N command line option
    if that was set, or the code default of EVENT.

    TIRPC is a special component that also sets the active RPC_Debug_Flags.
    If the level for TIRPC is DEBUG or MID_DEBUG, the custom RPC_Debug_Flags
    set by that parameter will be used, otherwise flags will depend on the
    level the TIRPC component is set to:

        NULL or FATAL: 0

        CRIT or MAJ: TIRPC_DEBUG_FLAG_ERROR

        WARN: TIRPC_DEBUG_FLAG_ERROR | TIRPC_DEBUG_FLAG_WARN

        EVENT or INFO: TIRPC_DEBUG_FLAG_ERROR | TIRPC_DEBUG_FLAG_WARN | TIRPC_DEBUG_FLAG_EVENT

        DEBUG or MID_DEBUG: RPC_Debug_Flags

        FULL_DEBUG: 0xffffffff

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
