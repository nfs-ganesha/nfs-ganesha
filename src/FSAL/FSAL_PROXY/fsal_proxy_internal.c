/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_internal.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.25 $
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#define FSAL_INTERNAL_C

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/clnt.h>
#include <gssrpc/xdr.h>
#include <gssrpc/auth.h>
#else
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <arpa/inet.h>
#endif
#include "nfs4.h"

#include "nfs_proto_functions.h"
#include "fsal_nfsv4_macros.h"

#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"
#include "SemN.h"

#include <pthread.h>
#include <string.h>
#include "abstract_mem.h"

extern time_t ServerBootTime;
extern fsal_staticfsinfo_t global_fs_info;
extern proxyfs_specific_initinfo_t global_fsal_proxy_specific_info;

void fsal_interval_proxy_fsalattr2bitmap4(fsal_attrib_list_t * pfsalattr,
                                          bitmap4 * pbitmap)
{
  uint32_t tmpattrlist[100];
  uint32_t attrlen = 0;

  if(FSAL_TEST_MASK(pfsalattr->asked_attributes, FSAL_ATTR_SIZE))
    tmpattrlist[attrlen++] = FATTR4_SIZE;
  if(FSAL_TEST_MASK(pfsalattr->asked_attributes, FSAL_ATTR_MODE))
    tmpattrlist[attrlen++] = FATTR4_MODE;
  if(FSAL_TEST_MASK(pfsalattr->asked_attributes, FSAL_ATTR_OWNER))
    tmpattrlist[attrlen++] = FATTR4_OWNER;
  if(FSAL_TEST_MASK(pfsalattr->asked_attributes, FSAL_ATTR_GROUP))
    tmpattrlist[attrlen++] = FATTR4_OWNER_GROUP;
  if(FSAL_TEST_MASK(pfsalattr->asked_attributes, FSAL_ATTR_ATIME))
    tmpattrlist[attrlen++] = FATTR4_TIME_ACCESS_SET;
  if(FSAL_TEST_MASK(pfsalattr->asked_attributes, FSAL_ATTR_ATIME_SERVER))
    tmpattrlist[attrlen++] = FATTR4_TIME_ACCESS_SET;
  if(FSAL_TEST_MASK(pfsalattr->asked_attributes, FSAL_ATTR_MTIME))
    tmpattrlist[attrlen++] = FATTR4_TIME_MODIFY_SET;
  if(FSAL_TEST_MASK(pfsalattr->asked_attributes, FSAL_ATTR_MTIME_SERVER))
    tmpattrlist[attrlen++] = FATTR4_TIME_MODIFY_SET;
  if(FSAL_TEST_MASK(pfsalattr->asked_attributes, FSAL_ATTR_CTIME))
    tmpattrlist[attrlen++] = FATTR4_TIME_METADATA;
 
  nfs4_list_to_bitmap4(pbitmap, attrlen, tmpattrlist);
}                               /* fsal_interval_proxy_fsalattr2bitmap4 */

/**
 * fsal_internal_proxy_create_fattr_bitmap :
 * Create the fattr4 bitmap related to the attributes managed by this implementation of the proxy
 *
 *
 * @param none
 *
 * @return the desired bitmap
 *
 */
void fsal_internal_proxy_create_fattr_bitmap(bitmap4 * pbitmap)
{
  uint32_t tmpattrlist[20];
  uint32_t attrlen = 0;

  pbitmap->bitmap4_len = 2;

  memset(pbitmap->bitmap4_val, 0, sizeof(uint32_t) * pbitmap->bitmap4_len);

  tmpattrlist[0] = FATTR4_TYPE;
  tmpattrlist[1] = FATTR4_CHANGE;
  tmpattrlist[2] = FATTR4_SIZE;
  tmpattrlist[3] = FATTR4_FSID;
  tmpattrlist[4] = FATTR4_FILEID;
  tmpattrlist[5] = FATTR4_MODE;
  tmpattrlist[6] = FATTR4_NUMLINKS;
  tmpattrlist[7] = FATTR4_OWNER;
  tmpattrlist[8] = FATTR4_OWNER_GROUP;
  tmpattrlist[9] = FATTR4_SPACE_USED;
  tmpattrlist[10] = FATTR4_TIME_ACCESS;
  tmpattrlist[11] = FATTR4_TIME_METADATA;
  tmpattrlist[12] = FATTR4_TIME_MODIFY;
  tmpattrlist[13] = FATTR4_RAWDEV;

  attrlen = 14;

  nfs4_list_to_bitmap4(pbitmap, attrlen, tmpattrlist);

}                               /* fsal_internal_proxy_create_fattr_bitmap */

