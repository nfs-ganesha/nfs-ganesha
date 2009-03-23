/* Taken from RFC 2203 */

/* RPCSEC_GSS control procedures */
enum rpc_gss_proc_t {
        RPCSEC_GSS_DATA = 0,
        RPCSEC_GSS_INIT = 1,
        RPCSEC_GSS_CONTINUE_INIT = 2,
        RPCSEC_GSS_DESTROY = 3
};

/* RPCSEC_GSS services */

enum rpc_gss_service_t {
    /* Note: the enumerated value for 0 is reserved. */
    rpc_gss_svc_none = 1,
    rpc_gss_svc_integrity = 2,
    rpc_gss_svc_privacy = 3
};

/* Credential */

/*
 * Note: version 0 is reserved for possible future
 * definition of a version negotiation protocol
 *
 */
const RPCSEC_GSS_VERS_1 = 1;

/* (Fred) Unamed union inside struct did not meet XDR spec, so I removed
 * outer struct and typedef the struct in the case.  Also changed
 * 'version' to 'vers' to avoid conflict with RPC reserved word.
 */

struct rpc_gss_cred_vers_1_t {
    rpc_gss_proc_t gss_proc;  /* control procedure */
    unsigned int seq_num;   /* sequence number */
    rpc_gss_service_t service; /* service used */
    opaque handle<>;       /* context handle */
};

union rpc_gss_cred_t switch (unsigned int vers) { /* version of RPCSEC_GSS */
    case RPCSEC_GSS_VERS_1:
        rpc_gss_cred_vers_1_t rpc_gss_cred_vers_1_t;
};

/* Maximum sequence number value */
const MAXSEQ = 0x80000000;

struct rpc_gss_init_arg {
    opaque gss_token<>;
};

struct rpc_gss_init_res {
        opaque handle<>;
        unsigned int gss_major;
        unsigned int gss_minor;
        unsigned int seq_window;
        opaque gss_token<>;
};


struct rpc_gss_integ_data {
    opaque databody_integ<>;
    opaque checksum<>;
};

struct rpc_gss_data_t {
    unsigned int seq_num;
    proc_req_arg_t arg;
};


struct rpc_gss_priv_data {
    opaque databody_priv<>;
};

/* (Fred) I added this */
enum gss_major_codes {
      GSS_S_COMPLETE                = 0x00000000,
      GSS_S_CONTINUE_NEEDED         = 0x00000001,
      GSS_S_DUPLICATE_TOKEN         = 0x00000002,
      GSS_S_OLD_TOKEN               = 0x00000004,
      GSS_S_UNSEQ_TOKEN             = 0x00000008,
      GSS_S_GAP_TOKEN               = 0x00000010,
      GSS_S_BAD_MECH                = 0x00010000,
      GSS_S_BAD_NAME                = 0x00020000,
      GSS_S_BAD_NAMETYPE            = 0x00030000,
      GSS_S_BAD_BINDINGS            = 0x00040000,
      GSS_S_BAD_STATUS              = 0x00050000,
      GSS_S_BAD_MIC                 = 0x00060000,
      GSS_S_BAD_SIG                 = 0x00060000,
      GSS_S_NO_CRED                 = 0x00070000,
      GSS_S_NO_CONTEXT              = 0x00080000,
      GSS_S_DEFECTIVE_TOKEN         = 0x00090000,
      GSS_S_DEFECTIVE_CREDENTIAL    = 0x000a0000,
      GSS_S_CREDENTIALS_EXPIRED     = 0x000b0000,
      GSS_S_CONTEXT_EXPIRED         = 0x000c0000,
      GSS_S_FAILURE                 = 0x000d0000,
      GSS_S_BAD_QOP                 = 0x000e0000,
      GSS_S_UNAUTHORIZED            = 0x000f0000,
      GSS_S_UNAVAILABLE             = 0x00100000,
      GSS_S_DUPLICATE_ELEMENT       = 0x00110000,
      GSS_S_NAME_NOT_MN             = 0x00120000,
      GSS_S_CALL_INACCESSIBLE_READ  = 0x01000000,
      GSS_S_CALL_INACCESSIBLE_WRITE = 0x02000000,
      GSS_S_CALL_BAD_STRUCTURE      = 0x03000000
};
