.. SPDX-License-Identifier: LGPL-3.0-or-later

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

Display_UTC_Timestamp(bool, default false)
    Flag to enable displaying UTC date/time in log messages instead of localtime.

Match_Policy(token, default MATCH_ANY)
    Specifies how conditional logging rules are evaluated when Clients and/or
    Exports are configured.

    MATCH_ANY
        Enable conditional logging if either the client OR the export matches.
    MATCH_ALL
        Enable conditional logging only if both the client AND the export match.

LOG { COMPONENTS {} }
--------------------------------------------------------------------------------
**Default_log_level(token,default EVENT)**
    These entries are of the form:
        COMPONENT = LEVEL;

    The components are:
        ALL, LOG, MEMLEAKS, FSAL, NFSPROTO, RECOVERY,
        NFS_V4, EXPORT, FILEHANDLE, DISPATCH, MDCACHE,
        MDCACHE_LRU, HASHTABLE, HASHTABLE_CACHE, DUPREQ,
        INIT, MAIN, IDMAPPER, NFS_READDIR, NFS_V4_LOCK,
        CONFIG, CLIENTID, SESSIONS, PNFS, RW_LOCK, NLM,
        TIRPC, NFS_CB, THREAD, NFS_V4_ACL, STATE, 9P,
        9P_DISPATCH, FSAL_UP, DBUS, NFS_MSK, XPRT, GRPC

    Some synonyms are:
        FH = FILEHANDLE
        HT = HASHTABLE
        CACHE_INODE_LRU = MDCAHCE_LRU
        CACHE_INODE = MDCACHE
        INODE_LRU = MDCAHCE_LRU
        INODE = MDCACHE
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

LOG { CONDITIONAL {} }
----------------------
Conditional logging allows selective override of log levels for specific
clients or exports, while all other traffic continues to use the global
COMPONENTS settings. This mechanism is useful for debugging targeted
activity without increasing verbosity system-wide.

How It Works

Global Filtering (COMPONENTS block)
All requests are first evaluated against the log levels defined in the top-level
COMPONENTS block.These levels serve as the default for all clients and exports.

Conditional Overrides (Conditional block)
The Conditional block provides a way to override component
log levels only for the clients or exports explicitly listed.

If a request matches a client and/or export mentioned in the Conditional block:
→ Logging levels defined inside Conditional apply.

If a request does not match any entry in the Conditional block:
→ Logging uses only the global COMPONENTS settings.

This behavior ensures that only selected traffic receives more or less verbose
logging, while all remaining traffic follows the normal log-level configuration.

Example::

    LOG {
      COMPONENTS {
        FSAL = WARN;
        NFS4 = CRIT;
      }

      Match_Policy = MATCH_ANY;

      Conditional {
        Clients = 192.0.2.25;
        Exports = 101;

        COMPONENT_ALL = EVENT;
        NFS4 = DEBUG;
      }
    }

Explanation::

  * Requests originating from client/export inside the Conditional block
    are logged strictly according to conditional components (e.g. NFS4 = DEBUG,
    ALL = EVENT).
  * Requests from all other clients or exports are logged strictly according to
    the top-level COMPONENTS block (e.g., FSAL = WARN, NFS4 = CRIT).

**Clients(client list, default: empty)**

    * Client list entries can take on one of the following forms. This
      parameter may be repeated to extend the list.

    * x.x.x.x  IPv6 addresses are only allowed

**Exports(Export list, default: empty)**
    * Export list entries can take list of Export_IDs.

**COMPONENT = LEVEL**
    The components are:

                ALL, LOG, MEMLEAKS, FSAL, NFSPROTO,
                NFS_V4, EXPORT, FILEHANDLE, DISPATCH, MDCACHE,
                MDCACHE_LRU, HASHTABLE, HASHTABLE_CACHE, DUPREQ,
                INIT, MAIN, IDMAPPER, NFS_READDIR, NFS_V4_LOCK,
                CONFIG, CLIENTID, SESSIONS, PNFS, RW_LOCK, NLM,
                TIRPC, NFS_CB, THREAD, NFS_V4_ACL, STATE, 9P,
                9P_DISPATCH, FSAL_UP, DBUS, NFS_MSK, XPRT

    Some synonyms are:

    FH = FILEHANDLE
    HT = HASHTABLE
    CACHE_INODE_LRU = MDCAHCE_LRU
    CACHE_INODE = MDCACHE
    INODE_LRU = MDCAHCE_LRU
    INODE = MDCACHE
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
                FULL_DEBUG, F_DBG], default FULL_DEBUG

    ALL is a special component that when set, sets all components to the
    specified value, overriding any that are explicitly set. default is
    set to FULL_DEBUG. COMPONENT_ALL in Conditional acts as fallback
    for any unspecified components.

LOG { FACILITY {} }
--------------------------------------------------------------------------------
This block may be repeated to configure multiple log facilities.

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

**OP_ID(bool, default false)**

**CLIENT_REQ_XID(bool, default false)**

**LOG_INDEX(bool, default false)**

**LOG_INDEX_WRAP_AROUND(uint32_t, default 100000)**

LOG { ROTATE {} }
--------------------------------------------------------------------------------
**size_kb(uint32_t, default 0)**
    Rotate the log file when the size in KB exceeds this value, if zero,
    do not rotate based on size.
**time_sec(uint32_t, default 0)**
    Rotate the log file when the time in seconds exceeds this value, if zero,
    do not rotate based on time.

See also
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
