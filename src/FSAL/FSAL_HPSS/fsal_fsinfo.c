/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_fsinfo.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/16 08:20:22 $
 * \version $Revision: 1.12 $
 * \brief   functions for retrieving filesystem info.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

#if HPSS_MAJOR_VERSION == 5
  /* for struct statfs */
# include <sys/types.h>
#endif

/**
 * FSAL_static_fsinfo:
 * Return static filesystem info such as
 * behavior, configuration, supported operations...
 *
 * \param filehandle (input):
 *        Handle of an object in the filesystem
 *        whom info is to be retrieved.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param staticinfo (output):
 *        Pointer to the static info of the filesystem.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t HPSSFSAL_static_fsinfo(hpssfsal_handle_t * filehandle,    /* IN */
                                     hpssfsal_op_context_t * p_context, /* IN */
                                     fsal_staticfsinfo_t * staticinfo   /* OUT */
    )
{
  /* sanity checks. */
  /* for HPSS, handle and credential are not used. */
  if(!staticinfo)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_static_fsinfo);

  /* returning static info about the filesystem */
  (*staticinfo) = global_fs_info;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_static_fsinfo);

}

/**
 * FSAL_dynamic_fsinfo:
 * Return dynamic filesystem info such as
 * used size, free size, number of objects...
 *
 * \param filehandle (input):
 *        Handle of an object in the filesystem
 *        whom info is to be retrieved.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param dynamicinfo (output):
 *        Pointer to the static info of the filesystem.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t HPSSFSAL_dynamic_fsinfo(hpssfsal_handle_t * filehandle,   /* IN */
                                      hpssfsal_op_context_t * p_context,        /* IN */
                                      fsal_dynamicfsinfo_t * dynamicinfo        /* OUT */
    )
{

#if HPSS_MAJOR_VERSION == 5
  struct statfs hpss_statfs;
#elif HPSS_MAJOR_VERSION >= 6
  hpss_statfs_t hpss_statfs;
#endif

  unsigned int cos_export;
  int rc;

  /* sanity checks. */
  if(!filehandle || !dynamicinfo || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_dynamic_fsinfo);

  /* retrieves the default cos (or the user defined cos for this fileset) */

  if(p_context->export_context->default_cos != 0)
    {
      cos_export = p_context->export_context->default_cos;
    }
  else
    {
      /* retrieves default fileset cos */

      ns_FilesetAttrBits_t attrBits;
      ns_FilesetAttrs_t fsattrs;

      attrBits = cast64m(0);
      attrBits = orbit64m(attrBits, NS_FS_ATTRINDEX_COS);

      TakeTokenFSCall();
      rc = hpss_FilesetGetAttributes(NULL, NULL,
                                     &p_context->export_context->fileset_root_handle,
                                     NULL, attrBits, &fsattrs);
      ReleaseTokenFSCall();

      if(rc)
        Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_dynamic_fsinfo);

      cos_export = fsattrs.ClassOfService;

    }

  TakeTokenFSCall();
  rc = hpss_Statfs(p_context->export_context->default_cos, &hpss_statfs);
  ReleaseTokenFSCall();

  if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_dynamic_fsinfo);

  /* retrieves the default cos (or the user defined cos for this fileset) */
  if(p_context->export_context->default_cos != 0)
    {
      cos_export = p_context->export_context->default_cos;
    }
  else
    {
      /* retrieves default fileset cos */

      ns_FilesetAttrBits_t attrBits;
      ns_FilesetAttrs_t fsattrs;

      attrBits = cast64m(0);
      attrBits = orbit64m(attrBits, NS_FS_ATTRINDEX_COS);

      TakeTokenFSCall();
      rc = hpss_FilesetGetAttributes(NULL, NULL,
                                     &p_context->export_context->fileset_root_handle,
                                     NULL, attrBits, &fsattrs);
      ReleaseTokenFSCall();

      if(rc)
        Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_dynamic_fsinfo);

      cos_export = fsattrs.ClassOfService;

    }

  TakeTokenFSCall();
  rc = hpss_Statfs(p_context->export_context->default_cos, &hpss_statfs);
  ReleaseTokenFSCall();

  if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_dynamic_fsinfo);

  /* then retrieve info about this cos */
#ifdef BUGAZOMEU
  /* retrieves the default cos (or the user defined cos for this fileset) */
  if(DefaultCosId != 0)
    {
      cos_export = DefaultCosId;
    }
  else
    {
      /* retrieves default fileset cos */

      ns_FilesetAttrBits_t attrBits;
      ns_FilesetAttrs_t fsattrs;
      hpss_fileattr_t rootattr;
      ns_ObjHandle_t fshdl;

      /* recupere la racine */

      TakeTokenFSCall();
      rc = HPSSFSAL_GetRoot(&rootattr);
      ReleaseTokenFSCall();

      if(rc)
        Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_dynamic_fsinfo);

      fshdl = rootattr.ObjectHandle;

      /* recupere la cos du fileset correspondant */

      attrBits = cast64m(0);
      attrBits = orbit64m(attrBits, NS_FS_ATTRINDEX_COS);

      TakeTokenFSCall();
      rc = hpss_FilesetGetAttributes(NULL, NULL, &fshdl, NULL, attrBits, &fsattrs);
      ReleaseTokenFSCall();

      if(rc)
        Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_dynamic_fsinfo);

      cos_export = fsattrs.ClassOfService;

      /* @todo : sometimes NULL ??? */
      if(cos_export == 0)
        cos_export = 1;

    }

  /* then retrieve info about this cos */

  TakeTokenFSCall();
  rc = hpss_Statfs(cos_export, &hpss_statfs);
  ReleaseTokenFSCall();

  if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_dynamic_fsinfo);

  /* @todo :  sometimes hpss_statfs.f_blocks < hpss_statfs.f_bfree !!! */

  if(dynamicinfo->total_bytes > dynamicinfo->free_bytes)
    {
      dynamicinfo->total_bytes = hpss_statfs.f_blocks * hpss_statfs.f_bsize;
      dynamicinfo->free_bytes = hpss_statfs.f_bfree * hpss_statfs.f_bsize;
      dynamicinfo->avail_bytes = hpss_statfs.f_bavail * hpss_statfs.f_bsize;

      dynamicinfo->total_files = hpss_statfs.f_files;
      dynamicinfo->free_files = hpss_statfs.f_ffree;
      dynamicinfo->avail_files = hpss_statfs.f_ffree;
    }
#else

  /* return dummy values... like HPSS do... */

/*  dynamicinfo->total_bytes= 1976007601074984ULL;
  dynamicinfo->free_bytes =   23992398925016ULL;
  dynamicinfo->avail_bytes=   23992398925016ULL;*/

  dynamicinfo->total_bytes = INT_MAX;
  dynamicinfo->free_bytes = INT_MAX;
  dynamicinfo->avail_bytes = INT_MAX;

  dynamicinfo->total_files = 20000000;
  dynamicinfo->free_files = 1000000;
  dynamicinfo->avail_files = 1000000;

#endif
  dynamicinfo->time_delta.seconds = 1;
  dynamicinfo->time_delta.nseconds = 0;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_dynamic_fsinfo);

}