void fsal_internal_proxy_create_fattr_readdir_bitmap(bitmap4 * pbitmap)
{
  uint32_t tmpattrlist[20];
  uint32_t attrlen = 0;

  pbitmap->bitmap4_len = 2;
  memset(pbitmap->bitmap4_val, 0, sizeof(uint32_t) * pbitmap->bitmap4_len);

  tmpattrlist[0] = FATTR4_TYPE;
  tmpattrlist[1] = FATTR4_CHANGE;
  tmpattrlist[2] = FATTR4_SIZE;
  tmpattrlist[3] = FATTR4_FSID;
  tmpattrlist[4] = FATTR4_FILEHANDLE;
  tmpattrlist[5] = FATTR4_FILEID;
  tmpattrlist[6] = FATTR4_MODE;
  tmpattrlist[7] = FATTR4_NUMLINKS;
  tmpattrlist[8] = FATTR4_OWNER;
  tmpattrlist[9] = FATTR4_OWNER_GROUP;
  tmpattrlist[10] = FATTR4_SPACE_USED;
  tmpattrlist[11] = FATTR4_TIME_ACCESS;
  tmpattrlist[12] = FATTR4_TIME_METADATA;
  tmpattrlist[13] = FATTR4_TIME_MODIFY;
  tmpattrlist[14] = FATTR4_RAWDEV;

  attrlen = 15;

  nfs4_list_to_bitmap4(pbitmap, attrlen, tmpattrlist);

}                               /* fsal_internal_proxy_create_fattr_readdir_bitmap */

void fsal_internal_proxy_create_fattr_fsinfo_bitmap(bitmap4 * pbitmap)
{
  uint32_t tmpattrlist[10];
  uint32_t attrlen = 0;

  pbitmap->bitmap4_len = 2;
  memset(pbitmap->bitmap4_val, 0, sizeof(uint32_t) * pbitmap->bitmap4_len);

  tmpattrlist[0] = FATTR4_FILES_AVAIL;
  tmpattrlist[1] = FATTR4_FILES_FREE;
  tmpattrlist[2] = FATTR4_FILES_TOTAL;
  tmpattrlist[3] = FATTR4_SPACE_AVAIL;
  tmpattrlist[4] = FATTR4_SPACE_FREE;
  tmpattrlist[5] = FATTR4_SPACE_TOTAL;

  attrlen = 6;

  nfs4_list_to_bitmap4(pbitmap, attrlen, tmpattrlist);

}                               /* fsal_internal_proxy_create_fattr_fsinfo_bitmap */

void fsal_internal_proxy_setup_readdir_fattr(fsal_proxy_internal_fattr_readdir_t * pfattr)
{
  /* Just do the correct connection */
  pfattr->owner.utf8string_val = pfattr->padowner;
  pfattr->owner_group.utf8string_val = pfattr->padgroup;
  pfattr->filehandle.nfs_fh4_val = pfattr->padfh;
}

void fsal_internal_proxy_setup_fattr(fsal_proxy_internal_fattr_t * pfattr)
{
  /* Just do the correct connection */
  pfattr->owner.utf8string_val = pfattr->padowner;
  pfattr->owner_group.utf8string_val = pfattr->padgroup;
}

/**
 * fsal_internal_proxy_error_convert :
 * Build a fsal_status in a correct way based on a NFS status
 *
 *
 * @param nfsstatus [IN] the nfs status to be converted
 * @param indexfunc [IN] the index of the FSAL function was does the call
 *
 * @return the converted value
 *
 */
