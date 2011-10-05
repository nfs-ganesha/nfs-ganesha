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
#include "rpcal.h"
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

#ifdef _USE_TIRPC
static bool_t Svcauth_gss_destroy();
static bool_t Svcauth_gss_destroy_copy();
static bool_t Svcauth_gss_wrap();
static bool_t Svcauth_gss_unwrap();
#else
static bool_t Svcauth_gss_destroy(SVCAUTH *);
static bool_t Svcauth_gss_destroy_copy(SVCAUTH *);
static bool_t Svcauth_gss_wrap(SVCAUTH *, XDR *, xdrproc_t, caddr_t);
static bool_t Svcauth_gss_unwrap(SVCAUTH *, XDR *, xdrproc_t, caddr_t);
#endif

static bool_t Svcauth_gss_nextverf(struct svc_req *, u_int);

struct svc_auth_ops Svc_auth_gss_ops = {
  Svcauth_gss_wrap,
  Svcauth_gss_unwrap,
  Svcauth_gss_destroy
};

struct svc_auth_ops Svc_auth_gss_copy_ops = {
  Svcauth_gss_wrap,
  Svcauth_gss_unwrap,
  Svcauth_gss_destroy_copy
};

const char *str_gc_proc(rpc_gss_proc_t gc_proc)
{
  switch(gc_proc)
   {
     case RPCSEC_GSS_DATA: return "RPCSEC_GSS_DATA";
     case RPCSEC_GSS_INIT: return "RPCSEC_GSS_INIT";
     case RPCSEC_GSS_CONTINUE_INIT: return "RPCSEC_GSS_CONTINUE_INIT";
     case RPCSEC_GSS_DESTROY: return "RPCSEC_GSS_DESTROY";
   }

 return "unknown";
}

/**
 *
 * sperror_gss: converts GSSAPI status to a string.
 * 
 * @param outmsg    [OUT] output string 
 * @param tag       [IN]  input tag
 * @param maj_stat  [IN]  GSSAPI major status
 * @param min_stat  [IN]  GSSAPI minor status
 *
 * @return TRUE is successfull, false otherwise.
 * 
 */
void log_sperror_gss(char *outmsg, OM_uint32 maj_stat, OM_uint32 min_stat)
{
  OM_uint32 smin;
  gss_buffer_desc msg;
  gss_buffer_desc msg2;
  int msg_ctx = 0;

  if(gss_display_status(&smin,
                        maj_stat,
                        GSS_C_GSS_CODE, GSS_C_NULL_OID, &msg_ctx, &msg) != GSS_S_COMPLETE)
    {
      sprintf(outmsg, "untranslatable error");
      return;
    }

  if(gss_display_status(&smin,
                        min_stat,
                        GSS_C_MECH_CODE,
                        GSS_C_NULL_OID, &msg_ctx, &msg2) != GSS_S_COMPLETE)
    {
      gss_release_buffer(&smin, &msg);
      sprintf(outmsg, "%s : untranslatable error", (char *)msg.value);
      return;
    }

  sprintf(outmsg, "%s : %s ", (char *)msg.value, (char *)msg2.value);

  gss_release_buffer(&smin, &msg);
  gss_release_buffer(&smin, &msg2);
}                               /* log_sperror_gss */

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

  if(!svc_getargs(rqst->rq_xprt, (xdrproc_t)xdr_rpc_gss_init_args, (caddr_t) & recv_tok))
    return (FALSE);

  gr->gr_major = gss_accept_sec_context(&gr->gr_minor,
                                        &gd->ctx,
                                        svcauth_gss_creds,
                                        &recv_tok,
                                        GSS_C_NO_CHANNEL_BINDINGS,
                                        &gd->client_name,
                                        &mech, &gr->gr_token, &ret_flags, NULL, NULL);

  svc_freeargs(rqst->rq_xprt, (xdrproc_t)xdr_rpc_gss_init_args, (caddr_t) & recv_tok);

  if(gr->gr_major != GSS_S_COMPLETE && gr->gr_major != GSS_S_CONTINUE_NEEDED)
    {
      sockaddr_t addr;
      char ipstring[SOCK_NAME_MAX];
      copy_xprt_addr(&addr, rqst->rq_xprt);
      sprint_sockaddr(&addr, ipstring, sizeof(ipstring));

      LogWarn(COMPONENT_RPCSEC_GSS,
              "Bad authentication major=%u minor=%u addr=%s",
              gr->gr_major, gr->gr_minor, ipstring);
      gd->ctx = GSS_C_NO_CONTEXT;
      goto errout;
    }
  /*
   * ANDROS: krb5 mechglue returns ctx of size 8 - two pointers,
   * one to the mechanism oid, one to the internal_ctx_id
   */
  if((gr->gr_ctx.value = Mem_Alloc(sizeof(gss_union_ctx_id_desc))) == NULL)
    {
      LogCrit(COMPONENT_RPCSEC_GSS,
              "svcauth_gss_accept_context: out of memory");
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
	  LogFullDebug(COMPONENT_RPCSEC_GSS,
	               "cname.val: %s  cname.len: %d",
	               (char *)gd->cname.value, (int)gd->cname.length);
#ifdef SPKM
        }
