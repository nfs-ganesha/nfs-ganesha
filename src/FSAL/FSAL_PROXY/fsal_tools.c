/*
 * vim:set expandtab:shiftwidth=2:tabstop=8:
 */

/**
 * \file    fsal_tools.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/09 12:16:07 $
 * \version $Revision: 1.28 $
 * \brief   miscelaneous FSAL tools that can be called from outside.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <ctype.h>              /* for isalpha */
#include <netdb.h>              /* for gethostbyname */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_common.h"
#include "fsal_convert.h"
#include "config_parsing.h"
#include "fsal_common.h"
#include <string.h>

#ifdef _HANDLE_MAPPING
#include "handle_mapping/handle_mapping.h"
#endif

extern proxyfs_specific_initinfo_t global_fsal_proxy_specific_info;

/* case unsensitivity */
#define STRCMP   strcasecmp

char *PROXYFSAL_GetFSName()
{
  return "NFSv4 PROXY";
}

/** 
 * FSAL_handlecmp:
 * Compare 2 handles.
 *
 * \param handle1 (input):
 *        The first handle to be compared.
 * \param handle2 (input):
 *        The second handle to be compared.
 * \param status (output):
 *        The status of the compare operation.
 *
 * \return - 0 if handles are the same.
 *         - A non null value else.
 *         - Segfault if status is a NULL pointer.
 */

int PROXYFSAL_handlecmp(fsal_handle_t * handle_1, fsal_handle_t * handle_2,
                        fsal_status_t * status)
{
  *status = FSAL_STATUS_NO_ERROR;
  proxyfsal_handle_t * handle1 = (proxyfsal_handle_t *)handle_1;
  proxyfsal_handle_t * handle2 = (proxyfsal_handle_t *)handle_2;

  if(!handle1 || !handle2)
    {
      status->major = ERR_FSAL_FAULT;
      return -1;
    }

  /* Check if size are the same for underlying's server FH */
  if(handle1->data.srv_handle_len != handle2->data.srv_handle_len)
    return -1;

  /* At last, check underlying FH value. We use the fact that srv_handle_len is the same */
  if(memcmp(handle1->data.srv_handle_val, handle2->data.srv_handle_val, handle1->data.srv_handle_len))
    return -1;

  /* If this point is reached, then the FH are the same */
  return 0;
}                               /* FSAL_handlecmp */

/**
 * FSAL_Handle_to_HashIndex
 * This function is used for hashing a FSAL handle
 * in order to dispatch entries into the hash table array.
 *
 * \param p_handle      The handle to be hashed
 * \param cookie        Makes it possible to have different hash value for the
 *                      same handle, when cookie changes.
 * \param alphabet_len  Parameter for polynomial hashing algorithm
 * \param index_size    The range of hash value will be [0..index_size-1]
 *
 * \return The hash value
 */

unsigned int PROXYFSAL_Handle_to_HashIndex(fsal_handle_t *handle,
                                           unsigned int cookie,
                                           unsigned int alphabet_len,
                                           unsigned int index_size)
{
  unsigned int cpt = 0;
  unsigned int sum = 0;
  unsigned int extract = 0;
  unsigned int mod;
  proxyfsal_handle_t * p_handle = (proxyfsal_handle_t *)handle;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get uninitialized
   * chars after the end of the handle. We must avoid this by skipping the last loop
   * and doing a special processing for the last bytes */

  mod = p_handle->data.srv_handle_len % sizeof(unsigned int);

  sum = cookie;
  for(cpt = 0; cpt < p_handle->data.srv_handle_len - mod; cpt += sizeof(unsigned int))
    {
      memcpy(&extract, &(p_handle->data.srv_handle_val[cpt]), sizeof(unsigned int));
      sum = (3 * sum + 5 * extract + 1999) % index_size;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = p_handle->data.srv_handle_len - mod; cpt < p_handle->data.srv_handle_len; cpt++)
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)p_handle->data.srv_handle_val[cpt];
        }
      sum = (3 * sum + 5 * extract + 1999) % index_size;
    }

  return sum;
}

/*
 * FSAL_Handle_to_RBTIndex
 * This function is used for generating a RBT node ID
 * in order to identify entries into the RBT.
 *
 * \param p_handle      The handle to be hashed
 * \param cookie        Makes it possible to have different hash value for the
 *                      same handle, when cookie changes.
 *
 * \return The hash value
 */

