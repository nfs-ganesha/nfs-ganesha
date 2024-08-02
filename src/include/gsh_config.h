/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup config Ganesha Configuration
 *
 * Ganesha configuration is contained in a global structure that is
 * populated with defaults, then modified from a configuration file.
 * This structure informs all behaviors of the daemon.
 *
 * @{
 */

/**
 * @file nfs_core.h
 * @brief Configuration structure and defaults for NFS Ganesha
 */

#ifndef GSH_CONFIG_H
#define GSH_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#include "nfs4.h"
#include "gsh_recovery.h"
#include "gsh_rpc.h"

/**
 * @brief An enumeration of protocols in the NFS family
 */

typedef enum protos {
	P_NFS,			/*< NFS, of course. */
#ifdef _USE_NFS3
	P_MNT,			/*< Mount (for v3) */
#endif
#ifdef _USE_NLM
	P_NLM,			/*< NLM (for v3) */
#endif
#ifdef _USE_RQUOTA
	P_RQUOTA,		/*< RQUOTA (for v3) */
#endif
#ifdef USE_NFSACL3
	P_NFSACL,		/*< NFSACL (for v3) */
#endif
#ifdef RPC_VSOCK
	P_NFS_VSOCK,		/*< NFS over vmware, qemu vmci sockets */
#endif
#ifdef _USE_NFS_RDMA
	P_NFS_RDMA,		/*< NFS over RPC/RDMA */
#endif
	P_COUNT			/*< Number of protocols */
} protos;

/**
 * @defgroup config_core Structure and defaults for NFS_Core_Param
 *
 * @{
 */

/**
 * @brief Default NFS Port.
 */
#define NFS_PORT 2049

/**
 * @brief Default RQUOTA port.
 */
#define RQUOTA_PORT 875

/**
 * @brief Default NFS Over RDMA Port.
 */
#define NFS_RDMA_PORT 20049

/**
 * @brief Default value for _9p_param.nb_worker
 */
#define NB_WORKER_THREAD_DEFAULT 256

/**
 * @brief Default value for core_param.drc.recycle_hiwat
 */
#define DRC_RECYCLE_HIWAT 1024

/**
 * @brief Default value for core_param.drc.tcp.npart
 */
#define DRC_TCP_NPART 1

/**
 * @brief Default value for core_param.drc.tcp.size
 */
#define DRC_TCP_SIZE 1024

/**
 * @brief Default value for core_param.drc.tcp.cachesz
 */
#define DRC_TCP_CACHESZ 127	/* make prime */

/**
 * @brief Default value for core_param.drc.tcp.hiwat
 */
#define DRC_TCP_HIWAT 64	/* 1/2(size) */

/**
 * @brief Default value for core_param.drc.tcp.recycle_npart
 */
#define DRC_TCP_RECYCLE_NPART 7

/**
 * @brief Default value for core_param.drc.tcp.expire_s
 */
#define DRC_TCP_RECYCLE_EXPIRE_S 600	/* 10m */

/**
 * @brief Default value for core_param.drc.tcp.checksum
 */
#define DRC_TCP_CHECKSUM true

/**
 * @brief Default value for core_param.drc.udp.npart
 */
#define DRC_UDP_NPART 7

/**
 * @brief Default value for core_param.drc.udp.size
 */
#define DRC_UDP_SIZE 32768

/**
 * @brief Default value for core_param.drc.udp.cachesz
 */
#define DRC_UDP_CACHESZ 599	/* make prime */

/**
 * @brief Default value for core_param.drc.udp.hiwat
 */
#define DRC_UDP_HIWAT 16384	/* 1/2(size) */

/**
 * @brief Default value for core_param.drc.udp.checksum
 */
#define DRC_UDP_CHECKSUM true

/**
 * Default value for core_param.rpc.max_send_buffer_size
 */
#define NFS_DEFAULT_SEND_BUFFER_SIZE 1048576

/**
 * Default value for core_param.rpc.max_recv_buffer_size
 */
#define NFS_DEFAULT_RECV_BUFFER_SIZE 1048576

/**
 * @brief Default Monitoring Port.
 */
#define MONITORING_PORT 9587

/**
 * @brief Turn off all protocols
 */

#define CORE_OPTION_NONE 0x00000000	/*< No operations are supported */

/**
 * @brief Support NFSv3
 */

#define CORE_OPTION_NFSV3 0x00000001	/*< NFSv3 operations are supported */

/**
 * @brief Support NFSv4
 */
#define CORE_OPTION_NFSV4 0x00000002	/*< NFSv4 operations are supported */

/**
 * @brief Support 9p
 */
#define CORE_OPTION_9P 0x00000004	/*< 9P operations are supported */

/**
 * @brief NFS AF_VSOCK
 */
#define CORE_OPTION_NFS_VSOCK 0x00000008 /*< AF_VSOCK NFS listener */

/**
 * @brief Support RPC/RDMA v1
 */
#define CORE_OPTION_NFS_RDMA 0x00000010 /*< RPC/RDMA v1 NFS listener */

/**
 * @brief Support NFSv3 and NFSv4.
 */
#ifdef _USE_NFS3
#define CORE_OPTION_ALL_NFS_VERS (CORE_OPTION_NFSV3 | CORE_OPTION_NFSV4)
#else
#define CORE_OPTION_ALL_NFS_VERS CORE_OPTION_NFSV4
#endif

#define UDP_LISTENER_NONE	0
#define UDP_LISTENER_ALL	0x00000001
#define UDP_LISTENER_MOUNT	0x00000002
#define UDP_LISTENER_MASK (UDP_LISTENER_ALL | UDP_LISTENER_MOUNT)

#ifdef _USE_NFS_RDMA
#define NFS_RDMA_ENABLE_FOR_NONE	0
#define NFS_RDMA_ENABLE_FOR_NFSV3	0x00000001
/* Validations for V4.x happens based on below values being 2 << x */
#define NFS_RDMA_ENABLE_FOR_NFSV40	0x00000002
#define NFS_RDMA_ENABLE_FOR_NFSV41	0x00000004
#define NFS_RDMA_ENABLE_FOR_NFSV42	0x00000008

#define NFS_RDMA_ENABLE_FOR_ALL		(NFS_RDMA_ENABLE_FOR_NFSV3 | \
					 NFS_RDMA_ENABLE_FOR_NFSV40)