fsal_status_t fsal_internal_proxy_error_convert(nfsstat4 nfsstatus, int indexfunc)
{
  switch (nfsstatus)
    {
    case NFS4_OK:
      Return(ERR_FSAL_NO_ERROR, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_PERM:
      Return(ERR_FSAL_PERM, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_NOENT:
      Return(ERR_FSAL_NOENT, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_IO:
      Return(ERR_FSAL_IO, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_NXIO:
      Return(ERR_FSAL_NXIO, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_ACCESS:
    case NFS4ERR_DENIED:
      Return(ERR_FSAL_ACCESS, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_EXIST:
      Return(ERR_FSAL_EXIST, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_XDEV:
      Return(ERR_FSAL_XDEV, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_NOTDIR:
      Return(ERR_FSAL_NOTDIR, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_ISDIR:
      Return(ERR_FSAL_ISDIR, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_INVAL:
      Return(ERR_FSAL_INVAL, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_FBIG:
      Return(ERR_FSAL_FBIG, 0, indexfunc);
      break;

    case NFS4ERR_NOSPC:
      Return(ERR_FSAL_NOSPC, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_ROFS:
      Return(ERR_FSAL_ROFS, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_MLINK:
      Return(ERR_FSAL_MLINK, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_NAMETOOLONG:
      Return(ERR_FSAL_NAMETOOLONG, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_NOTEMPTY:
      Return(ERR_FSAL_NOTEMPTY, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_DQUOT:
      Return(ERR_FSAL_DQUOT, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_STALE:
      Return(ERR_FSAL_STALE, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_BADHANDLE:
      Return(ERR_FSAL_BADHANDLE, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_BAD_COOKIE:
      Return(ERR_FSAL_BADCOOKIE, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_NOTSUPP:
      Return(ERR_FSAL_NOTSUPP, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_TOOSMALL:
      Return(ERR_FSAL_TOOSMALL, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_SERVERFAULT:
      Return(ERR_FSAL_SERVERFAULT, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_BADTYPE:
      Return(ERR_FSAL_BADTYPE, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_DELAY:
      Return(ERR_FSAL_DELAY, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_SAME:
    case NFS4ERR_NOT_SAME:
      Return(ERR_FSAL_NO_ERROR, (int)nfsstatus, indexfunc);     /* no "actual" errors */
      break;

    case NFS4ERR_GRACE:
      Return(ERR_FSAL_DELAY, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_EXPIRED:
    case NFS4ERR_LOCKED:
    case NFS4ERR_SHARE_DENIED:
    case NFS4ERR_LOCK_RANGE:
    case NFS4ERR_OPENMODE:
    case NFS4ERR_FILE_OPEN:
      Return(ERR_FSAL_ACCESS, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_FHEXPIRED:
      Return(ERR_FSAL_FHEXPIRED, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_WRONGSEC:
      Return(ERR_FSAL_SEC, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_CLID_INUSE:
    case NFS4ERR_MOVED:
    case NFS4ERR_RESOURCE:
    case NFS4ERR_MINOR_VERS_MISMATCH:
    case NFS4ERR_STALE_CLIENTID:
    case NFS4ERR_STALE_STATEID:
    case NFS4ERR_OLD_STATEID:
    case NFS4ERR_BAD_STATEID:
    case NFS4ERR_BAD_SEQID:
    case NFS4ERR_RESTOREFH:
    case NFS4ERR_LEASE_MOVED:
    case NFS4ERR_NO_GRACE:
    case NFS4ERR_RECLAIM_BAD:
    case NFS4ERR_RECLAIM_CONFLICT:
    case NFS4ERR_BADXDR:
    case NFS4ERR_BADCHAR:
    case NFS4ERR_BADNAME:
    case NFS4ERR_BAD_RANGE:
    case NFS4ERR_BADOWNER:
    case NFS4ERR_OP_ILLEGAL:
    case NFS4ERR_LOCKS_HELD:
    case NFS4ERR_LOCK_NOTSUPP:
    case NFS4ERR_DEADLOCK:
    case NFS4ERR_ADMIN_REVOKED:
    case NFS4ERR_CB_PATH_DOWN:
      Return(ERR_FSAL_INVAL, (int)nfsstatus, indexfunc);        /* For wanting of something wiser */
      break;

    case NFS4ERR_NOFILEHANDLE:
      Return(ERR_FSAL_BADHANDLE, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_SYMLINK:
      Return(ERR_FSAL_SYMLINK, (int)nfsstatus, indexfunc);
      break;

    case NFS4ERR_ATTRNOTSUPP:
      Return(ERR_FSAL_ATTRNOTSUPP, (int)nfsstatus, indexfunc);
      break;

    default:                   /* Should never occur, all cases are stated above */
      Return(ERR_FSAL_INVAL, (int)nfsstatus, indexfunc);
      break;

    }                           /* switch( nfsstatus ) */

}                               /* fsal_internal_proxy_error_convert */

/**
 * fsal_internal_proxy_create_fh :
 *
 *
 * @param pnfs4_handle [IN]  the NFSv4 Handle
 * @param type         [IN]  the type of object for this entry
 * @param fileid       [IN]  the file id for this entry
 * @param pfsal_handle [OUT] the resulting FSAL Handle
 *
 * @return TRUE if OK, FALSE otherwise
 *
 */
int fsal_internal_proxy_create_fh(nfs_fh4 * pnfs4_handle,
                                  fsal_nodetype_t type,
                                  fsal_u64_t fileid, fsal_handle_t * fsal_handle)
{
  proxyfsal_handle_t * pfsal_handle = (proxyfsal_handle_t *) fsal_handle;

  if(pnfs4_handle == NULL || pfsal_handle == NULL)
    return FALSE;

  if(isFullDebug(COMPONENT_FSAL))
    {
      char outstr[1024];
      nfs4_sprint_fhandle(pnfs4_handle, outstr);
      LogFullDebug(COMPONENT_FSAL, "fsal_internal_proxy_create_fh: input nfsv4 server handle=%s", outstr);
    }
 
  memset( (char *)pfsal_handle, 0, sizeof( proxyfsal_handle_t ) ) ;

  pfsal_handle->data.object_type_reminder = type;
  pfsal_handle->data.fileid4 = fileid;
  pfsal_handle->data.srv_handle_len = pnfs4_handle->nfs_fh4_len;
  memset(pfsal_handle->data.srv_handle_val, 0, FSAL_PROXY_FILEHANDLE_MAX_LEN);
  memcpy(pfsal_handle->data.srv_handle_val, pnfs4_handle->nfs_fh4_val,
         pnfs4_handle->nfs_fh4_len);

  if(isFullDebug(COMPONENT_FSAL))
    {
      char outstr[1024];
      if(type == FSAL_TYPE_FILE)
        {
          nfs_fh4 tmpfh;

          tmpfh.nfs_fh4_len = pfsal_handle->data.srv_handle_len;
          tmpfh.nfs_fh4_val = pfsal_handle->data.srv_handle_val;
          nfs4_sprint_fhandle(&tmpfh, outstr);
          LogFullDebug(COMPONENT_FSAL,
                       "fsal_internal_proxy_create_fh: output nfsv4 server handle= %s fileid=%llu",
                       outstr, fileid);
        }

      if(memcmp
         (pfsal_handle->data.srv_handle_val, pnfs4_handle->nfs_fh4_val, pnfs4_handle->nfs_fh4_len))
        LogFullDebug(COMPONENT_FSAL,
                     "CRITICAL ERROR: ==========> Filehandle mismatch n ifsal_internal_proxy_create");
    }

  return TRUE;
}                               /* fsal_internal_proxy_create_fh */

/**
 * fsal_internal_proxy_extract_fh :
 * Do the FSAL -> NFSv4 FH conversion
 *
 *
 * @param pnfs4_handle [OUT] the NFSv4 Handle
 * @param pfsal_handle [IN]  the resulting FSAL Handle
 *
 * @return TRUE if OK, FALSE otherwise
 *
 */
int fsal_internal_proxy_extract_fh(nfs_fh4 * pnfs4_handle,
                                   fsal_handle_t * fsal_handle)
{
  proxyfsal_handle_t * pfsal_handle = (proxyfsal_handle_t *) fsal_handle;

  if(pnfs4_handle == NULL || pfsal_handle == NULL)
    return FALSE;

  pnfs4_handle->nfs_fh4_len = pfsal_handle->data.srv_handle_len;
  pnfs4_handle->nfs_fh4_val = pfsal_handle->data.srv_handle_val;

  if(isFullDebug(COMPONENT_FSAL))
    {
      char outstr[1024];
      nfs4_sprint_fhandle(pnfs4_handle, outstr);
      LogFullDebug(COMPONENT_FSAL, "fsal_internal_proxy_extract_fh: input nfsv4 server handle=%s\n", outstr);
    }

  return TRUE;
}                               /* fsal_internal_proxy_extract_fh */

/**
 * fsal_internal_proxy_fsal_name_2_utf8 :
 * Do the FSAL -> NFSv4 UTF8 str conversion
 *
 *
 * @param pname   [IN]  the fsal name
 * @param utf8str [OUT] the resulting UTF8 str
 *
 * @return TRUE if OK, FALSE otherwise
 *
 */

int fsal_internal_proxy_fsal_name_2_utf8(fsal_name_t * pname, utf8string * utf8str)
{
  char tmpstr[FSAL_MAX_NAME_LEN];
  fsal_status_t fsal_status;

  if(pname == NULL || utf8str == NULL)
    return FALSE;

  fsal_status = FSAL_name2str(pname, tmpstr, FSAL_MAX_NAME_LEN);
  if(fsal_status.major != ERR_FSAL_NO_ERROR)
    return FALSE;

  if(utf8str->utf8string_len == 0)
    {
      if((utf8str->utf8string_val = gsh_malloc(pname->len)) == NULL)
        return FALSE;
      else
        utf8str->utf8string_len = pname->len;
    }

  if(str2utf8(tmpstr, utf8str) == -1)
    return FALSE;
  return TRUE;
}                               /* fsal_internal_proxy_fsal_name_2_utf8 */

/**
 * fsal_internal_proxy_fsal_path_2_utf8 :
 * Do the FSAL -> NFSv4 UTF8 str conversion
 *
 *
 * @param ppath   [IN]  the fsal path
 * @param utf8str [OUT] the resulting UTF8 str
 *
 * @return TRUE if OK, FALSE otherwise
 *
 */

int fsal_internal_proxy_fsal_path_2_utf8(fsal_path_t * ppath, utf8string * utf8str)
{
  char tmpstr[FSAL_MAX_PATH_LEN];
  fsal_status_t fsal_status;

  if(ppath == NULL || utf8str == NULL)
    return FALSE;

  fsal_status = FSAL_path2str(ppath, tmpstr, FSAL_MAX_NAME_LEN);
  if(fsal_status.major != ERR_FSAL_NO_ERROR)
    return FALSE;

  if(utf8str->utf8string_len == 0)
    {
      if((utf8str->utf8string_val = gsh_malloc(ppath->len)) == NULL)
        return FALSE;
      else
        utf8str->utf8string_len = ppath->len;
    }

  if(str2utf8(tmpstr, utf8str) == -1)
    return FALSE;

  return TRUE;
}                               /* fsal_internal_proxy_fsal_path_2_utf8 */

/**
 * fsal_internal_proxy_fsal_utf8_2_path :
 * Do the NFSv4 UTF8 -> FSAL str conversion
 *
 *
 * @param ppath   [IN]  the fsal path
 * @param utf8str [OUT] the resulting UTF8 str
 *
 * @return TRUE if OK, FALSE otherwise
 *
 */

int fsal_internal_proxy_fsal_utf8_2_path(fsal_path_t * ppath, utf8string * utf8str)
{
  char tmpstr[FSAL_MAX_PATH_LEN];
  fsal_status_t fsal_status;

  if(ppath == NULL || utf8str == NULL)
    return FALSE;

  if(utf82str(tmpstr, sizeof(tmpstr), utf8str) == -1)
    return FALSE;

  fsal_status = FSAL_str2path(tmpstr, FSAL_MAX_PATH_LEN, ppath);
  if(fsal_status.major != ERR_FSAL_NO_ERROR)
    return FALSE;

  return TRUE;
}                               /* fsal_internal_proxy_fsal_utf8_2_path */

/**
 * fsal_internal_proxy_fsal_utf8_2_name :
 * Do the NFSv4 UTF8 -> FSAL str conversion
 *
 *
 * @param pname   [IN]  the fsal name
 * @param utf8str [OUT] the resulting UTF8 str
 *
 * @return TRUE if OK, FALSE otherwise
 *
 */

int fsal_internal_proxy_fsal_utf8_2_name(fsal_name_t * pname, utf8string * utf8str)
{
  char tmpstr[FSAL_MAX_NAME_LEN+1];
  fsal_status_t fsal_status;

  if(pname == NULL || utf8str == NULL)
    return FALSE;

  if(utf82str(tmpstr, sizeof(tmpstr), utf8str) == -1)
    return FALSE;

  fsal_status = FSAL_str2name(tmpstr, FSAL_MAX_NAME_LEN, pname);
  if(fsal_status.major != ERR_FSAL_NO_ERROR)
    return FALSE;

  return TRUE;
}                               /* fsal_internal_proxy_fsal_utf8_2_name */

/**
 *
 * proxy_Fattr_To_FSAL_dynamic_fsinfo: Converts NFSv4 attributes buffer to a FSAL dynamic fsinfo structure.
 *
 *  Converts NFSv4 attributes buffer to a FSAL dynamic fsinfo structure.
 *
 * @param pdynamicinfo [OUT]  pointer to FSAL attributes.
 * @param Fattr        [IN] pointer to NFSv4 attributes.
 *
 * @return 1 if successful, 0 if not supported, -1 if argument is badly formed
 *
 */
int proxy_Fattr_To_FSAL_dynamic_fsinfo(fsal_dynamicfsinfo_t * pdynamicinfo,
                                       fattr4 * Fattr)
{
  u_int LastOffset = 0;
  unsigned int i = 0;
  char __attribute__ ((__unused__)) funcname[] = "proxy_Fattr_To_FSAL_dynamic_fsinfo";
  uint32_t attrmasklist[FATTR4_MOUNTED_ON_FILEID];      /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  uint32_t attrmasklen = 0;
  uint32_t attribute_to_set = 0;
  uint64_t tmp_uint64 = 0LL;

  if(pdynamicinfo == NULL || Fattr == NULL)
    return -1;

  /* Check attributes data */
  if(Fattr->attr_vals.attrlist4_val == NULL)
    return -1;

  /* Convert the attribute bitmap to an attribute list */
  nfs4_bitmap4_to_list(&(Fattr->attrmask), &attrmasklen, attrmasklist);

  LogFullDebug(COMPONENT_NFS_V4, "   nfs4_bitmap4_to_list ====> attrmasklen = %d\n", attrmasklen);

  /* Init */
  memset((char *)pdynamicinfo, 0, sizeof(fsal_dynamicfsinfo_t));

  for(i = 0; i < attrmasklen; i++)
    {
      attribute_to_set = attrmasklist[i];

      if(attrmasklist[i] > FATTR4_MOUNTED_ON_FILEID)
        {
          /* Erroneous value... skip */
          continue;
        }
      LogFullDebug(COMPONENT_NFS_V4, "=================> nfs4_Fattr_To_FSAL_attr: i=%u attr=%u\n", i,
             attrmasklist[i]);
      LogFullDebug(COMPONENT_NFS_V4, "Flag for Operation = %d|%d is ON,  name  = %s  reply_size = %d\n",
             attrmasklist[i], fattr4tab[attribute_to_set].val,
             fattr4tab[attribute_to_set].name, fattr4tab[attribute_to_set].size_fattr4);

      switch (attribute_to_set)
        {
        case FATTR4_FILES_AVAIL:
          memcpy((char *)&tmp_uint64,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_files_avail));
          pdynamicinfo->avail_files = nfs_ntohl64(tmp_uint64);

          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_FILES_FREE:
          memcpy((char *)&tmp_uint64,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_files_free));
          pdynamicinfo->free_files = nfs_ntohl64(tmp_uint64);

          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_FILES_TOTAL:
          memcpy((char *)&tmp_uint64,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_files_total));
          pdynamicinfo->total_files = nfs_ntohl64(tmp_uint64);

          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_SPACE_AVAIL:
          memcpy((char *)&tmp_uint64,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_space_avail));
          pdynamicinfo->avail_bytes = nfs_ntohl64(tmp_uint64);

          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_SPACE_FREE:
          memcpy((char *)&tmp_uint64,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_space_free));
          pdynamicinfo->free_bytes = nfs_ntohl64(tmp_uint64);

          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_SPACE_TOTAL:
          memcpy((char *)&tmp_uint64,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_space_total));
          pdynamicinfo->total_bytes = nfs_ntohl64(tmp_uint64);

          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        default:
          LogFullDebug(COMPONENT_NFS_V4, "SATTR: Attribut no supporte %d name=%s\n", attribute_to_set,
                 fattr4tab[attribute_to_set].name);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          break;

        }                       /*   switch( attribute_to_set ) */

    }

  return 1;

}                               /* proxy_Fattr_To_FSAL_dynamic_fsinfo */

/**
 *
 * proxy_Fattr_To_FSAL_attr: Converts NFSv4 attributes buffer to a FSAL attributes structure.
 *
 * Converts NFSv4 attributes buffer to a FSAL attributes structure.
 *
 * @param pFSAL_attr [OUT]  pointer to FSAL attributes.
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 *
 * @return 1 if successful, 0 if not supported, -1 if argument is badly formed
 *
 */
int proxy_Fattr_To_FSAL_attr(fsal_attrib_list_t * pFSAL_attr,
                             proxyfsal_handle_t * phandle, fattr4 * Fattr)
{
  nfs_fh4 hdl4;

  if (Fattr4_To_FSAL_attr(pFSAL_attr, Fattr, &hdl4, ANON_UID, ANON_GID) != NFS4_OK)
      return -1;

  return fsal_internal_proxy_create_fh(&hdl4, pFSAL_attr->type, pFSAL_attr->fileid,
                                      (fsal_handle_t *)phandle);
}                               /* proxy_Fattr_To_FSAL_attr */

fsal_status_t fsal_internal_set_auth_gss(proxyfsal_op_context_t * p_thr_context)
{
#ifdef _USE_GSSRPC
  fsal_status_t fsal_status;
  struct rpc_gss_sec rpcsec_gss_data;
  gss_OID mechOid;
  char mechname[1024];
  gss_buffer_desc mechgssbuff;
  OM_uint32 maj_stat, min_stat;

  /* Set up mechOid */
  strcpy(mechname, "{ 1 2 840 113554 1 2 2 }");

  mechgssbuff.value = mechname;
  mechgssbuff.length = strlen(mechgssbuff.value);

  LogFullDebug(COMPONENT_FSAL, "----> %p\n", p_thr_context->rpc_client);
  if((maj_stat = gss_str_to_oid(&min_stat, &mechgssbuff, &mechOid)) != GSS_S_COMPLETE)
    Return(ERR_FSAL_SEC, maj_stat, INDEX_FSAL_InitClientContext);

  /* Authentification avec RPCSEC_GSS */
  rpcsec_gss_data.mech = mechOid;
  rpcsec_gss_data.qop = GSS_C_QOP_DEFAULT;
  rpcsec_gss_data.svc = global_fsal_proxy_specific_info.sec_type;

  if((p_thr_context->rpc_client->cl_auth =
      authgss_create_default(p_thr_context->rpc_client,
                             global_fsal_proxy_specific_info.remote_principal,
                             &rpcsec_gss_data)) == NULL)
    Return(ERR_FSAL_SEC, 0, INDEX_FSAL_InitClientContext);
#endif
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);
}                               /* fsal_internal_set_auth_gss */


fsal_status_t
fsal_proxy_create_rpc_clnt(proxyfsal_op_context_t * ctx)
{
  int sock;
  struct sockaddr_in addr_rpc;
  struct timeval timeout = TIMEOUTRPC;
  int rc;
  int priv_port = 0 ; 
  fsal_status_t fsal_status;
  char addr[INET_ADDRSTRLEN];

  memset(&addr_rpc, 0, sizeof(addr_rpc));
  addr_rpc.sin_port = ctx->srv_port;
  addr_rpc.sin_family = AF_INET;
  addr_rpc.sin_addr.s_addr = ctx->srv_addr;

  if(!strcmp(ctx->srv_proto, "udp"))
    {
      if((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        ReturnCode(ERR_FSAL_FAULT, errno);

      ctx->rpc_client = clntudp_bufcreate(&addr_rpc,
                                          ctx->srv_prognum,
                                          FSAL_PROXY_NFS_V4,
                                          (struct timeval){ 25, 0},
                                          &sock,
                                          ctx->srv_sendsize,
                                          ctx->srv_recvsize);

      if(ctx->rpc_client == NULL)
        {

          LogCrit(COMPONENT_FSAL,
                  "Cannot contact server addr=%s port=%u prognum=%u using NFSv4 protocol",
                  inet_ntop(AF_INET, &ctx->srv_addr, addr, sizeof(addr)),
                  ntohs(ctx->srv_port), ctx->srv_prognum);

          ReturnCode(ERR_FSAL_INVAL, 0);
        }
    }
  else if(!strcmp(ctx->srv_proto, "tcp"))
    {
      if( ctx->use_privileged_client_port  == TRUE )
        {
	  if( (sock = rresvport( &priv_port ) )< 0 )
           {
             LogCrit(COMPONENT_FSAL, "Cannot create a tcp socket on a privileged port");
             ReturnCode(ERR_FSAL_FAULT, 0);
           }
        }
      else
        {
          if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
            {
              LogCrit(COMPONENT_FSAL, "Cannot create a tcp socket - %d", errno);
              ReturnCode(ERR_FSAL_FAULT, 0);
            }
        }

      if(connect(sock, (struct sockaddr *)&addr_rpc, sizeof(addr_rpc)) < 0)
        {
          LogCrit(COMPONENT_FSAL,
                  "Cannot connect to server addr=%s port=%u",
                  inet_ntop(AF_INET, &ctx->srv_addr, addr, sizeof(addr)),
                  ntohs(ctx->srv_port));

          ReturnCode(ERR_FSAL_FAULT, 0);
        }

      ctx->rpc_client = clnttcp_create(&addr_rpc,
                                       ctx->srv_prognum,
                                       FSAL_PROXY_NFS_V4,
                                       &sock,
                                       ctx->srv_sendsize,
                                       ctx->srv_recvsize);
      if(ctx->rpc_client == NULL)
        {
          LogCrit(COMPONENT_FSAL,
                  "Cannot contact server addr=%s port=%u prognum=%u using NFSv4 protocol",
                  inet_ntop(AF_INET, &ctx->srv_addr, addr, sizeof(addr)),
                  ntohs(ctx->srv_port), ctx->srv_prognum);

          ReturnCode(ERR_FSAL_INVAL, 0);
        }
    }
  else
    {
      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  ctx->socket = sock;

#ifdef _USE_GSSRPC
  if(global_fsal_proxy_specific_info.active_krb5 == TRUE)
    {
      fsal_status = fsal_internal_set_auth_gss(ctx);
      if(FSAL_IS_ERROR(fsal_status))
        ReturnCode(fsal_status.major, fsal_status.minor);
    }
  else
#endif                          /* _USE_GSSRPC */
  if((ctx->rpc_client->cl_auth = authunix_create_default()) == NULL)
    ReturnCode(ERR_FSAL_INVAL, 0);

  /* test if the newly created context can 'ping' the server via PROC_NULL */
  rc = clnt_call(ctx->rpc_client, NFSPROC4_NULL,
                 (xdrproc_t) xdr_void, (caddr_t) NULL,
                 (xdrproc_t) xdr_void, (caddr_t) NULL, timeout);
  if(rc  != RPC_SUCCESS)
    ReturnCode(ERR_FSAL_INVAL, rc);

  fsal_status = FSAL_proxy_setclientid(ctx);
  if(FSAL_IS_ERROR(fsal_status))
    ReturnCode(ERR_FSAL_FAULT, 0);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 *
 * fsal_internal_ClientReconnect: Redo a previously lost connection to the server.
 *
 * Redo a previously lost connection to the server.
 *
 * @param p_thr_context [INOUT]  pointer to the FSAL thread context.
 *
 * @return 0 if successful, -1 if failed
 *
 */
int fsal_internal_ClientReconnect(proxyfsal_op_context_t * p_thr_context)
{
  fsal_status_t fsal_status;
  char addr[INET_ADDRSTRLEN];

  LogEvent(COMPONENT_FSAL,
           "Lost connection to the server %s, trying to reconnect",
           inet_ntop(AF_INET,  &p_thr_context->srv_addr, addr, sizeof(addr)));

  /* First of all, close the formerly opened socket that is now useless */
  if( close( p_thr_context->socket ) == -1 )
    LogMajor( COMPONENT_FSAL,
              "FSAL RECONNECT : got POSIX error %u while closing socket in fsal_internal_ClientReconnect", errno ) ;
  
  if(p_thr_context->rpc_client)
    {
      auth_destroy(p_thr_context->rpc_client->cl_auth);
      p_thr_context->rpc_client->cl_auth = NULL;
      clnt_destroy(p_thr_context->rpc_client);
      p_thr_context->rpc_client = NULL;
    }
  fsal_status = fsal_proxy_create_rpc_clnt(p_thr_context);
  if(FSAL_IS_ERROR(fsal_status))
    return -1;

  fsal_status = FSAL_proxy_setclientid_renego(p_thr_context);
  if(FSAL_IS_ERROR(fsal_status))
    return -1;

  return 0;
}                               /* fsal_internal_ClientReconnect */

/**
 * FSAL_proxy_open_confirm:
 * Confirms a previously made OP_OPEN if this is required by the server
 *
 * \param pfd [INOUT] :
 *        Open file descriptor for the file whose open is to be confirmed
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *        ERR_FSAL_IO, ...
 */

fsal_status_t FSAL_proxy_open_confirm(proxyfsal_file_t * pfd)
{
  fsal_status_t fsal_status;
  int rc;
  

  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;

#define FSAL_PROXY_OPEN_CONFIRM_NB_OP_ALLOC 2
#define FSAL_PROXY_OPEN_CONFIRM_IDX_OP_PUTFH        0
#define FSAL_PROXY_OPEN_CONFIRM_IDX_OP_OPEN_CONFIRM 1
  nfs_argop4 argoparray[FSAL_PROXY_OPEN_CONFIRM_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_PROXY_OPEN_CONFIRM_NB_OP_ALLOC];
  struct timeval timeout = TIMEOUTRPC;

  if(pfd == NULL)
    {
      fsal_status.major = ERR_FSAL_FAULT;
      fsal_status.minor = 0;

      return fsal_status;
    }

  if(pfd->pcontext == NULL)
    {
      LogFullDebug(COMPONENT_FSAL, "===================> FSAL_proxy_open_confirm: Non initialized fd !!!!!");
      fsal_status.major = ERR_FSAL_FAULT;
      fsal_status.minor = 0;

      return fsal_status;
    }
  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, (fsal_handle_t *) &pfd->fhandle) == FALSE)
    {
      fsal_status.major = ERR_FSAL_FAULT;
      fsal_status.minor = 0;

      return fsal_status;
    }

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  argnfs4.argarray.argarray_val[FSAL_PROXY_OPEN_CONFIRM_IDX_OP_OPEN_CONFIRM].argop =
      NFS4_OP_OPEN_CONFIRM;
  argnfs4.argarray.argarray_val[FSAL_PROXY_OPEN_CONFIRM_IDX_OP_OPEN_CONFIRM].nfs_argop4_u.
      opopen_confirm.open_stateid.seqid = pfd->stateid.seqid;
  memcpy((char *)argnfs4.argarray.
         argarray_val[FSAL_PROXY_OPEN_CONFIRM_IDX_OP_OPEN_CONFIRM].nfs_argop4_u.
         opopen_confirm.open_stateid.other, (char *)pfd->stateid.other, 12);
  argnfs4.argarray.argarray_val[FSAL_PROXY_OPEN_CONFIRM_IDX_OP_OPEN_CONFIRM].nfs_argop4_u.
      opopen_confirm.seqid = pfd->stateid.seqid + 1;
  argnfs4.argarray.argarray_len = 2;

  TakeTokenFSCall();
  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(pfd->pcontext, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      fsal_status.major = ERR_FSAL_IO;
      fsal_status.minor = resnfs4.status;

      return fsal_status;
    }
  ReleaseTokenFSCall();

  /* set the error is res if not NFS4_OK */
  if(resnfs4.status != NFS4_OK)
    {
      fsal_status.major = ERR_FSAL_IO;
      fsal_status.minor = resnfs4.status;

      return fsal_status;
    }
  /* Update the file descriptor with the new stateid */
  pfd->stateid.seqid =
      resnfs4.resarray.resarray_val[FSAL_PROXY_OPEN_CONFIRM_IDX_OP_OPEN_CONFIRM].
      nfs_resop4_u.opopen_confirm.OPEN_CONFIRM4res_u.resok4.open_stateid.seqid;
  memcpy((char *)pfd->stateid.other,
         (char *)resnfs4.resarray.
         resarray_val[FSAL_PROXY_OPEN_CONFIRM_IDX_OP_OPEN_CONFIRM].nfs_resop4_u.
         opopen_confirm.OPEN_CONFIRM4res_u.resok4.open_stateid.other, 12);

  fsal_status.major = ERR_FSAL_NO_ERROR;
  fsal_status.minor = NFS4_OK;

  return fsal_status;
}                               /* FSAL_proxy_open_confirm */

void *FSAL_proxy_change_user(proxyfsal_op_context_t * p_thr_context)
{
  static char hostname[MAXNAMLEN+1];
  static bool_t done = FALSE;

  P(p_thr_context->lock);
  switch (p_thr_context->rpc_client->cl_auth->ah_cred.oa_flavor)
    {
    case AUTH_NONE:
      /* well... to be honest, there is nothing to be done here... */
      break;

    case AUTH_UNIX:
      if(!done)
        {
          if(gethostname(hostname, MAXNAMLEN) == -1)
            strncpy(hostname, "NFS-GANESHA/Proxy", MAXNAMLEN);

          done = TRUE;
        }
      auth_destroy(p_thr_context->rpc_client->cl_auth);

      p_thr_context->rpc_client->cl_auth =
	      authunix_create(hostname,
			      p_thr_context->credential.user,
			      p_thr_context->credential.group,
			      p_thr_context->credential.nbgroups,
			      p_thr_context->credential.alt_groups);
      break;
#ifdef _USE_GSSRPC
    case RPCSEC_GSS:
#endif
    default:
        /** @todo: Nothing done now. Once RPCSEC_GSS will have explicit management, return an error as defaut behavior: non supported auth flavor */
      break;

    }                           /* switch( pthr_context->rpc_client->cl_auth->ah_cred.oa_flavor ) */

  V(p_thr_context->lock);

  /* Return authentication */
  return p_thr_context->rpc_client->cl_auth;
}                               /* FSAL_proxy_change_user */
