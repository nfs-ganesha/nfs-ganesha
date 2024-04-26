===================================================================
ganesha-core-config -- NFS Ganesha Core Configuration File
===================================================================

.. program:: ganesha-core-config


SYNOPSIS
==========================================================

| /etc/ganesha/ganesha.conf

DESCRIPTION
==========================================================

NFS-Ganesha reads the configuration data from:
| /etc/ganesha/ganesha.conf

This file lists NFS related core config options.

NFS_CORE_PARAM {}
--------------------------------------------------------------------------------
Core parameters:

HAProxy_Hosts (host list, empty)
    This is the list of hosts that can serve as HAProxy load balancers/proxies
    that will use the HAProxy protocol to indicate to Ganesha the actual end
    client IP address. This parameter may be repeated to extend the list.

    Host list entries can take on one of the following forms:

        \*          Match any host
        @name       Netgroup name
        x.x.x.x/y   IPv4 network address, IPv6 addresses are also allowed
                    but the format is too complex to show here
        wildcarded  If the string contains at least one ? or *
                    character (and is not simply "*"), the string is
                    used to pattern match host names. Note that [] may
                    also be used, but the pattern MUST have at least one
                    ? or *
        hostname    Match a single host (match is by IP address, all
                    addresses returned by getaddrinfo will match, the
                    getaddrinfo call is made at config parsing time)
        IP address  Match a single host

NFS_Port (uint16, range 0 to UINT16_MAX, default 2049)
    Port number used by NFS Protocol.

MNT_Port (uint16, range 0 to UINT16_MAX, default 0)
    Port number used by MNT Protocol.

NLM_Port (uint16, range 0 to UINT16_MAX, default 0)
    Port number used by NLM Protocol.

Rquota_Port (uint16, range 0 to UINT16_MAX, default 875)
    Port number used by Rquota Protocol.

NFS_RDMA_Port (uint16, range 0 to UINT16_MAX, default 20049)
    Port number used by NFS Over RDMA Protocol.

NFS_RDMA_Protocol_Versions(enum list, default [4.0])
    Possible values:
        (NONE, 3, v3, NFS3, NFSv3, 4.0, v4.0, NFS4.0, NFSv4.0, 4.1, ALL)
    Supported NFS Version for NFS Over RDMA. By default, NFSv4.0 is enabled.

Monitoring_Port (uint16, range 0 to UINT16_MAX, default 9587)
    Port number used to export monitoring metrics.

Enable_Dynamic_Metrics (bool, default true)
    Whether to create metrics labels on the fly based on client-ip,
    export name, etc. Provides more debugging information, but significantly
    reduces performance. Enabled by default for backward compatibility.

Bind_addr(IPv4 or IPv6 addr, default 0.0.0.0)
    The address to which to bind for our listening port.

NFS_Program(uint32, range 1 to INT32_MAX, default 100003)
    RPC program number for NFS.

MNT_Program(uint32, range 1 to INT32_MAX, default 100005)
    RPC program number for MNT.

NLM_Program(uint32, range 1 to INT32_MAX, default 100021)
    RPC program number for NLM.

Drop_IO_Errors(bool, default false)
    For NFSv3, whether to drop rather than reply to requests yielding I/O
    errors. It results in client retry.

Drop_Inval_Errors(bool, default false)
    For NFSv3, whether to drop rather than reply to requests yielding invalid
    argument errors.  False by default and settable with Drop_Inval_Errors.

Drop_Delay_Errors(bool, default false)
    For NFSv3, whether to drop rather than reply to requests yielding delay
    errors.  False by default and settable with Drop_Delay_Errors.

Plugins_Dir(path, default "/usr/lib64/ganesha")
    Path to the directory containing server specific modules

Enable_NFS_Stats(bool, default true)
    Whether to collect performance statistics. By default the performance
    counting is enabled. Enable_NFS_Stats can be enabled or disabled
    dynamically via ganesha_stats.

Enable_Fast_Stats(bool, default false)
    Whether to use fast stats. If enabled this will skip statistics counters
    collection for per client and per export.

Enable_FSAL_Stats(bool, default false)
    Whether to count and collect FSAL specific performance statistics.
    Enable_FSAL_Stats can be enabled or disabled dynamically via ganesha_stats

Enable_FULLV3_Stats(bool, default false)
    Whether to count and collect "detailed statistics" for NFSv3.
    Enable_FULLV3_Stats can be enabled or disabled dynamically via
    ganesha_stats.