#endif
      if(maj_stat != GSS_S_COMPLETE)
        {
        }
#ifdef HAVE_HEIMDAL
#else
      if(isFullDebug(COMPONENT_RPCSEC_GSS))
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
      LogFullDebug(COMPONENT_RPCSEC_GSS,
                   "gss_sign in sec_accept_context");
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

int sprint_ctx(char *buff, unsigned char *ctx, int len)
{
  int i;

  if(ctx == NULL)
    return sprintf(buff, "<null>");

  LogFullDebug(COMPONENT_RPCSEC_GSS,
               "sprint_ctx len=%d", len);

  if (len > 16)
    len = 16;

  for(i = 0; i < len; i++)
    sprintf(buff + i * 2, "%02x", ctx[i]);

  return len * 2;
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
  oa = &msg->rm_call.cb_cred;

  LogFullDebug(COMPONENT_RPCSEC_GSS,
               "Call to Svcauth_gss_validate --> xid=%u dir=%u rpcvers=%u prog=%u vers=%u proc=%u flavor=%u len=%u base=%p ckeck.len=%u check.val=%p",
               msg->rm_xid,
               msg->rm_direction,
               msg->rm_call.cb_rpcvers,
               msg->rm_call.cb_prog,
               msg->rm_call.cb_vers,
               msg->rm_call.cb_proc,
               oa->oa_flavor,
               oa->oa_length,
               oa->oa_base,
               msg->rm_call.cb_verf.oa_length,
               msg->rm_call.cb_verf.oa_base);

  if(oa->oa_length > MAX_AUTH_BYTES)
    {
      LogCrit(COMPONENT_RPCSEC_GSS,
              "Svcauth_gss_validate oa->oa_length (%u) > MAX_AUTH_BYTES (%u)",
              oa->oa_length, MAX_AUTH_BYTES);
      return (FALSE);
    }
  
  /* 8 XDR units from the IXDR macro calls. */
  if(sizeof(rpchdr) < (8 * BYTES_PER_XDR_UNIT +
     RNDUP(oa->oa_length)))
    {
      LogCrit(COMPONENT_RPCSEC_GSS,
              "Svcauth_gss_validate sizeof(rpchdr) (%d) < (8 * BYTES_PER_XDR_UNIT (%d) + RNDUP(oa->oa_length (%u))) (%d)",
              (int) sizeof(rpchdr),
              (int) (8 * BYTES_PER_XDR_UNIT),
              oa->oa_length,
              (int) (8 * BYTES_PER_XDR_UNIT + RNDUP(oa->oa_length)));
      return (FALSE);
    }

  buf = (int32_t *) (void *)rpchdr;
  IXDR_PUT_LONG(buf, msg->rm_xid);
  IXDR_PUT_ENUM(buf, msg->rm_direction);
  IXDR_PUT_LONG(buf, msg->rm_call.cb_rpcvers);
  IXDR_PUT_LONG(buf, msg->rm_call.cb_prog);
  IXDR_PUT_LONG(buf, msg->rm_call.cb_vers);
  IXDR_PUT_LONG(buf, msg->rm_call.cb_proc);
  IXDR_PUT_ENUM(buf, oa->oa_flavor);
  IXDR_PUT_LONG(buf, oa->oa_length);
  if(oa->oa_length)
    {
      memcpy((caddr_t) buf, oa->oa_base, oa->oa_length);
      buf += RNDUP(oa->oa_length) / sizeof(int32_t);
    }
  rpcbuf.value = rpchdr;
  rpcbuf.length = (u_char *) buf - rpchdr;

  checksum.value = msg->rm_call.cb_verf.oa_base;
  checksum.length = msg->rm_call.cb_verf.oa_length;

  if(isFullDebug(COMPONENT_RPCSEC_GSS))
    {
      char ctx_str[64];
      sprint_ctx(ctx_str, (unsigned char *)gd->ctx, sizeof(gss_union_ctx_id_desc));
      LogFullDebug(COMPONENT_RPCSEC_GSS,
                   "Svcauth_gss_validate context %s rpcbuf=%p:%u checksum=%p:$%u)",
                   ctx_str, rpcbuf.value, (unsigned int) rpcbuf.length,
                   checksum.value, (unsigned int) checksum.length);
    }

  maj_stat = gss_verify_mic(&min_stat, gd->ctx, &rpcbuf, &checksum, &qop_state);

  if(maj_stat != GSS_S_COMPLETE)
    {
      log_sperror_gss(GssError, maj_stat, min_stat);
      LogCrit(COMPONENT_RPCSEC_GSS, "Error in gss_verify_mic: %s", GssError);
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
  struct svc_rpc_gss_data *gd;
  struct rpc_gss_cred *gc;
  struct rpc_gss_init_res gr;
  int call_stat, offset;
  OM_uint32 min_stat;
  gss_union_ctx_id_desc *gss_ctx_data;
  char ctx_str[64];

  /* Used to update the hashtable entries. 
   * These should not be used for purposes other than updating
   * hashtable entries. */
  bool_t *p_established = NULL;
  u_int *p_seqlast = NULL;
  uint32_t *p_seqmask = NULL;

  /* Initialize reply. */
  LogFullDebug(COMPONENT_RPCSEC_GSS, "Gssrpc__svcauth_gss called");

  /* Allocate and set up server auth handle. */
  if(rqst->rq_xprt->xp_auth == NULL || rqst->rq_xprt->xp_auth == &Svc_auth_none)
    {
      if((auth = (SVCAUTH *)Mem_Calloc(1, sizeof(*auth))) == NULL)
        {
          LogCrit(COMPONENT_RPCSEC_GSS, "svcauth_gss: out_of_memory");
          return (AUTH_FAILED);
        }
      if((gd = (struct svc_rpc_gss_data *)Mem_Calloc(1, sizeof(*gd))) == NULL)
        {
          LogCrit(COMPONENT_RPCSEC_GSS, "svcauth_gss: out_of_memory");
          Mem_Free(auth);
          return (AUTH_FAILED);
        }
      auth->svc_ah_ops = &Svc_auth_gss_ops;
      auth->svc_ah_private = (void *)gd;
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

  if(gc->gc_ctx.length != 0)
    gss_ctx_data = (gss_union_ctx_id_desc *)(gc->gc_ctx.value);
  else
    gss_ctx_data = NULL;

  if(isFullDebug(COMPONENT_RPCSEC_GSS))
    {
      sprint_ctx(ctx_str, (char *)gc->gc_ctx.value, gc->gc_ctx.length);
      LogFullDebug(COMPONENT_RPCSEC_GSS,
                   "Gssrpc__svcauth_gss gc_proc (%u) %s context %s",
                   gc->gc_proc, str_gc_proc(gc->gc_proc), ctx_str);
    }

  /* If we do not retrieve gss data from the cache, then this important
   * variables could not possibly be meaningful. */
  gd->seqlast = 0;
  gd->seqmask = 0;
  gd->established = 0;

  /** @todo Think about restoring the correct lines */
  //if( gd->established == 0 && gc->gc_proc == RPCSEC_GSS_DATA   )
  if(gc->gc_proc == RPCSEC_GSS_DATA || gc->gc_proc == RPCSEC_GSS_DESTROY)
    {
      if(isFullDebug(COMPONENT_RPCSEC_GSS))
        {
          LogFullDebug(COMPONENT_RPCSEC_GSS,
                       "Dump context hash table");
          Gss_ctx_Hash_Print();
        }
      
      LogFullDebug(COMPONENT_RPCSEC_GSS, "Getting gss data struct from hashtable.");
      
      /* Fill in svc_rpc_gss_data from cache */
      if(!Gss_ctx_Hash_Get(gss_ctx_data,
			   gd,
			   &p_established,
			   &p_seqlast,
			   &p_seqmask))
	{
          LogCrit(COMPONENT_RPCSEC_GSS, "Could not find gss context ");
          ret_freegc(AUTH_REJECTEDCRED);
        }
      else
        {
          /* If you 'mount -o sec=krb5i' you will have gc->gc_proc > RPCSEC_GSS_SVN_NONE, but the
           * negociation will have been made as if option was -o sec=krb5, the value of sec.svc has to be updated
           * id the stored gd that we got fromn the hash */
          if(gc->gc_svc != gd->sec.svc)
            gd->sec.svc = gc->gc_svc;
        }
    }

  if(isFullDebug(COMPONENT_RPCSEC_GSS))
    {
      char ctx_str_2[64];

      sprint_ctx(ctx_str_2, (unsigned char *)gd->ctx, sizeof(gss_ctx_data));
      sprint_ctx(ctx_str, (unsigned char *)gc->gc_ctx.value, gc->gc_ctx.length);

      LogFullDebug(COMPONENT_RPCSEC_GSS,
                   "Call to Gssrpc__svcauth_gss ----> Client=%s length=%lu (GD: established=%u ctx=%s) (RQ:sock=%u) (GC: Proc=%u Svc=%u ctx=%s)",
                   (char *)gd->cname.value,
                   (long unsigned int)gd->cname.length,
                   gd->established,
                   ctx_str_2,
                   rqst->rq_xprt->XP_SOCK,
                   gc->gc_proc,
                   gc->gc_svc,
                   ctx_str);
    }

  retstat = AUTH_FAILED;

  /* Check version. */
  if(gc->gc_v != RPCSEC_GSS_VERSION)
    {
      LogDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: bad GSS version.");
      ret_freegc(AUTH_BADCRED);
    }

  /* Check RPCSEC_GSS service. */
  if(gc->gc_svc != RPCSEC_GSS_SVC_NONE &&
     gc->gc_svc != RPCSEC_GSS_SVC_INTEGRITY && gc->gc_svc != RPCSEC_GSS_SVC_PRIVACY)
    {
      LogDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: bad GSS service (krb5, krb5i, krb5p)");
      ret_freegc(AUTH_BADCRED);
    }

  /* Check sequence number. */
  if(gd->established)
    {
      /* Sequence should be less than the max sequence number */
      if(gc->gc_seq > MAXSEQ)
	{
	  LogDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: max sequence number exceeded.");
	  ret_freegc(RPCSEC_GSS_CTXPROBLEM);
	}

      /* Check the difference between the current sequence number 
       * and the last sequence number. */
      LogFullDebug(COMPONENT_RPCSEC_GSS, "seqlast: %d  seqnum: %d offset: %d seqwin: %d seqmask: %x",
		   gd->seqlast, gc->gc_seq, gd->seqlast - gc->gc_seq, gd->win, gd->seqmask);

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
	  if ((unsigned int)offset >= gd->win)
	    LogDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: the current seqnum is lower"
		     " than seqlast by %d and out of the seq window of size %d.", offset, gd->win);
	  else
	    LogDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: the current seqnum has already been used.");

          *no_dispatch = TRUE;
          ret_freegc(RPCSEC_GSS_CTXPROBLEM);
        }
      gd->seq = gc->gc_seq;
      gd->seqmask |= (1 << offset);
    }

  if(gd->established)
    {
      rqst->rq_clntname = (char *)gd->client_name;
#ifndef _USE_TIRPC
      rqst->rq_svccred = (char *)gd->ctx;
#else
      rqst->rq_svcname = (char *)gd->ctx;
#endif
    }

  /* Handle RPCSEC_GSS control procedure. */
  switch (gc->gc_proc)
    {

    case RPCSEC_GSS_INIT:
      LogFullDebug(COMPONENT_RPCSEC_GSS, "Reached RPCSEC_GSS_INIT:");
    case RPCSEC_GSS_CONTINUE_INIT:
      LogFullDebug(COMPONENT_RPCSEC_GSS, "Reached RPCSEC_GSS_CONTINUE_INIT:");
      if(rqst->rq_proc != NULLPROC)
	{
	  LogFullDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: request proc != NULL during INIT request");
	  ret_freegc(AUTH_FAILED);        /* XXX ? */
	}

      if(!Svcauth_gss_acquire_cred())
	{
	  LogFullDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: Can't acquire credentials from RPC request.");
	  ret_freegc(AUTH_FAILED);
	}

      if(!Svcauth_gss_accept_sec_context(rqst, &gr))
	{
	  LogFullDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: Can't accept the security context.");
	  ret_freegc(AUTH_REJECTEDCRED);
	}

      if(!Svcauth_gss_nextverf(rqst, htonl(gr.gr_win)))
        {
          gss_release_buffer(&min_stat, &gr.gr_token);
          Mem_Free(gr.gr_ctx.value);
	  LogFullDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: Checksum verification failed");
          ret_freegc(AUTH_FAILED);
        }
      *no_dispatch = TRUE;

      if(isFullDebug(COMPONENT_RPCSEC_GSS))
        {
          sprint_ctx(ctx_str, (unsigned char *)gr.gr_ctx.value, gr.gr_ctx.length);
          LogFullDebug(COMPONENT_RPCSEC_GSS,
                       "Call to Gssrpc__svcauth_gss ----> Client=%s length=%lu (GD: established=%u) (RQ:sock=%u) (GR: maj=%u min=%u ctx=%s)",
                       (char *)gd->cname.value,
                       (long unsigned int)gd->cname.length,
                       gd->established,
                       rqst->rq_xprt->XP_SOCK,
                       gr.gr_major,
                       gr.gr_minor,
                       ctx_str);
        }
      call_stat = svc_sendreply(rqst->rq_xprt, (xdrproc_t)xdr_rpc_gss_init_res, (caddr_t) & gr);

      gss_release_buffer(&min_stat, &gr.gr_token);
      gss_release_buffer(&min_stat, &gd->checksum);
      Mem_Free(gr.gr_ctx.value);

      if(!call_stat)
	{
	  LogFullDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: svc_sendreply failed.");
	  ret_freegc(AUTH_FAILED);
	}

      if(gr.gr_major == GSS_S_COMPLETE)
        {
          gss_union_ctx_id_desc *gss_ctx_data2 = (gss_union_ctx_id_desc *)gd->ctx;

          gd->established = TRUE;

          /* Keep the gss context in a hash, gr.gr_ctx.value is used as key */
          (void) Gss_ctx_Hash_Set(gss_ctx_data2, gd);
        }

      break;

    case RPCSEC_GSS_DATA:
      LogFullDebug(COMPONENT_RPCSEC_GSS, "Reached RPCSEC_GSS_DATA:");
      if(!Svcauth_gss_validate(rqst, gd, msg))
	{
	  LogFullDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: Couldn't validate request.");
	  ret_freegc(RPCSEC_GSS_CREDPROBLEM);
	}

      if(!Svcauth_gss_nextverf(rqst, htonl(gc->gc_seq)))
	{
	  LogFullDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: Checksum verification failed.");
	  ret_freegc(AUTH_FAILED);
	}

      /* Update a few important values in the hashtable entry */
      if ( p_established != NULL)
	*p_established = gd->established;
      if ( p_seqlast != NULL)
	*p_seqlast = gd->seqlast;
      if (p_seqmask != NULL)
	*p_seqmask = gd->seqmask;

      break;

    case RPCSEC_GSS_DESTROY:
      LogFullDebug(COMPONENT_RPCSEC_GSS, "Reached RPCSEC_GSS_DESTROY:");
      if(rqst->rq_proc != NULLPROC)
        ret_freegc(AUTH_FAILED);        /* XXX ? */

      if(!Svcauth_gss_validate(rqst, gd, msg))
        ret_freegc(RPCSEC_GSS_CREDPROBLEM);

      if(!Svcauth_gss_nextverf(rqst, htonl(gc->gc_seq)))
	{
	  LogFullDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: Checksum verification failed.");
	  ret_freegc(AUTH_FAILED);
	}

      *no_dispatch = TRUE;

      call_stat = svc_sendreply(rqst->rq_xprt, (xdrproc_t)xdr_void, (caddr_t) NULL);

      if(!Gss_ctx_Hash_Del(gss_ctx_data))
        {
          LogCrit(COMPONENT_RPCSEC_GSS,
                  "Could not delete Gss Context from hash");
        }
      else
        LogFullDebug(COMPONENT_RPCSEC_GSS, "Gss_ctx_Hash_Del OK");

      if(!Svcauth_gss_release_cred())
	{
	  LogFullDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: Failed to release credentials.");
	  ret_freegc(AUTH_FAILED);
	}

      if(rqst->rq_xprt->xp_auth)
        SVCAUTH_DESTROY(rqst->rq_xprt->xp_auth);
      rqst->rq_xprt->xp_auth = &Svc_auth_none;

      break;

    default:
      LogFullDebug(COMPONENT_RPCSEC_GSS, "BAD AUTH: Request is not INIT, INIT_CONTINUE, DATA, OR DESTROY.");
      ret_freegc(AUTH_REJECTEDCRED);
      break;
    }

  LogFullDebug(COMPONENT_RPCSEC_GSS,
               "Call to Gssrpc__svcauth_gss - OK ---> (RQ:sock=%u)",
               rqst->rq_xprt->XP_SOCK);

  retstat = AUTH_OK;
 freegc:
  if(retstat != AUTH_OK)
    LogCrit(COMPONENT_RPCSEC_GSS,
            "Call to Gssrpc__svcauth_gss - FAILED ---> (RQ:sock=%u)",
            rqst->rq_xprt->XP_SOCK);

  xdr_free((xdrproc_t)xdr_rpc_gss_cred, gc);
  return (retstat);
}

