#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stuff_alloc.h"
#include "HashData.h"
#include "HashTable.h"
#include "log_macros.h"
#include "config_parsing.h"
#include "nfs_core.h"
#include "log_macros.h"

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

/*
 * from mit-krb5-1.2.1 mechglue/mglueP.h:
 * Array of context IDs typed by mechanism OID
 */
typedef struct gss_union_ctx_id_t
{
  gss_OID mech_type;
  gss_ctx_id_t internal_ctx_id;
} gss_union_ctx_id_desc, *gss_union_ctx_id_t;

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
#define GSS_CNAMELEN  1024
#define GSS_CKSUM_LEN 1024

struct svc_rpc_gss_data_stored
{
  bool_t established;
  gss_buffer_desc ctx_exported;
  struct rpc_gss_sec sec;
  char cname_val[GSS_CNAMELEN];
  size_t cname_len;
  u_int seq;
  u_int win;
  u_int seqlast;
  uint32_t seqmask;
  gss_name_t client_name;
  char checksum_val[GSS_CKSUM_LEN];
  size_t checksum_len;
};

/**
 *
 *  gss_data2stored: converts a rpc_gss_data into a storable structure.
 * 
 * converts a rpc_gss_data into a storable structure.
 *
 * @param gd      [IN]  the structure to be used for authentication
 * @param pstored [OUT] the stored value
 *
 * @return 1 is operation is a success, 0 otherwise
 *
 */
static int gss_data2stored(struct svc_rpc_gss_data *gd,
                           struct svc_rpc_gss_data_stored *pstored)
{
  OM_uint32 major;
  OM_uint32 minor;

  /* Save the data with a fixed size */
  pstored->established = gd->established;
  pstored->sec = gd->sec;
  pstored->seq = gd->seq;
  pstored->win = gd->win;
  pstored->seqlast = gd->seqlast;
  pstored->seqmask = gd->seqmask;

  /* keep the gss_buffer_desc */
  memcpy(pstored->cname_val, gd->cname.value, gd->cname.length);
  pstored->cname_len = gd->cname.length;

  memcpy(pstored->checksum_val, gd->checksum.value, gd->checksum.length);
  pstored->checksum_len = gd->checksum.length;

  /* Duplicate the gss_name */
  if((major = gss_duplicate_name(&minor,
                                 gd->client_name,
                                 &pstored->client_name)) != GSS_S_COMPLETE)
    return FALSE;

  /* export the sec context */
  if((major = gss_export_sec_context(&minor,
                                     &gd->ctx, &pstored->ctx_exported)) != GSS_S_COMPLETE)
    return FALSE;

  return TRUE;
}                               /* gss_data2stored */

/**
 *
 *  gss_stored2data: converts a stored rpc_gss_data into a usable structure.
 * 
 *  Converts a stored rpc_gss_data into a usable structure.
 *
 * @param gd      [OUT] the structure to be used for authentication
 * @param pstored [IN]  the stored value
 *
 * @return 1 is operation is a success, 0 otherwise
 *
 */
static int gss_stored2data(struct svc_rpc_gss_data *gd,
                           struct svc_rpc_gss_data_stored *pstored)
{
  OM_uint32 major;
  OM_uint32 minor;

  /* Get the data with a fixed size */
  gd->established = pstored->established;
  gd->sec = pstored->sec;
  gd->seq = pstored->seq;
  gd->win = pstored->win;
  gd->seqlast = pstored->seqlast;
  gd->seqmask = pstored->seqmask;

  /* Get the gss_buffer_desc */
  if(gd->cname.value == NULL && pstored->cname_len != 0)
    {
      if((gd->cname.value = (char *)Mem_Alloc(pstored->cname_len)) == NULL)
        return FALSE;
    }
  memcpy(gd->cname.value, pstored->cname_val, pstored->cname_len);
  gd->cname.length = pstored->cname_len;

  if(gd->checksum.value == NULL && pstored->checksum_len != 0)
    {
      if((gd->checksum.value = (char *)Mem_Alloc(pstored->checksum_len)) == NULL)
        return FALSE;
    }
  memcpy(gd->checksum.value, pstored->checksum_val, pstored->checksum_len);
  gd->checksum.length = pstored->checksum_len;

  /* Duplicate the gss_name */
  if((major = gss_duplicate_name(&minor,
                                 pstored->client_name,
                                 &gd->client_name)) != GSS_S_COMPLETE)
    return FALSE;

  /* Import the sec context */
  if((major = gss_import_sec_context(&minor,
                                     &pstored->ctx_exported, &gd->ctx)) != GSS_S_COMPLETE)
    return FALSE;

  return TRUE;
}                               /* gss_stored2data */

#define SVCAUTH_PRIVATE(auth) \
	(*(struct svc_rpc_gss_data **)&(auth)->svc_ah_private)

hash_table_t *ht_gss_ctx;

