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

NFS_Port (uint16, range 0 to UINT16_MAX, default 2049)
    Port number used by NFS Protocol.

MNT_Port (uint16, range 0 to UINT16_MAX, default 0)
    Port number used by MNT Protocol.

NLM_Port (uint16, range 0 to UINT16_MAX, default 0)
    Port number used by NLM Protocol.

Bind_addr(IP4 addr, default 0.0.0.0)
    The address to which to bind for our listening port.
    IPv4 only, for now.

NFS_Program(uint32, range 1 to INT32_MAX, default 100003)
    RPC program number for NFS.

MNT_Program(uint32, range 1 to INT32_MAX, default 100005)
    RPC program number for MNT.

NLM_Program(uint32, range 1 to INT32_MAX, default 100021)
    RPC program number for NLM.

Nb_Worker(uint32, range 1 to 1024*128, default 256)
    Number of worker threads.

Drop_IO_Errors(bool, default false)
    For NFSv3, whether to drop rather than reply to requests yielding I/O
    errors. It results in client retry.

Drop_Inval_Errors(bool, default false)
    For NFSv3, whether to drop rather than reply to requests yielding invalid
    argument errors.  False by default and settable with Drop_Inval_Errors.

Drop_Delay_Errors(bool, default false)
    For NFSv3, whether to drop rather than reply to requests yielding delay
    errors.  False by default and settable with Drop_Delay_Errors.

Dispatch_Max_Reqs(uint32, range 1 to 1024*128*16, default 5000)
    Total number of requests to allow into the dispatcher at once.

Dispatch_Max_Reqs_Xprt(uint32, range 1 to 2048, default 512)
    Number of requests to allow into the dispatcher from one specific transport.

Plugins_Dir(path, default "/usr/lib64/ganesha")
    Path to the directory containing server specific modules

Enable_Fast_Stats(bool, default false)
    Whether to use fast stats.

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

Decoder_Fridge_Expiration_Delay(int64, range 0 to 7200, default 600)
    How long (in seconds) to let unused decoder threads wait before exiting.

Decoder_Fridge_Block_Timeout(int64, range 0 to 7200, default 600)
    How long (in seconds) to wait for the decoder fridge to accept a task
    before erroring.

Blocked_Lock_Poller_Interval(int64, range 0 to 180, default 10)
    Polling interval for blocked lock polling thread

Protocols(enum list, values [3, 4, NFS3, NFS4, V3, V4, NFSv3, NFSv4, 9P], default [3, 4, 9P])
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

mount_path_pseudo(bool, default false)
    Whether to use Pseudo (true) or Path (false) for NFS v3 and 9P mounts.


Parameters controlling TCP DRC behavior:
----------------------------------------

DRC_Disabled(bool, default false)
    Whether to disable the DRC entirely.

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

RPC_Debug_Flags(uint32, range 0 to UINT32_MAX, default 0)
    Debug flags for TIRPC.

RPC_Max_Connections(uint32, range 1 to 10000, default 1024)
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

TCP_KEEPIDLE(UINT32, range 0 to 65535, default 0 -> use system defautls)
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


NFSv4 {}
--------------------------------------------------------------------------------


Graceless(bool, default false)
    Whether to disable the NFSv4 grace period.

Lease_Lifetime(uint32, range 0 to 120, default 60)
    The NFSv4 lease lifetime.

Grace_Period(uint32, range 0 to 180, default 90)
    The NFS grace period.

DomainName(string, default "localdomain")
    Domain to use if we aren't using the nfsidmap.

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

pnfs_mds(book, default false)
    Whether this a pNFS MDS server.

pnfs_ds(book, default false)
    Whether this a pNFS DS server.

RecoveryBackend(path, default "fs")
    Use different backend for client info:
    - fs : shared filesystem
    - rados_kv : rados key-value

RADOS_KV {}
--------------------------------------------------------------------------------

ceph_conf(string, no default)
    Connection to ceph cluster, should be file path for ceph configuration.

userid(path, no default)
    User ID to ceph cluster.

pool(string, no default)
    Pool for client info.