static bool_t Svcauth_gss_destroy(SVCAUTH * auth)
{
  struct svc_rpc_gss_data *gd;
  OM_uint32 min_stat;

  gd = SVCAUTH_PRIVATE(auth);

  gss_delete_sec_context(&min_stat, &gd->ctx, GSS_C_NO_BUFFER);

  gss_release_buffer(&min_stat, &gd->cname);
  gss_release_buffer(&min_stat, &gd->checksum);

  if(gd->client_name)
    gss_release_name(&min_stat, &gd->client_name);

  Mem_Free(gd);
  Mem_Free(auth);

  return (TRUE);
}

static bool_t Svcauth_gss_destroy_copy(SVCAUTH * auth)
{
  /* svc_ah_private aka gd points to the same gd as the original, so no need
   * to free or destroy.
   * Just free the auth structure (pointer to ops and pointer to gd).
   */
  Mem_Free(auth);

  return (TRUE);
}

#ifndef DONT_USE_WRAPUNWRAP
#define RPC_SLACK_SPACE 1024

bool_t
Xdr_rpc_gss_buf(XDR *xdrs, gss_buffer_t buf, u_int maxsize)
{
	bool_t xdr_stat;
	u_int tmplen;

	if (xdrs->x_op != XDR_DECODE) {
		if (buf->length > UINT_MAX)
			return FALSE;
		else
			tmplen = buf->length;
	}
	xdr_stat = xdr_bytes(xdrs, (char **)&buf->value, &tmplen, maxsize);

	if (xdr_stat && xdrs->x_op == XDR_DECODE)
		buf->length = tmplen;

	LogFullDebug(COMPONENT_RPCSEC_GSS,
	             "Xdr_rpc_gss_buf: %s %s (%p:%d)",
		  (xdrs->x_op == XDR_ENCODE) ? "encode" : "decode",
		  (xdr_stat == TRUE) ? "success" : "failure",
		  buf->value, (int)buf->length);

	return xdr_stat;
}

