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
#include "fsal_types.h"

/**
 * @brief An enumeration of protocols in the NFS family
 */

typedef enum protos {
	P_NFS,			/*< NFS, of course. */
	P_MNT,			/*< Mount (for v3) */
	P_NLM,			/*< NLM (for v3) */
	P_RQUOTA,		/*< RQUOTA (for v3) */
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
 * @brief Default value for core_param.nb_worker
 */
#define NB_WORKER_THREAD_DEFAULT 16

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
 * @brief Default value for core_param.drc.tcp.checkstum
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
 * @brief Default value for core_param.rpc.debug_flags
 */
#define TIRPC_DEBUG_FLAGS 0x0

/**
 * Default value for core_param.rpc.max_send_buffer_size
 */
#define NFS_DEFAULT_SEND_BUFFER_SIZE 1048576

/**
 * Default value for core_param.rpc.max_recv_buffer_size
 */
#define NFS_DEFAULT_RECV_BUFFER_SIZE 1048576

/**
 * @brief Support NFSv3
 */

#define CORE_OPTION_NFSV3 0x00000001	/*< NFSv3 operations are supported */

/**
 * @brief Support NFSv4
 */
#define CORE_OPTION_NFSV4 0x00000002	/*< NFSv4 operations are supported */

/**
 * @brief Support NFSv3 and NFSv4.
 */
#define CORE_OPTION_ALL_VERS (CORE_OPTION_NFSV3 | CORE_OPTION_NFSV4)

typedef struct nfs_core_param {
	/** An array of port numbers, one for each protocol.  Set by
	    the NFS_Port, MNT_Port, NLM_Port, and Rquota_Port options. */
	uint16_t port[P_COUNT];
	/** The address to which to bind for our listening port.
	    IPv4 only, for now.  Set by the Bind_Addr option. */
	struct sockaddr_in bind_addr;
	/** An array of RPC program numbers.  The correct values, by
	    default, they may be set to incorrect values with the
	    NFS_Program, MNT_Program, NLM_Program, and
	    Rquota_Program.  It is debatable whether this is a
	    worthwhile option to have. */
	uint32_t program[P_COUNT];
	/** Number of worker threads.  Set to NB_WORKER_DEFAULT by
	    default and changed with the Nb_Worker option. */
	uint32_t nb_worker;
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
	/** Total number of requests to allow into the dispatcher at
	    once.  Defaults to 5000 and settable by Dispatch_Max_Reqs */
	uint32_t dispatch_max_reqs;
	/** Number of requests to allow into the dispatcher from one
	    specific transport.  Defaults to 512 and settable by
	    Dispatch_Max_Reqs_Xprt. */
	uint32_t dispatch_max_reqs_xprt;
	/** Parameters controlling the Duplicate Request Cache.  */
	struct {
		/** Whether to disable the DRC entirely.  Defaults to
		    false, settable by DRC_Disabled. */
		bool disabled;
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
		/** Debug flags for TIRPC.  Defaults to
		    TIRPC_DEBUG_FLAGS and settable by
		    RPC_Debug_Flags. */
		uint32_t debug_flags;
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
		/** TIRPC ioq max simultaneous io threads.  Defaults to
		    200 and settable by RPC_Ioq_ThrdMax. */
		uint32_t ioq_thrd_max;
	} rpc;
	/** How long (in seconds) to let unused decoder threads wait before
	    exiting.  Settable with Decoder_Fridge_Expiration_Delay. */
	time_t decoder_fridge_expiration_delay;
	/** How long (in seconds) to wait for the decoder fridge to
	    accept a task before erroring.  Settable with
	    Decoder_Fridge_Block_Timeout. */
	time_t decoder_fridge_block_timeout;
	/** Protocols to support.  Should probably be renamed.
	    Defaults to CORE_OPTION_ALL_VERS and is settable with
	    NFS_Protocols (as a comma-separated list of 3 and 4.) */
	unsigned int core_options;
	/** Whether to use the supplied name rather than the IP
	    address in NSM operations.  Settable with
	    NSM_Use_Caller_Name. */
	bool nsm_use_caller_name;
	/** Whether this Ganesha is part of a cluster of Ganeshas.
	    This is somewhat vendor-specific and should probably be
	    moved somewhere else.  Settable with Clustered. */
	bool clustered;
	/** Whether to support the Network Lock Manager protocol.
	    Defaults to true and is settable with Enable_NLM. */
	bool enable_NLM;
	/** Whether to support the Remote Quota protocol.  Defaults
	    to true and is settable with Enable_RQUOTA. */
	bool enable_RQUOTA;
	/** Whether to use fast stats.  Defaults to false. */
	bool enable_FASTSTATS;
	/** How long the server will trust information it got by
	    calling getgroups() when "Manage_Gids = TRUE" is
	    used in a export entry. */
	time_t manage_gids_expiration;
	/** Path to the directory containing server specific
	    modules.  In particular, this is where FSALs live. */
	char *ganesha_modules_loc;
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

typedef struct nfs_version4_parameter {
	/** Whether to disable the NFSv4 grace period.  Defaults to
	    false and settable with Graceless. */
	bool graceless;
	/** Whether to grace period handled by FSAL.  Defaults to
	    false and settable with FSAL_Grace. */
	bool fsal_grace;
	/** The NFSv4 lease lifetime.  Defaults to
	    LEASE_LIFETIME_DEFAULT and is settable with
	    Lease_Lifetime. */
	uint32_t lease_lifetime;
	/** The NFS grace period.  Defaults to
	    GRACE_PERIOD_DEFAULT and is settable with Grace_Period. */
	uint32_t grace_period;
	/** Domain to use if we aren't using the nfsidmap.  Defaults
	    to DOMAINNAME_DEFAULT and is set with DomainName. */
	char *domainname;
	/** Path to the idmap configuration file.  Defaults to
	    IDMAPCONF_DEFAULT, settable with IdMapConf */
	char *idmapconf;
	/** Whether to use local password (PAM, on Linux) rather than
	    nfsidmap.  Defaults to false if nfsidmap support is
	    compiled in and true if it isn't.  Settable with
	    UseGetpwnam. */
	bool use_getpwnam;
	/** Whether to allow bare numeric IDs in NFSv4 owner and
	    group identifiers.  Defaults to true and is settable with
	    Allow_Numeric_Owners. */
	bool allow_numeric_owners;
	/** Whether to allow delegations. Defaults to false and settable
	    with Delegations */
	bool allow_delegations;
} nfs_version4_parameter_t;

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
} nfs_parameter_t;

extern nfs_parameter_t nfs_param;

#endif				/* GSH_CONFIG_H */

/** @} */
