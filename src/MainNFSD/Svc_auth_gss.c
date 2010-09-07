/*
  Copyright (c) 2000 The Regents of the University of Michigan.
  All rights reserved.

  Copyright (c) 2000 Dug Song <dugsong@UMICH.EDU>.
  All rights reserved, all wrongs reversed.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
  3. Neither the name of the University nor the names of its
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Id: svc_auth_gss.c,v 1.28 2002/10/15 21:29:36 kwc Exp
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/svc_auth.h>
#include <gssrpc/auth_gssapi.h>
#ifdef HAVE_HEIMDAL
#include <gssapi.h>
#define gss_nt_service_name GSS_C_NT_HOSTBASED_SERVICE
#else
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_generic.h>
#endif

#include "stuff_alloc.h"
#include "nfs_core.h"
#include "log_macros.h"

#ifdef SPKM

#ifndef OID_EQ
#define g_OID_equal(o1,o2) \
   (((o1)->length == (o2)->length) && \
    ((o1)->elements != 0) && ((o2)->elements != 0) && \
    (memcmp((o1)->elements,(o2)->elements,(int) (o1)->length) == 0))
#define OID_EQ 1
#endif                          /* OID_EQ */

extern const gss_OID_desc *const gss_mech_spkm3;

#endif                          /* SPKM */

#ifndef SVCAUTH_DESTROY
#define SVCAUTH_DESTROY(auth) \
     ((*((auth)->svc_ah_ops->svc_ah_destroy))(auth))
#endif

extern SVCAUTH Svc_auth_none;

/*
 * from mit-krb5-1.2.1 mechglue/mglueP.h:
 * Array of context IDs typed by mechanism OID
 */
typedef struct gss_union_ctx_id_t
{
  gss_OID mech_type;
  gss_ctx_id_t internal_ctx_id;
} gss_union_ctx_id_desc, *gss_union_ctx_id_t;

static auth_gssapi_log_badauth_func log_badauth = NULL;
static caddr_t log_badauth_data = NULL;
static auth_gssapi_log_badverf_func log_badverf = NULL;
static caddr_t log_badverf_data = NULL;
static auth_gssapi_log_miscerr_func log_miscerr = NULL;
static caddr_t log_miscerr_data = NULL;

#define LOG_MISCERR(arg) if (log_miscerr) \
        (*log_miscerr)(rqst, msg, arg, log_miscerr_data)

static bool_t Svcauth_gss_destroy(SVCAUTH *);
static bool_t Svcauth_gss_wrap(SVCAUTH *, XDR *, xdrproc_t, caddr_t);
static bool_t Svcauth_gss_unwrap(SVCAUTH *, XDR *, xdrproc_t, caddr_t);

static bool_t Svcauth_gss_nextverf(struct svc_req *, u_int);

struct svc_auth_ops Svc_auth_gss_ops = {
  Svcauth_gss_wrap,
  Svcauth_gss_unwrap,
  Svcauth_gss_destroy
};

struct svc_rpc_gss_data
{
  bool_t established;           /* context established */
  gss_ctx_id_t ctx;             /* context id */
  struct rpc_gss_sec sec;       /* security triple */
  gss_buffer_desc cname;        /* GSS client name */
  u_int seq;                    /* sequence number */
  u_int win;                    /* sequence window */
  u_int seqlast;                /* last sequence number */
  uint32_t seqmask;             /* bitmask of seqnums */
  gss_name_t client_name;       /* unparsed name string */
  gss_buffer_desc checksum;     /* so we can free it */
};

#define SVCAUTH_PRIVATE(auth) \
	(*(struct svc_rpc_gss_data **)&(auth)->svc_ah_private)

/** @todo: BUGAZOMEU: To be put in a cleaner header file later */
int Gss_ctx_Hash_Set(gss_union_ctx_id_desc * pgss_ctx, struct svc_rpc_gss_data *gd);
int Gss_ctx_Hash_Get(gss_union_ctx_id_desc * pgss_ctx, struct svc_rpc_gss_data *gd);
int Gss_ctx_Hash_Init(nfs_krb5_parameter_t param);
int Gss_ctx_Hash_Del(gss_union_ctx_id_desc * pgss_ctx);
void Gss_ctx_Hash_Print(void);
int Gss_ctx_Hash_Get_Pointer(gss_union_ctx_id_desc * pgss_ctx,
                             struct svc_rpc_gss_data **pgd);