bool_t
Xdr_rpc_gss_wrap_data(XDR *xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr,
		      gss_ctx_id_t ctx, gss_qop_t qop,
		      rpc_gss_svc_t svc, u_int seq)
{
	gss_buffer_desc	databuf, wrapbuf;
	OM_uint32	maj_stat, min_stat;
	int		start, end, conf_state;
	bool_t		xdr_stat = FALSE;
	u_int		databuflen, maxwrapsz;

	/* Skip databody length. */
	start = XDR_GETPOS(xdrs);
	XDR_SETPOS(xdrs, start + 4);

	memset(&databuf, 0, sizeof(databuf));
	memset(&wrapbuf, 0, sizeof(wrapbuf));

	/* Marshal rpc_gss_data_t (sequence number + arguments). */
	if (!xdr_u_int(xdrs, &seq) || !(*xdr_func)(xdrs, xdr_ptr))
		return (FALSE);
	end = XDR_GETPOS(xdrs);

	/* Set databuf to marshalled rpc_gss_data_t. */
	databuflen = end - start - 4;
	XDR_SETPOS(xdrs, start + 4);
	databuf.value = XDR_INLINE(xdrs, databuflen);
	databuf.length = databuflen;

	xdr_stat = FALSE;

	if (svc == RPCSEC_GSS_SVC_INTEGRITY) {
		/* Marshal databody_integ length. */
		XDR_SETPOS(xdrs, start);
		if (!xdr_u_int(xdrs, (u_int *)&databuflen))
			return (FALSE);

		/* Checksum rpc_gss_data_t. */
		maj_stat = gss_get_mic(&min_stat, ctx, qop,
				       &databuf, &wrapbuf);
		if (maj_stat != GSS_S_COMPLETE) {
			LogFullDebug(COMPONENT_RPCSEC_GSS,"gss_get_mic failed");
			return (FALSE);
		}
		/* Marshal checksum. */
		XDR_SETPOS(xdrs, end);
		maxwrapsz = (u_int)(wrapbuf.length + RPC_SLACK_SPACE);
		xdr_stat = Xdr_rpc_gss_buf(xdrs, &wrapbuf, maxwrapsz);
		gss_release_buffer(&min_stat, &wrapbuf);
	}
	else if (svc == RPCSEC_GSS_SVC_PRIVACY) {
		/* Encrypt rpc_gss_data_t. */
		maj_stat = gss_wrap(&min_stat, ctx, TRUE, qop, &databuf,
				    &conf_state, &wrapbuf);
		if (maj_stat != GSS_S_COMPLETE) {
			LogFullDebug(COMPONENT_RPCSEC_GSS,"gss_wrap %d %d", maj_stat, min_stat);
			return (FALSE);
		}
		/* Marshal databody_priv. */
		XDR_SETPOS(xdrs, start);
		maxwrapsz = (u_int)(wrapbuf.length + RPC_SLACK_SPACE);
		xdr_stat = Xdr_rpc_gss_buf(xdrs, &wrapbuf, maxwrapsz);
		gss_release_buffer(&min_stat, &wrapbuf);
	}
	return (xdr_stat);
}

