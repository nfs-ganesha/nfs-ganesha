/* SPDX-License-Identifier: unknown license... */

/* The maximum length of the string identifying the caller. */
const LM_MAXSTRLEN = 1024;
/* The maximum number of bytes in the nlm_notify name argument. */
const LM_MAXNAMELEN = 1025;
const MAXNETOBJ_SZ = 1024;

/* NSM related constatnts */
const SM_MAXSTRLEN = 1024;
const SM_PRIV_SZ   = 16;
/*
 * Basic typedefs for RFC 1832 data type definitions
 */
typedef int		int32_t;
typedef unsigned int	uint32_t;
typedef hyper		int64_t;
typedef unsigned hyper	uint64_t;


enum nlm4_stats {
	NLM4_GRANTED = 0,
	NLM4_DENIED = 1,
	NLM4_DENIED_NOLOCKS = 2,
	NLM4_BLOCKED = 3,
	NLM4_DENIED_GRACE_PERIOD = 4,
	NLM4_DEADLCK = 5,
	NLM4_ROFS = 6,
	NLM4_STALE_FH = 7,
	NLM4_FBIG = 8,
	NLM4_FAILED = 9
};

struct nlm4_stat {
	nlm4_stats stat;
};

struct nlm4_res {
	netobj cookie;
	nlm4_stat stat;
};

struct nlm4_holder {
	bool     exclusive;
	int32_t    svid;
	netobj   oh;
	uint64_t   l_offset;
	uint64_t   l_len;
};

union nlm4_testrply switch (nlm4_stats stat) {
case NLM4_DENIED:
	struct nlm4_holder holder; /* holder of the lock */
default: void;
};

struct nlm4_testres {
	netobj cookie;
	nlm4_testrply test_stat;
};


struct nlm4_lock {
	string   caller_name<LM_MAXSTRLEN>;
	netobj   fh;
	netobj   oh;
	int32_t    svid;
	uint64_t   l_offset;
	uint64_t   l_len;
};

struct nlm4_lockargs {
	netobj cookie;
	bool block; /* Flag to indicate blocking behaviour. */
	bool exclusive; /* If exclusive access is desired. */
	struct nlm4_lock alock; /* The actual lock data (see above) */
	bool reclaim; /* used for recovering locks */
	int32_t state; /* specify local NSM state */
};


struct nlm4_cancargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm4_lock alock;
};


struct nlm4_testargs {
	netobj cookie;
	bool exclusive;
	struct nlm4_lock alock;
};


struct nlm4_unlockargs {
	netobj cookie;
	struct nlm4_lock alock;
};

enum fsh4_mode {
	fsm_DN = 0,        /*  deny none  */
	fsm_DR = 1,        /*  deny read  */
	fsm_DW = 2,        /*  deny write  */
	fsm_DRW = 3        /*  deny read/write  */
};

enum fsh4_access {
	fsa_NONE = 0, /* for completeness */
	fsa_R = 1, /* read-only */
	fsa_W = 2, /* write-only */
	fsa_RW = 3 /* read/write */
};

struct nlm4_share {
	string      caller_name<LM_MAXSTRLEN>;
	netobj      fh;
	netobj      oh;
	fsh4_mode   mode;
	fsh4_access access;
};

struct nlm4_shareargs {
	netobj cookie;
	nlm4_share share;     /*  actual share data  */
	bool reclaim;        /*  used for recovering shares  */
};


struct nlm4_shareres {
	netobj cookie;
	nlm4_stats stat;
	int32_t sequence;
};

struct nlm4_notify {
	string name<LM_MAXNAMELEN>;
	int64_t state;
};

struct nlm4_sm_notifyargs {
	string name<SM_MAXSTRLEN>;
	int32_t state;
	opaque priv[SM_PRIV_SZ];
};

#ifdef RPC_HDR
%extern void nlm_init(void);
#endif

program NLMPROG {
	version NLM4_VERS {
		void NLMPROC4_NULL(void)				= 0;
		nlm4_testres NLMPROC4_TEST(nlm4_testargs)		= 1;
		nlm4_res NLMPROC4_LOCK(nlm4_lockargs)			= 2;
		nlm4_res NLMPROC4_CANCEL(nlm4_cancargs)			= 3;
		nlm4_res NLMPROC4_UNLOCK(nlm4_unlockargs)		= 4;
		nlm4_res NLMPROC4_GRANTED(nlm4_testargs)		= 5;
		void NLMPROC4_TEST_MSG(nlm4_testargs)			= 6;
		void NLMPROC4_LOCK_MSG(nlm4_lockargs)			= 7;
		void NLMPROC4_CANCEL_MSG(nlm4_cancargs)			= 8;
		void NLMPROC4_UNLOCK_MSG(nlm4_unlockargs)		= 9;
		void NLMPROC4_GRANTED_MSG(nlm4_testargs)		= 10;
		void NLMPROC4_TEST_RES(nlm4_testres)			= 11;
		void NLMPROC4_LOCK_RES(nlm4_res)			= 12;
		void NLMPROC4_CANCEL_RES(nlm4_res)			= 13;
		void NLMPROC4_UNLOCK_RES(nlm4_res)			= 14;
		void NLMPROC4_GRANTED_RES(nlm4_res)			= 15;
		void NLMPROC4_SM_NOTIFY(nlm4_sm_notifyargs)		= 16;
		nlm4_shareres NLMPROC4_SHARE(nlm4_shareargs)		= 20;
		nlm4_shareres NLMPROC4_UNSHARE(nlm4_shareargs)		= 21;
		nlm4_res NLMPROC4_NM_LOCK(nlm4_lockargs)		= 22;
		void NLMPROC4_FREE_ALL(nlm4_notify)			= 23;
	} = 4;
} = 100021;