unsigned int PROXYFSAL_Handle_to_RBTIndex(fsal_handle_t *handle,
                                          unsigned int cookie)
{
  unsigned int h = 0;
  unsigned int cpt = 0;
  unsigned int extract = 0;
  unsigned int mod;
  proxyfsal_handle_t * p_handle = (proxyfsal_handle_t *)handle;

  h = cookie;

  /* XXX If the handle is not 32 bits-aligned, the last loop will get uninitialized
   * chars after the end of the handle. We must avoid this by skipping the last loop
   * and doing a special processing for the last bytes */

  mod = p_handle->data.srv_handle_len % sizeof(unsigned int);

  for(cpt = 0; cpt < p_handle->data.srv_handle_len - mod; cpt += sizeof(unsigned int))
    {
      memcpy(&extract, &(p_handle->data.srv_handle_val[cpt]), sizeof(unsigned int));
      h = (857 * h ^ extract) % 715827883;
    }

  if(mod)
    {
      extract = 0;
      for(cpt = p_handle->data.srv_handle_len - mod; cpt < p_handle->data.srv_handle_len; cpt++)
        {
          /* shift of 1 byte */
          extract <<= 8;
          extract |= (unsigned int)p_handle->data.srv_handle_val[cpt];
        }
      h = (857 * h ^ extract) % 715827883;
    }

  return h;
}

static ssize_t proxy_sizeof_handle(const proxyfsal_handle_t *pxh)
{
  if(pxh->data.srv_handle_len > sizeof(pxh->data.srv_handle_val))
    return -1;
  return offsetof(proxyfsal_handle_t, data.srv_handle_val) + 
	 pxh->data.srv_handle_len;
}

/**
 * FSAL_DigestHandle :
 *  Convert an proxyfsal_handle_t to a buffer
 *  to be included into NFS handles,
 *  or another digest.
 *
 * \param output_type (input):
 *        Indicates the type of digest to do.
 * \param in_fsal_handle (input):
 *        The handle to be converted to digest.
 * \param out_buff (output):
 *        The buffer where the digest is to be stored.
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */
fsal_status_t PROXYFSAL_DigestHandle(fsal_export_context_t * exp_context, /* IN */
                                     fsal_digesttype_t output_type,     /* IN */
                                     fsal_handle_t * in_handle,       /* IN */
                                     struct fsal_handle_desc * fh_desc)
{
#ifdef _HANDLE_MAPPING
  nfs23_map_handle_t map_hdl;
#endif
  proxyfsal_export_context_t * p_expcontext = (proxyfsal_export_context_t *)exp_context;
  proxyfsal_handle_t * in_fsal_handle = (proxyfsal_handle_t *)in_handle;
  ssize_t sz;
  const void *data;
  uint32_t fid2;

  /* sanity checks */
  if(!in_handle || !fh_desc || !fh_desc->start || !p_expcontext)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (output_type)
    {
    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:
      if(!global_fsal_proxy_specific_info.enable_handle_mapping)
        ReturnCode(ERR_FSAL_NOTSUPP, 0);

#ifdef _HANDLE_MAPPING
      if(fh_desc->len < sizeof(map_hdl))
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

      /* returns a digest and register it to handle map
       * (use the same checksum as cache inode's RBT index)
       */
      map_hdl.object_id = in_fsal_handle->data.fileid4;
      map_hdl.handle_hash = FSAL_Handle_to_RBTIndex(in_fsal_handle, 0);

      HandleMap_SetFH(&map_hdl, in_fsal_handle);

      /* Do no set length - use as much of opaque handle as allowed,
       * it could help when converting handles back in ExpandHandle */
      memset(fh_desc->start, 0, fh_desc->len);
      memcpy(fh_desc->start, &map_hdl, sizeof(map_hdl));
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
#endif
    /* fallthru */
    case FSAL_DIGEST_NFSV4:
      sz = proxy_sizeof_handle(in_fsal_handle);
      data = in_fsal_handle;
      break;

    case FSAL_DIGEST_FILEID2:
      fid2 = in_fsal_handle->data.fileid4;
      if(fid2 != in_fsal_handle->data.fileid4)
	ReturnCode(ERR_FSAL_OVERFLOW, 0);
      data = &fid2;
      sz = sizeof(fid2);
      break;

    case FSAL_DIGEST_FILEID3:
    case FSAL_DIGEST_FILEID4:
      sz = sizeof(in_fsal_handle->data.fileid4);
      data = &(in_fsal_handle->data.fileid4);
      break;

    default:
      ReturnCode(ERR_FSAL_SERVERFAULT, 0);
    }