/* Global server credentials. */
gss_cred_id_t svcauth_gss_creds;
static gss_name_t svcauth_gss_name = NULL;

bool_t Svcauth_gss_set_svc_name(gss_name_t name)
{
  OM_uint32 maj_stat, min_stat;

  if(svcauth_gss_name != NULL)
    {
      maj_stat = gss_release_name(&min_stat, &svcauth_gss_name);

      if(maj_stat != GSS_S_COMPLETE)
        {
          return (FALSE);
        }
      svcauth_gss_name = NULL;
    }
  if(svcauth_gss_name == GSS_C_NO_NAME)
    return (TRUE);

  maj_stat = gss_duplicate_name(&min_stat, name, &svcauth_gss_name);

  if(maj_stat != GSS_S_COMPLETE)
    {
      return (FALSE);
    }

  return (TRUE);
}

bool_t Svcauth_gss_import_name(char *service)
{
  gss_name_t name;
  gss_buffer_desc namebuf;
  OM_uint32 maj_stat, min_stat;

  namebuf.value = service;
  namebuf.length = strlen(service);

  maj_stat = gss_import_name(&min_stat, &namebuf, (gss_OID) gss_nt_service_name, &name);

  if(maj_stat != GSS_S_COMPLETE)
    {
      return (FALSE);
    }
  if(Svcauth_gss_set_svc_name(name) != TRUE)
    {
      gss_release_name(&min_stat, &name);
      return (FALSE);
    }
  return (TRUE);
}

bool_t Svcauth_gss_acquire_cred(void)
{
  OM_uint32 maj_stat, min_stat;

  maj_stat = gss_acquire_cred(&min_stat, svcauth_gss_name, 0,
                              GSS_C_NULL_OID_SET, GSS_C_ACCEPT,
                              &svcauth_gss_creds, NULL, NULL);

  if(maj_stat != GSS_S_COMPLETE)
    {
      return (FALSE);
    }
  return (TRUE);
}

static bool_t Svcauth_gss_release_cred(void)
{
  OM_uint32 maj_stat, min_stat;

  maj_stat = gss_release_cred(&min_stat, &svcauth_gss_creds);

  if(maj_stat != GSS_S_COMPLETE)
    {
      return (FALSE);
    }

  svcauth_gss_creds = NULL;

  return (TRUE);
}