/**
 *
 *  gss_ctx_hash_func: computes the hash value for the entry in GSS Ctx cache.
 * 
 * Computes the hash value for the entry in GSS Ctx cache. In fact, it just use addresse as value (identity function) modulo the size of the hash.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam     [IN] hash table parameter.
 * @param buffcleff  [IN] pointer to the hash key buffer
 *
 * @return the computed hash value.
 *
 * @see HashTable_Init
 *
 */
unsigned long gss_ctx_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef)
{
  unsigned long hash_func;
  gss_union_ctx_id_desc *pgss_ctx;

  pgss_ctx = (gss_union_ctx_id_desc *) (buffclef->pdata);

  /* The gss context is basically made of two address in memory: one for the gss mech and one for the 
   * mech's specific data for this context */
  hash_func =
      (unsigned long)pgss_ctx->mech_type + (unsigned long)pgss_ctx->internal_ctx_id;

  /* LogFullDebug(COMPONENT_HASHTABLE, "gss_ctx_hash_func : 0x%lx%lx --> %lx", 
     (unsigned long)pgss_ctx->internal_ctx_id,  (unsigned long)pgss_ctx->mech_type, hash_func ) ; */

  return hash_func % p_hparam->index_size;
}                               /*  gss_ctx_hash_func */

/**
 *
 *  gss_ctx_rbt_hash_func: computes the rbt value for the entry in GSS Ctx cache.
 * 
 * Computes the rbt value for the entry in GSS Ctx cache. In fact, it just use the address value
 * itself (which is an unsigned integer) as the rbt value.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed rbt value.
 *
 * @see HashTable_Init
 *
 */
unsigned long gss_ctx_rbt_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef)
{
  unsigned long hash_func;
  gss_union_ctx_id_desc *pgss_ctx;

  pgss_ctx = (gss_union_ctx_id_desc *) (buffclef->pdata);

  /* The gss context is basically made of two address in memory: one for the gss mech and one for the 
   * mech's specific data for this context */
  hash_func =
      (unsigned long)pgss_ctx->mech_type ^ (unsigned long)pgss_ctx->internal_ctx_id;

  /* LogFullDebug(COMPONENT_HASHTABLE, "gss_ctx_rbt_hash_func : 0x%lx%lx --> %lx", 
     (unsigned long)pgss_ctx->internal_ctx_id ,(unsigned long)pgss_ctx->mech_type, hash_func ); */

  return hash_func;
}                               /* gss_ctx_rbt_hash_func */

/**
 *
 * compare_gss_ctx: compares the gss_ctx stored in the key buffers.
 *
 * compare the gss_ctx stored in the key buffers. This function is to be used as 'compare_key' field in 
 * the hashtable storing the gss context.
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different. 
 *
 */
int compare_gss_ctx(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  gss_union_ctx_id_desc *pgss_ctx1 = (gss_union_ctx_id_desc *) (buff1->pdata);
  gss_union_ctx_id_desc *pgss_ctx2 = (gss_union_ctx_id_desc *) (buff2->pdata);

  /* Check internal_ctx_id before mech_type before mech_type will VERY often be the same */
  return ((pgss_ctx1->internal_ctx_id == pgss_ctx2->internal_ctx_id)
          && (pgss_ctx1->mech_type == pgss_ctx2->mech_type)) ? 0 : 1;
}                               /* compare_gss_ctx */

/**
 *
 * display_gss_ctx: displays the gss_ctx stored in the buffer.
 *
 * displays the gss_ctx stored in the buffer. This function is to be used as 'key_to_str' field in 
 * the hashtable storing the gss context.
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_gss_ctx(hash_buffer_t * pbuff, char *str)
{
  gss_union_ctx_id_desc *pgss_ctx;

  pgss_ctx = (gss_union_ctx_id_desc *) (pbuff->pdata);

  return sprintf(str, "0x%lx%lx", (unsigned long)pgss_ctx->internal_ctx_id,
                 (unsigned long)pgss_ctx->mech_type);
}                               /* display_gss_ctx */

/**
 *
 * display_gss_svc_data: displays the gss_svc_data stored in the buffer.
 *
 * displays the gss_svc__data stored in the buffer. This function is to be used as 'value_to_str' field in 
 * the hashtable storing the gss context.
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */

int display_gss_svc_data(hash_buffer_t * pbuff, char *str)
{
  struct svc_rpc_gss_data_stored *gd;

  gd = (struct svc_rpc_gss_data_stored *)pbuff->pdata;

  return sprintf(str,
                 "established=%u ctx=(%u) sec=(mech=%p,qop=%u,svc=%u,cred=%p,flags=%u) cname=(%u|%s) seq=%u win=%u seqlast=%u seqmask=%u",
                 gd->established, gd->ctx_exported.length, gd->sec.mech, gd->sec.qop,
                 gd->sec.svc, gd->sec.cred, gd->sec.req_flags, gd->cname_len,
                 gd->cname_val, gd->seq, gd->win, gd->seqlast, gd->seqmask);
}                               /* display_gss_svc_data */