#define NFS_RDMA_ENABLE_BY_DEFAULT	NFS_RDMA_ENABLE_FOR_NFSV40
#endif

typedef struct nfs_core_param {
	/** The list of hosts allowed to use the HAProxy protocol. These are
	 *  the hosts running HAProxy, acting as load balancing/proxy. Actual
	 *  end clients are handled in EXPORT CLIENT lists.
	 */
	struct glist_head haproxy_hosts;
	/** The IPv4 or IPv6 address to which to bind for our
	    listening port.  Set by the Bind_Addr option.
	    Must be 8-byte aligned (see sockaddr_t). */
	sockaddr_t bind_addr;
	/** An array of port numbers, one for each protocol.  Set by
	    the NFS_Port, MNT_Port, NLM_Port, and Rquota_Port options. */
	uint16_t port[P_COUNT];
	/** An array of RPC program numbers.  The correct values, by
	    default, they may be set to incorrect values with the
	    NFS_Program, MNT_Program, NLM_Program, and
	    Rquota_Program.  It is debatable whether this is a
	    worthwhile option to have. */
	uint32_t program[P_COUNT];
	/** For NFSv3, whether to drop rather than reply to requests
	    yielding I/O errors.  True by default and settable with
	    Drop_IO_Errors.  As this generally results in client
	    retry, this seems like a dubious idea. */
	bool drop_io_errors;
	/** For NFSv3, whether to drop rather than reply to requests
	    yielding invalid argument errors.  False by default and
	    settable with Drop_Inval_Errors.  As this generally
	    results in client retry, this seems like a really awful
	    idea. */
	bool drop_inval_errors;
	/** For NFSv3, whether to drop rather than reply to requests
	    yielding delay errors.  True by default and settable with
	    Drop_Delay_Errors.  As this generally results in client
	    retry and there is no NFSERR_DELAY, this seems like an
	    excellent idea. */
	bool drop_delay_errors;
	/** Parameters controlling the Duplicate Request Cache.  */
	struct {
		/** Whether to disable the DRC entirely.  Defaults to
		    false, settable by DRC_Disabled. */
		bool disabled;
		/** High water mark for len of recycle queue for DRCs */
		uint32_t recycle_hiwat;
		/* Parameters controlling TCP specific DRC behavior. */
		struct {
			/** Number of partitions in the tree for the
			    TCP DRC.  Defaults to DRC_TCP_NPART,
			    settable by DRC_TCP_Npart. */
			uint32_t npart;
			/** Maximum number of requests in a transport's
			    DRC.  Defaults to DRC_TCP_SIZE and
			    settable by DRC_TCP_Size. */
			uint32_t size;
			/** Number of entries in the O(1) front-end
			    cache to a TCP Duplicate Request
			    Cache.  Defaults to DRC_TCP_CACHESZ and
			    settable by DRC_TCP_Cachesz. */
			uint32_t cachesz;
			/** High water mark for a TCP connection's
			    DRC at which to start retiring entries if
			    we can.  Defaults to DRC_TCP_HIWAT and
			    settable by DRC_TCP_Hiwat. */
			uint32_t hiwat;
			/** Number of partitions in the recycle
			    tree that holds per-connection DRCs so
			    they can be used on reconnection (or
			    recycled.)  Defaults to
			    DRC_TCP_RECYCLE_NPART and settable by
			    DRC_TCP_Recycle_Npart. */
			uint32_t recycle_npart;
			/** How long to wait (in seconds) before
			    freeing the DRC of a disconnected
			    client.  Defaults to
			    DRC_TCP_RECYCLE_EXPIRE_S and settable by
			    DRC_TCP_Recycle_Expire_S. */
			uint32_t recycle_expire_s;
			/** Whether to use a checksum to match
			    requests as well as the XID.  Defaults to
			    DRC_TCP_CHECKSUM and settable by
			    DRC_TCP_Checksum. */
			bool checksum;
		} tcp;
		/** Parameters controlling UDP DRC behavior. */
		struct {
			/** Number of partitions in the tree for the
			    UDP DRC.  Defaults to DRC_UDP_NPART,
			    settable by DRC_UDP_Npart. */
			uint32_t npart;
			/** Maximum number of requests in the UDP DRC.
			    Defaults to DRC_UDP_SIZE and settable by
			    DRC_UDP_Size. */
			uint32_t size;
			/** Number of entries in the O(1) front-end
			    cache to the UDP Duplicate Request
			    Cache.  Defaults to DRC_UDP_CACHESZ and
			    settable by DRC_UDP_Cachesz. */
			uint32_t cachesz;
			/** High water mark for the UDP DRC at which
			    to start retiring entries if we can.
			    Defaults to DRC_UDP_HIWAT and settable by
			    DRC_UDP_Hiwat. */
			uint32_t hiwat;
			/** Whether to use a checksum to match
			    requests as well as the XID.  Defaults to
			    DRC_UDP_CHECKSUM and settable by
			    DRC_UDP_Checksum. */
			bool checksum;
		} udp;
	} drc;
	/** Parameters affecting the relation with TIRPC.   */
	struct {
		/** Maximum number of connections for TIRPC.
		    Defaults to 1024 and settable by
		    RPC_Max_Connections. */
		uint32_t max_connections;
		/** Size of RPC send buffer.  Defaults to
		    NFS_DEFAULT_SEND_BUFFER_SIZE and is settable by
		    MaxRPCSendBufferSize.  */
		uint32_t max_send_buffer_size;
		/** Size of RPC receive buffer.  Defaults to
		    NFS_DEFAULT_RECV_BUFFER_SIZE and is settable by
		    MaxRPCRecvBufferSize. */
		uint32_t max_recv_buffer_size;
		/** Idle timeout (seconds).  Defaults to 5m */
		uint32_t idle_timeout_s;
		/** TIRPC ioq min simultaneous io threads.  Defaults to
		    2 and settable by rpc_ioq_thrdmin. */
		uint32_t ioq_thrd_min;
		/** TIRPC ioq max simultaneous io threads.  Defaults to
		    200 and settable by RPC_Ioq_ThrdMax. */
		uint32_t ioq_thrd_max;
#ifdef _USE_NFS_RDMA
		/** RDMA credits */
		uint32_t rdma_credits;
		/** Maximum number of RDMA connections for TIRPC.
		    Defaults to 64 and settable by RPC_Max_RDMA_Connections. */
		uint32_t max_rdma_connections;
#endif
		struct {
			/** Partitions in GSS ctx cache table (default 13). */
			uint32_t ctx_hash_partitions;
			/** Max GSS contexts in cache (i.e.,
			 * max GSS clients, default 16K)
			 */
			uint32_t max_ctx;
			/** Max entries to expire in one idle
			 * check (default 200)
			 */
			uint32_t max_gc;
		} gss;
	} rpc;
	/** Polling interval for blocked lock polling thread. */
	time_t blocked_lock_poller_interval;
	/** Protocols to support.  Should probably be renamed.
	    Defaults to CORE_OPTION_ALL_VERS and is settable with
	    NFS_Protocols (as a comma-separated list of 3 and 4.) */
	unsigned int core_options;
	/** Whether this Ganesha is part of a cluster of Ganeshas.
	    This is somewhat vendor-specific and should probably be
	    moved somewhere else.  Settable with Clustered. */
	bool clustered;
#ifdef _USE_NLM
	/** Whether to support the Network Lock Manager protocol.
	    Defaults to true and is settable with Enable_NLM. */
	bool enable_NLM;
	/** Whether to disable NLM_SHARE and NLM_UNSHARE */
	bool disable_NLM_SHARE;
	/** Whether to use the supplied name rather than the IP
	    address in NSM operations.  Settable with
	    NSM_Use_Caller_Name. */
	bool nsm_use_caller_name;
#endif
#ifdef _USE_RQUOTA
	/** Whether to support the Remote Quota protocol.  Defaults
	    to true and is settable with Enable_RQUOTA. */
	bool enable_RQUOTA;
#endif
#ifdef USE_NFSACL3
	/* Whether to support the POSIX ACL. Defaults to false. */
	bool enable_NFSACL;
#endif
	/** Whether to collect NFS stats.  Defaults to true. */
	bool enable_NFSSTATS;
	/** Whether to use fast stats.  Defaults to false. */
	bool enable_FASTSTATS;
	/** Whether to collect FSAL stats.  Defaults to false. */
	bool enable_FSALSTATS;
#ifdef _USE_NFS3
	/** Whether to collect NFSv3 Detailed stats.  Defaults to false. */
	bool enable_FULLV3STATS;
#endif
	/** Whether to collect NFSv4 Detailed stats.  Defaults to false. */
	bool enable_FULLV4STATS;
	/** Whether to collect Auth related stats. Defaults to false. */
	bool enable_AUTHSTATS;
	/** Whether to collect client all ops stats. Defaults to false. */
	bool enable_CLNTALLSTATS;
	/** Whether tcp sockets should use SO_KEEPALIVE */
	bool enable_tcp_keepalive;
	/** Maximum number of TCP probes before dropping the connection */
	uint32_t tcp_keepcnt;
	/** Idle time before TCP starts to send keepalive probes */
	uint32_t tcp_keepidle;
	/** Time between each keepalive probe */
	uint32_t tcp_keepintvl;
	/** Whether to use short NFS file handle to accommodate VMware
	    NFS client. Enable this if you have a VMware NFSv3 client.
	    VMware NFSv3 client has a max limit of 56 byte file handles!
	    Defaults to false. */
	bool short_file_handle;
	/** How long the server will trust information it got by
	    calling getgroups() when "Manage_Gids = TRUE" is
	    used in a export entry. */
	time_t manage_gids_expiration;
	/** Path to the directory containing server specific
	    modules.  In particular, this is where FSALs live. */
	char *ganesha_modules_loc;
	/** Frequency of dbus health heartbeat in ms. Set to 0 to disable */
	uint32_t heartbeat_freq;
	/** Whether to use device major/minor for fsid. Defaults to false. */
	bool fsid_device;
	/** How many times to attempt retry of stat while resolving POSIX
	 *  filesystems */
	uint32_t resolve_fs_retries;
	/** Delay (in mili-seconds) between stat calls when trying to resolve
	 *  POSIX filesystems */
	uint32_t resolve_fs_delay;
	/** Whether to use Pseudo (true) or Path (false) for NFS v3 and 9P
	    mounts. */
	bool mount_path_pseudo;
	/** Whether to disable UDP listeners */
	uint32_t enable_UDP;
	/** DBus name prefix. Required if one wants to run multiple ganesha
	    instances on single host. The prefix should be different for every
	    ganesha instance. If this is set, dbus name will be
	    <prefix>.org.ganesha.nfsd */
	char *dbus_name_prefix;
	/** Max parallel queries to Directory Server when Manage_Gids=True.
	    Required if one does not want to overwhelm the directory server.
	    The value limits the number of concurrent uid2grp requests.
	    Useful when dealing with a slow Directory Service provider in an
	    environment where users are part of large number of groups.
	*/
	uint32_t max_uid_to_grp_reqs;
	/** Enable v3 filehandle to be used for v4 */
	bool enable_v3_fh_for_v4;
	/** Readdir response size, default is 64M (limited by maxcount from
	*  nfs request. range 4K-64M
	*/
	uint32_t readdir_res_size;
	/** Readdir max entries count, default is 1M (limited by dircount from
	*  nfs request). range 32-1M
	*/
	uint32_t readdir_max_count;
	/** Whether to call getattrs in nfs4_complete_read and
		nfs3_complete_read.
		Defaults to true and settable by Getattrs_In_Complete_Read. */
	bool getattrs_in_complete_read;
	/** Enable malloc trim */
	bool malloc_trim;
	/** Minimum threshold value to call malloc_trim. The malloc_trim
	* will be called once memory allocation exceeds minimum value.
	* Size in MB's. Note, this setting has no effect when
	* Enable_malloc_trim is set to false.
	*/
	uint32_t malloc_trim_minthreshold;
#ifdef USE_MONITORING
	/** Monitoring port number. */
	uint16_t monitoring_port;
	/** Enable creating metrics labels on the fly based on client-ip,
	 * export name, etc.
	 * Provides more debugging information, but significantly reduces
	 * performance.
	*/
	bool enable_dynamic_metrics;
#endif
	/** if  Manage_Gids=True and group resolution fails,
	 *  then use gid data from rpc request */
	bool enable_rpc_cred_fallback;
	/** unique server id, if 0 will use start time **/
	uint32_t unique_server_id;
	/** When enabled, a client (from the same source IP address), is
	 *  allowed to be connected to a single Ganesha server at a specific
	 *  point in time.
	 *  See details in connection_manager.h
	 */
	bool enable_connection_manager;
	/** Timeout for waiting until client is fully disconnected from other
	 *  Ganesha servers.
	 */
	uint32_t connection_manager_timeout_sec;
#ifdef _USE_NFS_RDMA
	/** NFS Versions to supported for NFSoRDMA.
	    Defaults to NFS_RDMA_ENABLE_BY_DEFAULT and is settable with
	    NFS_RDMA_Protocol_Versions
	    (as a comma-separated list of 3,4.0,4.1,4.2) */
	unsigned int nfs_rdma_supported_protocol_versions;
#endif
} nfs_core_parameter_t;