Enable_FULLV4_Stats(bool, default false)
    Whether to count and collect "detailed statistics" for NFSv4.
    Enable_FULLV4_Stats can be enabled or disabled dynamically via
    ganesha_stats.

Enable_CLNT_AllOps_Stats(bool, default false)
    Whether to count and collect statistics for all NFS operations requested
    by NFS clients. Enable_CLNT_AllOps_Stats can be enabled or disabled
    dynamically via ganesha_stats.

Short_File_Handle(bool, default false)
    Whether to use short NFS file handle to accommodate VMware NFS client.
    Enable this if you have a VMware NFSv3 client. VMware NFSv3 client has a max
    limit of 56 byte file handles.

Manage_Gids_Expiration(int64, range 0 to 7*24*60*60, default 30*60)
    How long the server will trust information it got by calling getgroups()
    when "Manage_Gids = TRUE" is used in a export entry.

heartbeat_freq(uint32, range 0 to 5000 default 1000)
    Frequency of dbus health heartbeat in ms.

Enable_NLM(bool, default true)
    Whether to support the Network Lock Manager protocol.

Disable_NLM_SHARE(bool, default false)
    This option allows disabling support for the NLM4PROC_SHARE and
    NLM4PROC_UNSHARE RPC procedures that implement share reservations for
    NFSv3 via NLM. With this set to true, these procedures will fail.

Blocked_Lock_Poller_Interval(int64, range 0 to 180, default 10)
    Polling interval for blocked lock polling thread

Protocols(enum list, default [3,4,9P])
    Possible values:
        3, 4, NFS3, NFS4, V3, V4, NFSv3, NFSv4, 9P

    The protocols that Ganesha will listen for.  This is a hard limit, as this
    list determines which sockets are opened.  This list can be restricted per
    export, but cannot be expanded.

NSM_Use_Caller_Name(bool, default false)
    Whether to use the supplied name rather than the IP address in NSM
    operations.

Clustered(bool, default true)
    Whether this Ganesha is part of a cluster of Ganeshas. Its vendor specific
    option.

fsid_device(bool, default false)
    Whether to use device major/minor for fsid.

resolve_fs_retries(uint32_t, range 1 to 1000, default 10)
    How many times to attempt stat while resolving POSIX filesystems for
    exports.

resolve_fs_delay(uint32_t, range 1 to 1000, default 100)
    How long to delay between stat attempts while resolving POSIX filesystems
    for exports.

mount_path_pseudo(bool, default false)
    Whether to use Pseudo (true) or Path (false) for NFS v3 and 9P mounts.

    This option defaults to false for backward compatibility, however, for
    new setups, it's strongly recommended to be set true since it then means
    the same server path for the mount is used for both v3 and v4.x.

    Note that as an export related option, it seems very desirable to be
    able to change this on config reload, unfortunately, at the moment it
    is NOT changeable on config reload. A restart is necessary to change this.

Dbus_Name_Prefix
    DBus name prefix. Required if one wants to run multiple ganesha instances on
    single host. The prefix should be different for every ganesha instance. If
    this is set, the dbus name will be <prefix>.org.ganesha.nfsd

Enable_UDP(enum, values [False, True, Mount], default True)
    Whether to create UDP listeners for Mount, NFS, NLM, RQUOTA, and register
    them with portmapper. Set to false, e.g., to run as non-root. Set to Mount
    to enable only Mount UDP listener.

Max_Uid_To_Group_Reqs(uint32, range 0 to INT32_MAX, default 0)
    Maximum number of concurrent uid2grp requests that can be made by ganesha.
    In environments with a slow Directory Service Provider, where users are
    part of large number of groups, and Manage_Gids is set to True, uid2grp
    queries made by ganesha can fail if a large number of them are made in
    parallel. This option throttles the number of concurrent uid2grp queries
    that ganesha makes.

Enable_V3fh_Validation_For_V4(bool, default false)
    Set true to enforce when v3 file handle used for v4

Readdir_Res_Size(uint32, range 4096 to 64*1024*1024, default 32*1024)
    Response size of readdir request.
    Suggested values are 4096,8192,16384 and 32768. Recommended 16384(16K) if
    readdir(ls command) operation performed on directory which has more files.

Readdir_Max_Count(uint32, range 32 to 1024*1024, default 1024*1024)
    Maximum number of directory entries returned for a readdir request.
    Suggested values are 4096,8192,16384 and 32768. Recommended 16384(16K) if
    readdir(ls command) operation performed on directory which has more files.