bool_t
Xdr_rpc_gss_unwrap_data(XDR *xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr,
			gss_ctx_id_t ctx, gss_qop_t qop,
			rpc_gss_svc_t svc, u_int seq)
{
	XDR		tmpxdrs;
	gss_buffer_desc	databuf, wrapbuf;
	OM_uint32	maj_stat, min_stat;
	u_int		seq_num, qop_state;
	int			conf_state;
	bool_t		xdr_stat;

	if (xdr_func == (xdrproc_t)xdr_void || xdr_ptr == NULL)
		return (TRUE);

	memset(&databuf, 0, sizeof(databuf));
	memset(&wrapbuf, 0, sizeof(wrapbuf));

	if (svc == RPCSEC_GSS_SVC_INTEGRITY) {
		/* Decode databody_integ. */
		if (!Xdr_rpc_gss_buf(xdrs, &databuf, (u_int)-1)) {
			LogFullDebug(COMPONENT_RPCSEC_GSS,"xdr decode databody_integ failed");
			return (FALSE);
		}
		/* Decode checksum. */
		if (!Xdr_rpc_gss_buf(xdrs, &wrapbuf, (u_int)-1)) {
			gss_release_buffer(&min_stat, &databuf);
			LogFullDebug(COMPONENT_RPCSEC_GSS,"xdr decode checksum failed");
			return (FALSE);
		}
		/* Verify checksum and QOP. */
		maj_stat = gss_verify_mic(&min_stat, ctx, &databuf,
					  &wrapbuf, &qop_state);
		gss_release_buffer(&min_stat, &wrapbuf);

		if (maj_stat != GSS_S_COMPLETE || qop_state != qop) {
			gss_release_buffer(&min_stat, &databuf);
			LogFullDebug(COMPONENT_RPCSEC_GSS,"gss_verify_mic %d %d", maj_stat, min_stat);
			return (FALSE);
		}
	}
	else if (svc == RPCSEC_GSS_SVC_PRIVACY) {
		/* Decode databody_priv. */
		if (!Xdr_rpc_gss_buf(xdrs, &wrapbuf, (u_int)-1)) {
			LogFullDebug(COMPONENT_RPCSEC_GSS,"xdr decode databody_priv failed");
			return (FALSE);
		}
		/* Decrypt databody. */
		maj_stat = gss_unwrap(&min_stat, ctx, &wrapbuf, &databuf,
				      &conf_state, &qop_state);

		gss_release_buffer(&min_stat, &wrapbuf);

		/* Verify encryption and QOP. */
		if (maj_stat != GSS_S_COMPLETE || qop_state != qop ||
			conf_state != TRUE) {
			gss_release_buffer(&min_stat, &databuf);
			LogFullDebug(COMPONENT_RPCSEC_GSS,"gss_unwrap %d %d", maj_stat, min_stat);
			return (FALSE);
		}
	}
	/* Decode rpc_gss_data_t (sequence number + arguments). */
	xdrmem_create(&tmpxdrs, databuf.value, databuf.length, XDR_DECODE);
	xdr_stat = (xdr_u_int(&tmpxdrs, &seq_num) &&
		    (*xdr_func)(&tmpxdrs, xdr_ptr));
	XDR_DESTROY(&tmpxdrs);
	gss_release_buffer(&min_stat, &databuf);

	/* Verify sequence number. */
	if (xdr_stat == TRUE && seq_num != seq) {
		LogFullDebug(COMPONENT_RPCSEC_GSS,"wrong sequence number in databody");
		return (FALSE);
	}
	return (xdr_stat);
}