static bool_t
Svcauth_gss_accept_sec_context(struct svc_req *rqst, struct rpc_gss_init_res *gr)
{
  struct svc_rpc_gss_data *gd;
  struct rpc_gss_cred *gc;
  gss_buffer_desc recv_tok, seqbuf;
  gss_OID mech;
  OM_uint32 maj_stat = 0, min_stat = 0, ret_flags, seq;

  gd = SVCAUTH_PRIVATE(rqst->rq_xprt->xp_auth);
  gc = (struct rpc_gss_cred *)rqst->rq_clntcred;
  memset(gr, 0, sizeof(*gr));

  /* Deserialize arguments. */
  memset(&recv_tok, 0, sizeof(recv_tok));

  if(!svc_getargs(rqst->rq_xprt, xdr_rpc_gss_init_args, (caddr_t) & recv_tok))
    return (FALSE);

  gr->gr_major = gss_accept_sec_context(&gr->gr_minor,
                                        &gd->ctx,
                                        svcauth_gss_creds,
                                        &recv_tok,
                                        GSS_C_NO_CHANNEL_BINDINGS,
                                        &gd->client_name,
                                        &mech, &gr->gr_token, &ret_flags, NULL, NULL);

  svc_freeargs(rqst->rq_xprt, xdr_rpc_gss_init_args, (caddr_t) & recv_tok);

  if(gr->gr_major != GSS_S_COMPLETE && gr->gr_major != GSS_S_CONTINUE_NEEDED)
    {
      if(log_badauth != NULL)
        {
          (*log_badauth) (gr->gr_major,
                          gr->gr_minor, &rqst->rq_xprt->xp_raddr, log_badauth_data);
        }
      gd->ctx = GSS_C_NO_CONTEXT;
      goto errout;
    }
  /*
   * ANDROS: krb5 mechglue returns ctx of size 8 - two pointers, 
   * one to the mechanism oid, one to the internal_ctx_id
   */
  if((gr->gr_ctx.value = mem_alloc(sizeof(gss_union_ctx_id_desc))) == NULL)
    {
      LogCrit(COMPONENT_RPCSEC_GSS, "svcauth_gss_accept_context: out of memory");
      goto errout;
    }
  memcpy(gr->gr_ctx.value, gd->ctx, sizeof(gss_union_ctx_id_desc));
  gr->gr_ctx.length = sizeof(gss_union_ctx_id_desc);

  /* gr->gr_win = 0x00000005; ANDROS: for debugging linux kernel version...  */
  gr->gr_win = sizeof(gd->seqmask) * 8;

  /* Save client info. */
  gd->sec.mech = mech;
  gd->sec.qop = GSS_C_QOP_DEFAULT;
  gd->sec.svc = gc->gc_svc;
  gd->seq = gc->gc_seq;
  gd->win = gr->gr_win;

  if(gr->gr_major == GSS_S_COMPLETE)
    {
#ifdef SPKM
      /* spkm3: no src_name (anonymous) */
      if(!g_OID_equal(gss_mech_spkm3, mech))
        {
#endif
          maj_stat = gss_display_name(&min_stat, gd->client_name,
                                      &gd->cname, &gd->sec.mech);
#ifdef SPKM
        }
#endif
      if(maj_stat != GSS_S_COMPLETE)
        {
        }
#ifdef HAVE_HEIMDAL
#else
      if(isFulleDebug(COMPONENT_RPCSEC_GSS))
        {
          gss_buffer_desc mechname;

          gss_oid_to_str(&min_stat, mech, &mechname);

          gss_release_buffer(&min_stat, &mechname);
        }
#endif
      seq = htonl(gr->gr_win);
      seqbuf.value = &seq;
      seqbuf.length = sizeof(seq);

      gss_release_buffer(&min_stat, &gd->checksum);
      LogFullDebug(COMPONENT_RPCSEC_GSS, "gss_sign in sec_accept_context");
      maj_stat = gss_sign(&min_stat, gd->ctx, GSS_C_QOP_DEFAULT, &seqbuf, &gd->checksum);

      if(maj_stat != GSS_S_COMPLETE)
        {
          goto errout;
        }

      rqst->rq_xprt->xp_verf.oa_flavor = RPCSEC_GSS;
      rqst->rq_xprt->xp_verf.oa_base = gd->checksum.value;
      rqst->rq_xprt->xp_verf.oa_length = gd->checksum.length;
    }
  return (TRUE);
 errout:
  gss_release_buffer(&min_stat, &gr->gr_token);
  return (FALSE);
}

static bool_t
Svcauth_gss_validate(struct svc_req *rqst, struct svc_rpc_gss_data *gd,
                     struct rpc_msg *msg)
{
  struct opaque_auth *oa;
  gss_buffer_desc rpcbuf, checksum;
  OM_uint32 maj_stat, min_stat, qop_state;
  u_char rpchdr[128];
  int32_t *buf;
  char GssError[256];

  memset(rpchdr, 0, sizeof(rpchdr));

  /* XXX - Reconstruct RPC header for signing (from xdr_callmsg). */
  buf = (int32_t *) (void *)rpchdr;
  IXDR_PUT_LONG(buf, msg->rm_xid);
  IXDR_PUT_ENUM(buf, msg->rm_direction);
  IXDR_PUT_LONG(buf, msg->rm_call.cb_rpcvers);
  IXDR_PUT_LONG(buf, msg->rm_call.cb_prog);
  IXDR_PUT_LONG(buf, msg->rm_call.cb_vers);
  IXDR_PUT_LONG(buf, msg->rm_call.cb_proc);
  oa = &msg->rm_call.cb_cred;
  IXDR_PUT_ENUM(buf, oa->oa_flavor);
  IXDR_PUT_LONG(buf, oa->oa_length);
  if(oa->oa_length)
    {
      memcpy((caddr_t) buf, oa->oa_base, oa->oa_length);
      buf += RNDUP(oa->oa_length) / sizeof(int32_t);
    }
  rpcbuf.value = rpchdr;
  rpcbuf.length = (u_char *) buf - rpchdr;