  if(fh_desc->len <  sz)
    {
      LogDebug(COMPONENT_FSAL, "Cannot fit %zd bytes into %zd",
	       sz, fh_desc->len);
      ReturnCode(ERR_FSAL_TOOSMALL, 0);
    }

  fh_desc->len = sz;
  memcpy(fh_desc->start, data, sz);
  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_DigestHandle */

/**
 * FSAL_ExpandHandle :
 *  Convert a buffer extracted from NFS handles
 *  to an FSAL handle.
 *
 * \param in_type (input):
 *        Indicates the type of digest to be expanded.
 * \param fh_desc (input/output):
 *        Pointer to the handle descriptor, length is set and verified
 *
 * \return The major code is ERR_FSAL_NO_ERROR is no error occured.
 *         Else, it is a non null value.
 */
fsal_status_t PROXYFSAL_ExpandHandle(fsal_export_context_t * p_expcontext, /* IN */
                                     fsal_digesttype_t in_type, /* IN */
                                     struct fsal_handle_desc *fh_desc)
{
#ifdef _HANDLE_MAPPING
  nfs23_map_handle_t *map_hdl;
  proxyfsal_handle_t tmp_hdl;
  int rc;
#endif
  ssize_t sz;

  /* sanity checks */
  if(!fh_desc || !fh_desc->start)
    ReturnCode(ERR_FSAL_FAULT, 0);

  switch (in_type)
    {
    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:
      if(!global_fsal_proxy_specific_info.enable_handle_mapping)
        ReturnCode(ERR_FSAL_NOTSUPP, 0);

#ifdef _HANDLE_MAPPING
      if(fh_desc->len < sizeof(*map_hdl))
        ReturnCode(ERR_FSAL_TOOSMALL, 0);

      map_hdl = (nfs23_map_handle_t *)fh_desc->start;
      rc = HandleMap_GetFH(map_hdl, &tmp_hdl);

      if(isFullDebug(COMPONENT_FSAL))
        {
          if(rc == HANDLEMAP_STALE)
            LogFullDebug(COMPONENT_FSAL, "File id=%llu : HandleMap_GetFH returns HANDLEMAP_STALE\n",
                         (unsigned long long)map_hdl->object_id);
          else if(rc == 0)
            LogFullDebug(COMPONENT_FSAL, "File id=%llu : HandleMap_GetFH returns HANDLEMAP_SUCCESS\n",
                         (unsigned long long)map_hdl->object_id);
          else
            LogFullDebug(COMPONENT_FSAL, "File id=%llu : HandleMap_GetFH returns error %d\n",
                         (unsigned long long)map_hdl->object_id, rc);
        }

      if(rc == HANDLEMAP_STALE)
        ReturnCode(ERR_FSAL_STALE, rc);
      else if(rc != 0)
        ReturnCode(ERR_FSAL_SERVERFAULT, rc);

      sz = proxy_sizeof_handle(&tmp_hdl);
      if(fh_desc->len < sz)
	ReturnCode(ERR_FSAL_TOOSMALL, 0);
      memcpy(fh_desc->start, &tmp_hdl, sz);
      break;
#endif
    /* fallthru */
    case FSAL_DIGEST_NFSV4:
      sz = proxy_sizeof_handle((proxyfsal_handle_t *)fh_desc->start);
      if(sz < 0)
	ReturnCode(ERR_FSAL_BADHANDLE, 0);
      if(fh_desc->len != sz)
        {
          LogMajor(COMPONENT_FSAL,
                   "size mismatch for handle.  should be %zd, got %zd",
                   sz, fh_desc->len);
          ReturnCode(ERR_FSAL_BADHANDLE, 0);
        }
      break;
    case FSAL_DIGEST_SIZEOF:
      sz = proxy_sizeof_handle((proxyfsal_handle_t *)fh_desc->start);
      break;
    default: /* Catch FILEID2, FILEID3, FILEID4 */
      ReturnCode(ERR_FSAL_SERVERFAULT, 0);
    }
  fh_desc->len = sz;  /* pass back the actual size */
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