/** @} */

/**
 * @defgroup config_nfsv4 Structure and defaults for NFSv4
 *
 * @{
 */

/**
 * @brief Default value for lease_lifetime
 */
#define LEASE_LIFETIME_DEFAULT 60

/**
 * @brief Default value for grace period
 */
#define GRACE_PERIOD_DEFAULT 90

/**
 * @brief Default value of domainname.
 */
#define DOMAINNAME_DEFAULT "localdomain"

/**
 * @brief Default value of idmapconf.
 */
#define IDMAPCONF_DEFAULT "/etc/idmapd.conf"

/**
 * @brief Default value of deleg_recall_retry_delay.
 */
#define DELEG_RECALL_RETRY_DELAY_DEFAULT 1

/**
 * @brief NFSv4 minor versions
 */
#define NFSV4_MINOR_VERSION_ZERO	(1 << 0)
#define NFSV4_MINOR_VERSION_ONE	(1 << 1)
#define NFSV4_MINOR_VERSION_TWO	(1 << 2)
#define NFSV4_MINOR_VERSION_ALL	(NFSV4_MINOR_VERSION_ZERO | \
					 NFSV4_MINOR_VERSION_ONE | \
					 NFSV4_MINOR_VERSION_TWO)

typedef struct nfs_version4_parameter {
	/** Whether to disable the NFSv4 grace period.  Defaults to
	    false and settable with Graceless. */
	bool graceless;
	/** The NFSv4 lease lifetime.  Defaults to
	    LEASE_LIFETIME_DEFAULT and is settable with
	    Lease_Lifetime. */
	uint32_t lease_lifetime;
	/** The NFS grace period.  Defaults to
	    GRACE_PERIOD_DEFAULT and is settable with Grace_Period. */
	uint32_t grace_period;
	/** The eir_server_scope for lock recovery. Defaults to NULL
	    and is settable with server_scope. */
	char *server_scope;
	/** The eir_server_owner. Defaults to NULL and is settable
	    with server_owner. */
	char *server_owner;
	/** This config param is deprecated. Use `domainname` defined
	    in `directory_services_param` struct. */
	char *domainname;
	/** Path to the idmap configuration file.  Defaults to
	    IDMAPCONF_DEFAULT, settable with IdMapConf */
	char *idmapconf;
	/** Full path to recovery root directory */
	char *recov_root;
	/** Name of recovery directory */
	char *recov_dir;
	/** Name of recovery old dir (for legacy recovery_fs only */
	char *recov_old_dir;
	/** Whether to use local password (PAM, on Linux) rather than
	    nfsidmap.  Defaults to false if nfsidmap support is
	    compiled in and true if it isn't.  Settable with
	    UseGetpwnam. */
	bool use_getpwnam;
	/** Whether to allow bare numeric IDs in NFSv4 owner and
	    group identifiers.  Defaults to true and is settable with
	    Allow_Numeric_Owners. */
	bool allow_numeric_owners;
	/** Whether to ONLY use bare numeric IDs in NFSv4 owner and
	    group identifiers.  Defaults to false and is settable with
	    Only_Numeric_Owners. NB., this is permissible for a server
	    implementation (RFC 5661). */
	bool only_numeric_owners;
	/** Whether to allow delegations. Defaults to false and settable
	    with Delegations */
	bool allow_delegations;
	/** Delay after which server will retry a recall in case of failures */
	uint32_t deleg_recall_retry_delay;
	/** Whether this a pNFS MDS server. Defaults to false */
	bool pnfs_mds;
	/** Whether this a pNFS DS server. Defaults to false */
	bool pnfs_ds;
	/** Recovery backend */
	enum recovery_backend recovery_backend;
	/** List of supported NFSV4 minor versions */
	unsigned int minor_versions;
	/** Number of allowed slots in the 4.1 slot table */
	uint32_t nb_slots;
	/** whether to skip utf8 validation. defaults to false and settable
	     with enforce_utf8_validation. */
	bool enforce_utf8_vld;
	/** Max number of Client IDs allowed on the system */
	uint32_t max_client_ids;
	/** Max number of files that could be opened by a client. Beyond this
	 * limit, client gets denied if it tries to open too many files.
	 */
	uint32_t max_open_states_per_client;
	/** Threshold for number of expired clients to reach,
	 *  in order to start with the actual expiration
	 */
	uint32_t expired_client_threshold;
	/** Number of open files that an unresponsive client could have,
	 * beyond which Ganesha need not keep them in memory or expire it.
	 */
	uint32_t max_open_files_for_expired_client;
	/** Max amount of time till which to keep the unresponsive client
	 * in memory, beyond which Ganesha would start reaping & expire it off.
	 */
	uint64_t max_alive_time_for_expired_client;
} nfs_version4_parameter_t;