  LogFullDebug(COMPONENT_RPCSEC_GSS,
            "Call to Svcauth_gss_validate --> xid=%u dir=%u rpcvers=%u prog=%u vers=%u proc=%u flavor=%u len=%u base=%p ckeck.len=%u check.val=%p",
             msg->rm_xid, msg->rm_direction, msg->rm_call.cb_rpcvers, msg->rm_call.cb_prog,
             msg->rm_call.cb_rpcvers, msg->rm_call.cb_proc, oa->oa_flavor, oa->oa_length,
             oa->oa_base, msg->rm_call.cb_verf.oa_length, msg->rm_call.cb_verf.oa_base);

  checksum.value = msg->rm_call.cb_verf.oa_base;
  checksum.length = msg->rm_call.cb_verf.oa_length;

  maj_stat = gss_verify_mic(&min_stat, gd->ctx, &rpcbuf, &checksum, &qop_state);

  if(maj_stat != GSS_S_COMPLETE)
    {
      log_sperror_gss(GssError, "gss_verify_mic", maj_stat, min_stat);
      LogCrit(COMPONENT_RPCSEC_GSS, "Error in gss_verify_mic: %s ", GssError);
      if(log_badverf != NULL)
        (*log_badverf) (gd->client_name, svcauth_gss_name, rqst, msg, log_badverf_data);
      return (FALSE);
    }
  return (TRUE);
}

static bool_t Svcauth_gss_nextverf(struct svc_req *rqst, u_int num)
{
  struct svc_rpc_gss_data *gd;
  gss_buffer_desc signbuf;
  OM_uint32 maj_stat, min_stat;

  if(rqst->rq_xprt->xp_auth == NULL)
    return (FALSE);

  gd = SVCAUTH_PRIVATE(rqst->rq_xprt->xp_auth);

  gss_release_buffer(&min_stat, &gd->checksum);

  signbuf.value = &num;
  signbuf.length = sizeof(num);

  maj_stat = gss_get_mic(&min_stat, gd->ctx, gd->sec.qop, &signbuf, &gd->checksum);

  if(maj_stat != GSS_S_COMPLETE)
    {
      return (FALSE);
    }
  rqst->rq_xprt->xp_verf.oa_flavor = RPCSEC_GSS;
  rqst->rq_xprt->xp_verf.oa_base = (caddr_t) gd->checksum.value;
  rqst->rq_xprt->xp_verf.oa_length = (u_int) gd->checksum.length;

  return (TRUE);
}

#define ret_freegc(code) do { retstat = code; goto freegc; } while (0)