bool_t
Xdr_rpc_gss_data(XDR *xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr,
		 gss_ctx_id_t ctx, gss_qop_t qop,
		 rpc_gss_svc_t svc, u_int seq)
{
	bool_t rc;
	switch (xdrs->x_op) {

	case XDR_ENCODE:
		rc = (Xdr_rpc_gss_wrap_data(xdrs, xdr_func, xdr_ptr,
					      ctx, qop, svc, seq));
		LogFullDebug(COMPONENT_RPCSEC_GSS,
		             "Xdr_rpc_gss_data ENCODE returns %d",
		             rc);
		return rc;
	case XDR_DECODE:
		rc = (Xdr_rpc_gss_unwrap_data(xdrs, xdr_func, xdr_ptr,
						ctx, qop,svc, seq));
		LogFullDebug(COMPONENT_RPCSEC_GSS,
		             "Xdr_rpc_gss_data DECODE returns %d",
		             rc);
		return rc;
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}
#endif

static bool_t
Svcauth_gss_wrap(SVCAUTH * auth, XDR * xdrs, xdrproc_t xdr_func, caddr_t xdr_ptr)
{
  struct svc_rpc_gss_data *gd;

  gd = SVCAUTH_PRIVATE(auth);

  if(!gd->established || gd->sec.svc == RPCSEC_GSS_SVC_NONE)
    {
      return ((*xdr_func) (xdrs, xdr_ptr));
    }
#ifndef DONT_USE_WRAPUNWRAP
  return (Xdr_rpc_gss_data(xdrs, xdr_func, xdr_ptr,
                           gd->ctx, gd->sec.qop, gd->sec.svc, gd->seq));
#else
  return (xdr_rpc_gss_data(xdrs, xdr_func, xdr_ptr,
                           gd->ctx, gd->sec.qop, gd->sec.svc, gd->seq));
#endif
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
#ifndef DONT_USE_WRAPUNWRAP
  return (Xdr_rpc_gss_data(xdrs, xdr_func, xdr_ptr,
                           gd->ctx, gd->sec.qop, gd->sec.svc, gd->seq));
#else
  return (xdr_rpc_gss_data(xdrs, xdr_func, xdr_ptr,
                           gd->ctx, gd->sec.qop, gd->sec.svc, gd->seq));
#endif
}

int copy_svc_authgss(SVCXPRT *xprt_copy, SVCXPRT *xprt_orig)
{
  if(xprt_orig->xp_auth)
    {
      if(xprt_orig->xp_auth->svc_ah_ops == &Svc_auth_gss_ops ||
         xprt_orig->xp_auth->svc_ah_ops == &Svc_auth_gss_copy_ops)
        {
          /* Copy GSS auth */
          struct svc_rpc_gss_data *gd_o, *gd_c;

          gd_o = SVCAUTH_PRIVATE(xprt_orig->xp_auth);
          xprt_copy->xp_auth = (SVCAUTH *)Mem_Alloc(sizeof(SVCAUTH));
          if(xprt_copy->xp_auth == NULL)
            return 0;
          gd_c = (struct svc_rpc_gss_data *)Mem_Alloc(sizeof(*gd_c));
          if(gd_c == NULL)
            {
              Mem_Free(xprt_copy->xp_auth);
              xprt_copy->xp_auth = NULL;
              return 0;
            }

          /* Copy everything over */
          memcpy(gd_c, gd_o, sizeof(*gd_c));

          /* Leave the original without the various pointed to things */
          gd_o->checksum.length = 0;
          gd_o->checksum.value  = NULL;
          gd_o->cname.length    = 0;
          gd_o->cname.value     = NULL;
          gd_o->client_name     = NULL;
          gd_o->ctx             = NULL;

          /* fill in xp_auth */
          xprt_copy->xp_auth->svc_ah_private = (void *)gd_c;
          xprt_copy->xp_auth->svc_ah_ops = &Svc_auth_gss_ops;
        }
      else
        {
          /* Should be Svc_auth_none */
          if(xprt_orig->xp_auth != &Svc_auth_none)
            LogFullDebug(COMPONENT_RPCSEC_GSS,
                         "copy_svc_authgss copying unknown xp_auth");
          xprt_copy->xp_auth = xprt_orig->xp_auth;
        }
    }
  else
    xprt_copy->xp_auth = NULL;
  return 1;
}

#if !defined(_NO_BUDDY_SYSTEM) && defined(_DEBUG_MEMLEAKS)
int CheckAuth(SVCAUTH *auth)
{
  int rc;

  if(auth == NULL)
    return 1;
  if(auth->svc_ah_private != &Svc_auth_none || auth->svc_ah_private)
    return 1;
  rc = BuddyCheckLabel(auth, 1, "xp_auth");
  if(!rc)
    return 0;
  rc = BuddyCheckLabel(auth->svc_ah_private, 1, "xp_auth->svc_ah_private");
  if(!rc)
    return 0;
  return 1;
}
#endif