Getattrs_In_Complete_Read(bool, default true)
    Whether to call extra getattrs after read, in order to check file size and
    validate the EOF flag correctness. Needed for ESXi client compatibility
    when FSAL's don't set it correctly.

Enable_malloc_trim(bool, default false)
    Set true to enable dynamic malloc_trim support.

Malloc_trim_MinThreshold(uint32, range 1 to INT32_MAX, default 15*1024)
    Minimum threshold value to call malloc_trim. The malloc_trim will be called
    once memory allocation exceeds minimum value. Size in MB's.
    Note, this setting has no effect when Enable_malloc_trim is set to false.

enable_rpc_cred_fallback(bool,  default false)
    if  Manage_Gids=True and group resolution fails,
    then use gid data from rpc request.

Enable_Connection_Manager(bool, default false)
    When enabled, a client (from the same source IP address), is allowed to
    be connected to a single Ganesha server at a specific point in time.
    See details in connection_manager.h

Connection_Manager_Timeout_sec(uint32, range 0 to UINT32_MAX, default 2*60)
    Timeout for waiting until client is fully disconnected from other Ganesha
    servers.

Unique_Server_Id(uint32, range 0 to UINT32_MAX, default 0)
   Unique value to the ganesha node, to diffrintiate it for the rest of the
   node. will be used as prefix for the Client id, to make sure it is
   unique between ganesha nodes and file write verifier.
   if 0 is supplied server boot epoch time in seconds will be used

Parameters controlling TCP DRC behavior:
----------------------------------------

DRC_Disabled(bool, default false)
    Whether to disable the DRC entirely.

DRC_Recycle_Hiwat(uint32, range 1 to 1000000, default 1024)
    High water mark for number of DRCs in recycle queue.

TCP_Npart(uint32, range 1 to 20, default 1)
    Number of partitions in the tree for the TCP DRC.

DRC_TCP_Size(uint32, range 1 to 32767, default 1024)
    Maximum number of requests in a transport's DRC.

DRC_TCP_Cachesz(uint32, range 1 to 255, default 127)
    Number of entries in the O(1) front-end cache to a TCP Duplicate Request
    Cache.

DRC_TCP_Hiwat(uint32, range 1 to 256, default 64)
    High water mark for a TCP connection's DRC at which to start retiring
    entries if we can.

DRC_TCP_Recycle_Npart(uint32, range 1 to 20, default 7)
    Number of partitions in the recycle tree that holds per-connection DRCs so
    they can be used on reconnection (or recycled.)

DRC_TCP_Recycle_Expire_S(uint32, range 0 to 60*60, default 600)
    How long to wait (in seconds) before freeing the DRC of a disconnected
    client.

DRC_TCP_Checksum(bool, default true)
    Whether to use a checksum to match requests as well as the XID


Parameters controlling UDP DRC behavior:
----------------------------------------

DRC_UDP_Npart(uint32, range 1 to 100, default 7)
    Number of partitions in the tree for the UDP DRC.

DRC_UDP_Size(uint32, range 512, to 32768, default 32768)
    Maximum number of requests in the UDP DRC.

DRC_UDP_Cachesz(uint32, range 1 to 2047, default 599)
    Number of entries in the O(1) front-end cache to the UDP Duplicate Request
    Cache.

DRC_UDP_Hiwat(uint32, range 1 to 32768, default 16384)
    High water mark for the UDP DRC at which to start retiring entries if we can

DRC_UDP_Checksum(bool, default true)
    Whether to use a checksum to match requests as well as the XID.


Parameters affecting the relation with TIRPC:
--------------------------------------------------------------------------------

RPC_Max_Connections(uint32, range 1 to 1000000, default 1024)
    Maximum number of connections for TIRPC.

RPC_Idle_Timeout_S(uint32, range 0 to 60*60, default 300)
    Idle timeout (seconds). Default to 300 seconds.

MaxRPCSendBufferSize(uint32, range 1 to 1048576*9, default 1048576)
    Size of RPC send buffer.

MaxRPCRecvBufferSize(uint32, range 1 to 1048576*9, default 1048576)
    Size of RPC receive buffer.

RPC_Ioq_ThrdMax(uint32, range 1 to 1024*128 default 200)
    TIRPC ioq max simultaneous io threads

RPC_GSS_Npart(uint32, range 1 to 1021, default 13)
    Partitions in GSS ctx cache table

RPC_GSS_Max_Ctx(uint32, range 1 to 1048576, default 16384)
    Max GSS contexts in cache. Default 16k

RPC_GSS_Max_Gc(uint32, range 1 to 1048576, default 200)
    Max entries to expire in one idle check