enum auth_stat
Gssrpc__svcauth_gss(struct svc_req *rqst, struct rpc_msg *msg, bool_t * no_dispatch)
{
  enum auth_stat retstat;
  XDR xdrs;
  SVCAUTH *auth;
  struct svc_rpc_gss_data *psaved_gd;
  struct svc_rpc_gss_data *gd;
  struct rpc_gss_cred *gc;
  struct rpc_gss_init_res gr;
  int call_stat, offset;
  OM_uint32 min_stat;
  gss_union_ctx_id_desc gss_ctx_data;

  /* Initialize reply. */
  /* rqst->rq_xprt->xp_verf = _null_auth ; */

  uint64_t buff64;

  /* Allocate and set up server auth handle. */
  if(rqst->rq_xprt->xp_auth == NULL || rqst->rq_xprt->xp_auth == &Svc_auth_none)
    {
      if((auth = calloc(sizeof(*auth), 1)) == NULL)
        {
          LogCrit(COMPONENT_RPCSEC_GSS, "svcauth_gss: out_of_memory");
          return (AUTH_FAILED);
        }
      if((gd = calloc(sizeof(*gd), 1)) == NULL)
        {
          LogCrit(COMPONENT_RPCSEC_GSS, "svcauth_gss: out_of_memory");
          free(auth);
          return (AUTH_FAILED);
        }
      auth->svc_ah_ops = &Svc_auth_gss_ops;
      SVCAUTH_PRIVATE(auth) = gd;
      rqst->rq_xprt->xp_auth = auth;
    }
  else
    gd = SVCAUTH_PRIVATE(rqst->rq_xprt->xp_auth);

  /* Deserialize client credentials. */
  if(rqst->rq_cred.oa_length <= 0)
    return (AUTH_BADCRED);

  gc = (struct rpc_gss_cred *)rqst->rq_clntcred;
  memset(gc, 0, sizeof(*gc));

  xdrmem_create(&xdrs, rqst->rq_cred.oa_base, rqst->rq_cred.oa_length, XDR_DECODE);

  if(!xdr_rpc_gss_cred(&xdrs, gc))
    {
      XDR_DESTROY(&xdrs);
      return (AUTH_BADCRED);
    }
  XDR_DESTROY(&xdrs);

  if(IsFulldebug(COMPONENT_RPCSEC_GSS))
    Gss_ctx_Hash_Print();

        /** @todo Think about restoring the correct lines */
  //if( gd->established == 0 && gc->gc_proc == RPCSEC_GSS_DATA   )  
  if(gc->gc_proc == RPCSEC_GSS_DATA)
    {

      memcpy((char *)&gss_ctx_data, (char *)gc->gc_ctx.value, gc->gc_ctx.length);
      if(!Gss_ctx_Hash_Get_Pointer(&gss_ctx_data, &gd))
        {
          LogCrit(COMPONENT_RPCSEC_GSS, "RPCSEC_GSS: /!\\ ERROR could not find gss context ");
          ret_freegc(AUTH_BADCRED);
        }
      else
        {

          /* If you 'mount -o sec=krb5i' you will have gc->gc_proc > RPCSEC_GSS_SVN_NONE, but the
           * negociation will have been made as if option was -o sec=krb5, the value of sec.svc has to be updated 
           * id the stored gd that we got fromn the hash */
          if(gc->gc_svc != gd->sec.svc)
            {
              gd->sec.svc = gc->gc_svc;
            }
          //gd = psaved_gd ;
          SVCAUTH_PRIVATE(rqst->rq_xprt->xp_auth) = gd; // C'est la que les bacteries attaquent .... 
        }
    }

  if(isFullDebug(COMPONENT_FSAL))
    {
      uint64_t buff64_2;

      if(gd->ctx != NULL)
        memcpy(&buff64_2, gd->ctx, 8);
      else
        buff64_2 = 0LL;
      
      memcpy(&buff64, gc->gc_ctx.value, gc->gc_ctx.length);
      LogFullDebug(COMPONENT_RPCSEC_GSS, 
          ("Call to Gssrpc__svcauth_gss ----> Client=%s length=%u (GD: established=%u ctx=%llx) (RQ:sock=%u) (GC: Proc=%u Svc=%u ctx=%u|%llx)",
           (char *)gd->cname.value, gd->cname.length, gd->established, buff64_2,
           rqst->rq_xprt->xp_sock, gc->gc_proc, gc->gc_svc, gc->gc_ctx.length, buff64);
    }

  retstat = AUTH_FAILED;

  /* Check version. */
  if(gc->gc_v != RPCSEC_GSS_VERSION)
    ret_freegc(AUTH_BADCRED);

  /* Check RPCSEC_GSS service. */
  if(gc->gc_svc != RPCSEC_GSS_SVC_NONE &&
     gc->gc_svc != RPCSEC_GSS_SVC_INTEGRITY && gc->gc_svc != RPCSEC_GSS_SVC_PRIVACY)
    ret_freegc(AUTH_BADCRED);

  /* Check sequence number. */
  if(gd->established)
    {
      if(gc->gc_seq > MAXSEQ)
        ret_freegc(RPCSEC_GSS_CTXPROBLEM);

      if((offset = gd->seqlast - gc->gc_seq) < 0)
        {
          gd->seqlast = gc->gc_seq;
          offset = 0 - offset;
          gd->seqmask <<= offset;
          offset = 0;
        }
      else if((unsigned int)offset >= gd->win
              || (gd->seqmask & (1 << (unsigned int)offset)))
        {
          *no_dispatch = 1;
          ret_freegc(RPCSEC_GSS_CTXPROBLEM);
        }
      gd->seq = gc->gc_seq;
      gd->seqmask |= (1 << offset);
    }

  if(gd->established)
    {
      rqst->rq_clntname = (char *)gd->client_name;
      rqst->rq_svccred = (char *)gd->ctx;
    }

  /* Handle RPCSEC_GSS control procedure. */
  switch (gc->gc_proc)
    {

    case RPCSEC_GSS_INIT:
    case RPCSEC_GSS_CONTINUE_INIT:
      if(rqst->rq_proc != NULLPROC)
        ret_freegc(AUTH_FAILED);        /* XXX ? */

      if(!Svcauth_gss_acquire_cred())
        ret_freegc(AUTH_FAILED);

      if(!Svcauth_gss_accept_sec_context(rqst, &gr))
        ret_freegc(AUTH_REJECTEDCRED);

      if(!Svcauth_gss_nextverf(rqst, htonl(gr.gr_win)))
        {
          gss_release_buffer(&min_stat, &gr.gr_token);
          mem_free(gr.gr_ctx.value, sizeof(gss_union_ctx_id_desc));
          ret_freegc(AUTH_FAILED);
        }
      *no_dispatch = TRUE;

      memcpy(&buff64, gr.gr_ctx.value, gr.gr_ctx.length);
      LogFullDebug(COMPONENT_RPCSEC_GSS, 
                "Call to Gssrpc__svcauth_gss ----> Client=%s length=%u (GD: established=%u) (RQ:sock=%u) (GR: maj=%u min=%u ctx=%u|0x%llx)",
                (char *)gd->cname.value, gd->cname.length, gd->established,
                rqst->rq_xprt->xp_sock, gr.gr_major, gr.gr_minor, gr.gr_ctx.length, buff64);

      call_stat = svc_sendreply(rqst->rq_xprt, xdr_rpc_gss_init_res, (caddr_t) & gr);

      gss_release_buffer(&min_stat, &gr.gr_token);
      gss_release_buffer(&min_stat, &gd->checksum);
      mem_free(gr.gr_ctx.value, sizeof(gss_union_ctx_id_desc));
      if(!call_stat)
        ret_freegc(AUTH_FAILED);

      if(gr.gr_major == GSS_S_COMPLETE)
        {
          gd->established = TRUE;
          /* Keep the gss context in a hash, gr.gr_ctx.value is used as key */
          memcpy((char *)&gss_ctx_data, (char *)gd->ctx, sizeof(gss_ctx_data));
          if(!Gss_ctx_Hash_Set(&gss_ctx_data, gd))
            LogCrit(COMPONENT_RPCSEC_GSS, 
                    "RPCSEC_GSS: /!\\ ERROR, could not add context 0x%llx to hashtable",
                     buff64);
          else
            LogFullDebug(COMPONENT_RPCSEC_GSS, "Call to Gssrpc_svcauth_gss : gss context 0x%llx added to hash",
                      buff64);
        }

      break;

    case RPCSEC_GSS_DATA:
      if(!Svcauth_gss_validate(rqst, gd, msg))
        ret_freegc(RPCSEC_GSS_CREDPROBLEM);

      if(!Svcauth_gss_nextverf(rqst, htonl(gc->gc_seq)))
        ret_freegc(AUTH_FAILED);
      break;

    case RPCSEC_GSS_DESTROY:
      if(rqst->rq_proc != NULLPROC)
        ret_freegc(AUTH_FAILED);        /* XXX ? */

      if(!Svcauth_gss_validate(rqst, gd, msg))
        ret_freegc(RPCSEC_GSS_CREDPROBLEM);

      if(!Svcauth_gss_nextverf(rqst, htonl(gc->gc_seq)))
        ret_freegc(AUTH_FAILED);

      *no_dispatch = TRUE;

      call_stat = svc_sendreply(rqst->rq_xprt, xdr_void, (caddr_t) NULL);

      memcpy((char *)&gss_ctx_data, (char *)gc->gc_ctx.value, gc->gc_ctx.length);
      if(!Gss_ctx_Hash_Del(&gss_ctx_data))
        {
          LogCrit(COMPONENT_RPCSEC_GSS, "RPCSEC_GSS: /!\\ ERROR, could not delete Gss Context from hash");
        }
      else
        LogFullDebug(COMPONENT_RPCSEC_GSS, "Gss_ctx_Hash_Del OK");

      if(!Svcauth_gss_release_cred())
        ret_freegc(AUTH_FAILED);

      SVCAUTH_DESTROY(rqst->rq_xprt->xp_auth);
      rqst->rq_xprt->xp_auth = &Svc_auth_none;

      break;

    default:
      ret_freegc(AUTH_REJECTEDCRED);
      break;
    }

  LogFullDebug(COMPONENT_RPCSEC_GSS, "Call to Gssrpc__svcauth_gss - OK ---> (RQ:sock=%u)", rqst->rq_xprt->xp_sock);

  retstat = AUTH_OK;
 freegc:
  if(retstat != AUTH_OK)
    LogCrit(COMPONENT_RPCSEC_GSS, 
            "RPCSEC_GSS: /!\\ Call to Gssrpc__svcauth_gss - FAILED ---> (RQ:sock=%u)",
            rqst->rq_xprt->xp_sock);

  xdr_free(xdr_rpc_gss_cred, gc);
  return (retstat);
}

static bool_t Svcauth_gss_destroy(SVCAUTH * auth)
{
  struct svc_rpc_gss_data *gd;
  OM_uint32 min_stat;

  gd = SVCAUTH_PRIVATE(auth);

  gss_delete_sec_context(&min_stat, &gd->ctx, GSS_C_NO_BUFFER);
  Mem_Free(gd->cname.value);
  Mem_Free(gd->checksum.value);
  // gss_release_buffer(&min_stat, &gd->cname);
  // gss_release_buffer(&min_stat, &gd->checksum);

  if(gd->client_name)
    gss_release_name(&min_stat, &gd->client_name);

        /** @todo BUGAZOMEU fix these two lines */
  /* mem_free(gd, sizeof(*gd)); */
  /* mem_free(auth, sizeof(*auth)); */

  return (TRUE);
}

static bool_t
Svcauth_gss_wrap(SVCAUTH * auth, XDR * xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr)
{
  struct svc_rpc_gss_data *gd;

  gd = SVCAUTH_PRIVATE(auth);

  if(!gd->established || gd->sec.svc == RPCSEC_GSS_SVC_NONE)
    {
      return ((*xdr_func) (xdrs, xdr_ptr));
    }
  return (xdr_rpc_gss_data(xdrs, xdr_func, xdr_ptr,
                           gd->ctx, gd->sec.qop, gd->sec.svc, gd->seq));
}

static bool_t
Svcauth_gss_unwrap(SVCAUTH * auth, XDR * xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr)
{
  struct svc_rpc_gss_data *gd;

  gd = SVCAUTH_PRIVATE(auth);

  if(!gd->established || gd->sec.svc == RPCSEC_GSS_SVC_NONE)
    {
      return ((*xdr_func) (xdrs, xdr_ptr));
    }
  return (xdr_rpc_gss_data(xdrs, xdr_func, xdr_ptr,
                           gd->ctx, gd->sec.qop, gd->sec.svc, gd->seq));
}

char *Svcauth_gss_get_principal(SVCAUTH * auth)
{
  struct svc_rpc_gss_data *gd;
  char *pname;

  gd = SVCAUTH_PRIVATE(auth);

  if(gd->cname.length == 0)
    return (NULL);

  if((pname = malloc(gd->cname.length + 1)) == NULL)
    return (NULL);

  memcpy(pname, gd->cname.value, gd->cname.length);
  pname[gd->cname.length] = '\0';

  return (pname);
}

/*
 * Function: Svcauth_gss_set_log_badauth_func
 *
 * Purpose: sets the logging function called when an invalid RPC call
 * arrives
 *
 * See functional specifications.
 */
void Svcauth_gss_set_log_badauth_func(auth_gssapi_log_badauth_func func, caddr_t data)
{
  log_badauth = func;
  log_badauth_data = data;
}

/*
 * Function: Svcauth_gss_set_log_badverf_func
 *
 * Purpose: sets the logging function called when an invalid RPC call
 * arrives
 *
 * See functional specifications.
 */
void Svcauth_gss_set_log_badverf_func(auth_gssapi_log_badverf_func func, caddr_t data)
{
  log_badverf = func;
  log_badverf_data = data;
}

/*
 * Function: Svcauth_gss_set_log_miscerr_func
 *
 * Purpose: sets the logging function called when a miscellaneous
 * AUTH_GSSAPI error occurs
 *
 * See functional specifications.
 */
void Svcauth_gss_set_log_miscerr_func(auth_gssapi_log_miscerr_func func, caddr_t data)
{
  log_miscerr = func;
  log_miscerr_data = data;
}