/**
 *
 * Gss_ctx_Hash_Set
 *
 * This routine sets a Gss Ctx into the Gss Context's hashtable.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int Gss_ctx_Hash_Set(gss_union_ctx_id_desc * pgss_ctx, struct svc_rpc_gss_data *gd)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  struct svc_rpc_gss_data_stored *stored_gd;

  gss_union_ctx_id_desc gssctx;

  if((buffkey.pdata = (caddr_t) Mem_Alloc(sizeof(gss_union_ctx_id_desc))) == NULL)
    return 0;

  memcpy(buffkey.pdata, pgss_ctx, sizeof(gss_union_ctx_id_desc));
  buffkey.len = sizeof(gss_union_ctx_id_desc);

  if((buffval.pdata =
      (caddr_t) Mem_Alloc(sizeof(struct svc_rpc_gss_data_stored))) == NULL)
    return 0;

  stored_gd = (struct svc_rpc_gss_data_stored *)buffval.pdata;
  if(gss_data2stored(gd, stored_gd) == 0)
    return 0;

  memcpy(&gssctx, buffkey.pdata, buffkey.len);
  /* LogFullDebug(COMPONENT_HASHTABLE, "===> Gss_ctx_Hash_Set key=0x%lx%lx", (unsigned long)gssctx.internal_ctx_id, (unsigned long)gssctx.mech_type ); */

  if(HashTable_Test_And_Set
     (ht_gss_ctx, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* Gss_ctx_Hash_Set */

/**
 *
 * Gss_ctx_Hash_Get_Pointer
 *
 * This routine gets a Gss Ctx from the  hashtable, by pointer.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int Gss_ctx_Hash_Get_Pointer(gss_union_ctx_id_desc * pgss_ctx,
                             struct svc_rpc_gss_data **pgd)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  struct svc_rpc_gss_data_stored *stored_gd;

  buffkey.pdata = (caddr_t) pgss_ctx;
  buffkey.len = sizeof(gss_union_ctx_id_desc);

  if(HashTable_Get(ht_gss_ctx, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      return 0;
    }

  stored_gd = (struct svc_rpc_gss_data_stored *)buffval.pdata;
  if(gss_stored2data(*pgd, stored_gd) == 0)
    return 0;

  return 1;
}                               /* Gss_ctx_Hash_Get_Pointer */

/**
 *
 * Gss_ctx_Hash_Get
 *
 * This routine gets a Gss Ctx from the  hashtable.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int Gss_ctx_Hash_Get(gss_union_ctx_id_desc * pgss_ctx, struct svc_rpc_gss_data *gd)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  struct svc_rpc_gss_data_stored *stored_gd;

  buffkey.pdata = (caddr_t) pgss_ctx;
  buffkey.len = sizeof(gss_union_ctx_id_desc);

  if(HashTable_Get(ht_gss_ctx, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      return 0;
    }

  stored_gd = (struct svc_rpc_gss_data_stored *)buffval.pdata;
  if(gss_stored2data(gd, stored_gd) == 0)
    return 0;

  return 1;
}                               /* Gss_ctx_Hash_Get */

/**
 *
 * Gss_ctx_Hash_Del
 *
 * This routine removes a state from the Gss ctx hashtable.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int Gss_ctx_Hash_Del(gss_union_ctx_id_desc * pgss_ctx)
{
  hash_buffer_t buffkey, old_key, old_value;

  buffkey.pdata = (caddr_t) pgss_ctx;
  buffkey.len = sizeof(gss_union_ctx_id_desc);

  if(HashTable_Del(ht_gss_ctx, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      /* free the key that was stored in hash table */
      Mem_Free((void *)old_key.pdata);
      Mem_Free((void *)old_value.pdata);

      return 1;
    }
  else
    return 0;
}                               /* Gss_ctx_Hash_Del */

/**
 *
 * Gss_ctx_Hash_Init: Init the hashtable for GSS Ctx
 *
 * Perform all the required initialization for hashtable Gss ctx cache
 * 
 * @return 0 if successful, -1 otherwise
 *
 */
int Gss_ctx_Hash_Init(nfs_krb5_parameter_t param)
{
  if((ht_gss_ctx = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_RPCSEC_GSS, "GSS_CTX_HASH: Cannot init GSS CTX  cache");
      return -1;
    }

  return 0;
}                               /* Gss_ctx_Hash_Init */

/**
 *
 * Gss_ctx_Hash_Print: Displays the content of the hash table (for debugging)
 * 
 * Displays the content of the hash table (for debugging).
 *
 * @return nothing (void function)
 *
 */
void Gss_ctx_Hash_Print(void)
{
  HashTable_Log(COMPONENT_RPCSEC_GSS, ht_gss_ctx);
}                               /* Gss_ctx_Hash_Print */