typedef struct directory_services_param {
	/** Domain to use if we aren't using the nfsidmap. Defaults
	    to NULL and is set with DomainName. */
	char *domainname;
	/** Whether to enable idmapping. Defaults to true. */
	bool idmapping_active;
	/** Cache validity in seconds for idmapped user entries */
	time_t idmapped_user_time_validity;
	/** Cache validity in seconds for idmapped group entries */
	time_t idmapped_group_time_validity;
	/** Max number of cached idmapped users */
	uint32_t cache_users_max_count;
	/** Max number of cached idmapped groups */
	uint32_t cache_groups_max_count;
	/** Max number of cached user-groups entries */
	uint32_t cache_user_groups_max_count;
	/** Cache validity in seconds for negative entries */
	time_t negative_cache_time_validity;
	/** Max number of negative cache users (that failed idmapping) */
	uint32_t negative_cache_users_max_count;
	/** Max number of negative cache groups (that failed idmapping) */
	uint32_t negative_cache_groups_max_count;
	/** Cache reaping interval in seconds for idmapped users and groups */
	time_t cache_reaping_interval;
	/** Whether to use fully qualified names for idmapping with pw-utils.
	    Defaults to false. */
	bool pwutils_use_fully_qualified_names;
} directory_services_param_t;

/** @} */

typedef struct nfs_param {
	/** NFS Core parameters, settable in the NFS_Core_Param
	    stanza. */
	nfs_core_parameter_t core_param;
	/** NFSv4 specific parameters, settable in the NFSv4 stanza. */
	nfs_version4_parameter_t nfsv4_param;
#ifdef _HAVE_GSSAPI
	/** kerberos configuration.  Settable in the NFS_KRB5 stanza. */
	nfs_krb5_parameter_t krb5_param;
#endif				/* _HAVE_GSSAPI */
	/** Directory_services configuration, settable in the
	    DIRECTORY_SERVICES stanza. */
	directory_services_param_t directory_services_param;
} nfs_parameter_t;

extern nfs_parameter_t nfs_param;

#endif				/* GSH_CONFIG_H */

/** @} */