Parameters for TCP:
--------------------------------------------------------------------------------

Enable_TCP_keepalive(bool, default true)
    Whether tcp sockets should use SO_KEEPALIVE

TCP_KEEPCNT(UINT32, range 0 to 255, default 0 -> use system defaults)
    Maximum number of TCP probes before dropping the connection

TCP_KEEPIDLE(UINT32, range 0 to 65535, default 0 -> use system defaults)
    Idle time before TCP starts to send keepalive probes

TCP_KEEPINTVL(INT32, range 0 to 65535, default 0 -> use system defaults)
    Time between each keepalive probe


NFS_IP_NAME {}
--------------------------------------------------------------------------------

Index_Size(uint32, range 1 to 51, default 17)
    Configuration for hash table for NFS Name/IP map.

Expiration_Time(uint32, range 1 to 60*60*24, default 3600)
    Expiration time for ip-name mappings.


NFS_KRB5 {}
--------------------------------------------------------------------------------

**PrincipalName(string, default "nfs")**

KeytabPath(path, default "")
    Kerberos keytab.

CCacheDir(path, default "/var/run/ganesha")
    The ganesha credential cache.

Active_krb5(bool, default false)
    Whether to activate Kerberos 5. Defaults to true (if Kerberos support is
    compiled in)


DIRECTORY_SERVICES {}
--------------------------------------------------------------------------------

DomainName(string, default "localdomain")
    Domain to use if we aren't using the nfsidmap.

Idmapping_Active(bool, default true)
    Whether to enable idmapping

Idmapped_User_Time_Validity(int64, range -1 to INT64_MAX, default -1)
    Cache validity in seconds for idmapped-user entries.
    The default value is -1, which indicates fallback to older config --
    "NFS_CORE_PARAM.Manage_Gids_Expiration", for backward compatibility.

Idmapped_Group_Time_Validity(int64, range -1 to INT64_MAX, default -1)
    Cache validity in seconds for idmapped-group entries.
    The default value is -1, which indicates fallback to older config --
    "NFS_CORE_PARAM.Manage_Gids_Expiration", for backward compatibility.

Cache_Users_Max_Count(uint32, range 0 to INT32_MAX, default INT32_MAX)
    Max number of cached idmapped users

Cache_Groups_Max_Count(uint32, range 0 to INT32_MAX, default INT32_MAX)
    Max number of cached idmapped groups

Cache_User_Groups_Max_Count(uint32, range 0 to INT32_MAX, default INT32_MAX)
    Max number of cached user-groups entries

Negative_Cache_Time_Validity(int64, range 0 to INT64_MAX, default 300)
    Cache validity in seconds for negative entries

Negative_Cache_Users_Max_Count(uint32, range 0 to INT32_MAX, default 50000)
    Max number of negative cache users (the ones that failed idmapping)

Negative_Cache_Groups_Max_Count(uint32, range 0 to INT32_MAX, default 50000)
    Max number of negative cache groups (the ones that failed idmapping)

Cache_Reaping_Interval(int64, range 0 to 3650*86400, default 0)
    Cache reaping interval in seconds for idmapped cached entites.
    Its default value is set to 0, which basically means that
    the cache-reaping is disabled.

Pwutils_Use_Fully_Qualified_Names(bool, default false)
    Whether to use fully qualified names for idmapping with pw-utils


NFSv4 {}
--------------------------------------------------------------------------------


Graceless(bool, default false)
    Whether to disable the NFSv4 grace period.

Lease_Lifetime(uint32, range 1 to 120, default 60)
    The NFSv4 lease lifetime.

Grace_Period(uint32, range 0 to 180, default 90)
    The NFS grace period.

DomainName(string, default NULL)
    This config param is deprecated. Use `DomainName` in `DIRECTORY_SERVICES`
    config section.

IdmapConf(path, default "/etc/idmapd.conf")
    Path to the idmap configuration file.

UseGetpwnam(bool, default false if using idmap, true otherwise)
    Whether to use local password (PAM, on Linux) rather than nfsidmap.

Allow_Numeric_Owners(bool, default true)
    Whether to allow bare numeric IDs in NFSv4 owner and group identifiers.

Only_Numeric_Owners(bool, default false)
    Whether to ONLY use bare numeric IDs in NFSv4 owner and group identifiers.

Delegations(bool, default false)
    Whether to allow delegations.

Deleg_Recall_Retry_Delay(uint32_t, range 0 to 10, default 1)
    Delay after which server will retry a recall in case of failures

pnfs_mds(bool, default false)
    Whether this a pNFS MDS server.
    For FSAL Gluster, if this is true, set pnfs_mds in gluster block as well.

pnfs_ds(bool, default false)
    Whether this a pNFS DS server.

RecoveryBackend(enum, default "fs")
    Use different backend for client info:

    - fs : filesystem
    - fs_ng: filesystem (better resiliency)
    - rados_kv : rados key-value
    - rados_ng : rados key-value (better resiliency)
    - rados_cluster: clustered rados backend (active/active)

RecoveryRoot(path, default "/var/lib/nfs/ganesha")
    Specify the root recovery directory for fs or fs_ng recovery backends.

RecoveryDir(path, default "v4recov")
    Specify the recovery directory name for fs or fs_ng recovery backends.

RecoveryOldDir(path, "v4old")
    Specify the recovery old directory name for fs recovery backend.

Minor_Versions(enum list, values [0, 1, 2], default [0, 1, 2])
    List of supported NFSV4 minor version numbers.

Slot_Table_Size(uint32, range 1 to 1024, default 64)
    Size of the NFSv4.1 slot table

Enforce_UTF8_Validation(bool, default false)
    Set true to enforce valid UTF-8 for path components and compound tags

Max_Client_Ids(uint32, range 0 to UINT32_MAX, default 0)
    Specify a max limit on number of NFS4 ClientIDs supported by the
    server. With filesystem recovery backend, each ClientID translates to
    one directory. With certain workloads, this could result in
    reaching inode limits of the filesystem that /var/lib/nfs/ganesha
    is part of. The above limit can be used as a guardrail to prevent
    getting into this situation.

Server_Scope(string, default "")
    Specify the value which is common for all cluster nodes.
    For e.g., Name of the cluster or cluster-id.

Server_Owner(string, default "")
    Connections to servers with the same server owner can be shared by
    the client. This is advertised to the client on EXCHANGE_ID.

Max_Open_States_Per_Client(uint32, range 0 to UINT32_MAX, default 0)
    Specify the maximum number of files that could be opened by a client. One
    misbehaving client could potentially open multiple files and exhaust the
    open FD limit allowed by ganesha's cgroup. Beyond this limit, client gets
    denied if it tries to open too many files. To disable set to ZERO.

Expired_Client_Threshold(uint32, range 0 to 256, default 16)
    Specify the threshold of number of expired clients to be kept in memory
    post lease period, unless the number of unresponsive clients go over this
    limit. Ganesha keeps track of all expired clients in LRU fashion and picks
    the oldest expired client when the number of clients exceeds the max limit.
    This allows Ganesha to retain the open & lock state and there by helping
    certain client workloads like MLPerf to run smoothly,
    even after a network partition.

Max_Open_Files_For_Expired_Client(uint32, range 0 to UINT32_MAX, default 4000)
    Specify the maximum number of open files that an unresponsive client could
    have, beyond which Ganesha won't keep client intact in memory and expire it.
    Comes to play if the config Expired_Client_Threshold is not set to ZERO.

Max_Alive_Time_For_Expired_Client(uint64, range 0 to UINT64_MAX, default 86400)
    Specify the max amount of time till which to keep the unresponsive client
    in memory, beyond which Ganesha would start reaping and expire it off.
    Comes to play if the config Expired_Client_Threshold is not set to ZERO.

RADOS_KV {}
--------------------------------------------------------------------------------

ceph_conf(string, no default)
    Connection to ceph cluster, should be file path for ceph configuration.

userid(path, no default)
    User ID to ceph cluster.

namespace(string, default NULL)
    RADOS Namespace in which to store objects

pool(string, default "nfs-ganesha")
    Pool for client info.

grace_oid(string, default "grace")
    Name of the object containing the rados_cluster grace DB

nodeid(string, default result of gethostname())
    Unique node identifier within rados_cluster

RADOS_URLS {}
--------------------------------------------------------------------------------
ceph_conf(string, no default)
    Connection to ceph cluster, should be file path for ceph configuration.

userid(path, no default)
    User ID to ceph cluster.

watch_url(url, no default)
    rados:// URL to watch for notifications of config changes. When a
    notification is received, the server will issue a SIGHUP to itself.

FSAL_LIST {}
--------------------------------------------------------------------------------
name(string, no default)
    This allows listing of the FSALs that will be used. This assures that the
    config blocks for those FSALs will not result in an error if no exports
    are configured using that FSAL. This parameter takes a list of FSAL names
    and the parameter may be listed multiple times.
