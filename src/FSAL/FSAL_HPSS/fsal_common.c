/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * Common HPSS tools for internal use in the FSAL.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal_common.h"
#include "fsal_internal.h"

#include <hpss_errno.h>

#define STRCMP strcasecmp

/**
 * HPSSFSAL_GetFilesetRoot :
 * Retrieves info about the root of a fileset (handle and attributes).
 *
 * \param hpss_fileattr_t (input/output):
 *        Pointer to the attributes to be retrieved.
 *
 * \return The HPSS error code (0 if no error, a negative value else).
 */
int HPSSFSAL_GetFilesetRoot(char *fileset_name, ns_ObjHandle_t * p_root_hdl)
{
  int rc;

  /* if fileset_name is null, root fileset is returned */

  if (fileset_name == NULL || fileset_name[0] == '\0')
    {
      hpss_fileattr_t root_attr;

      rc = hpss_FileGetAttributes("/", &root_attr);
      if (rc)
        return rc;

      *p_root_hdl = root_attr.ObjectHandle;
      return 0;

    }
  else
    {
      ns_FilesetAttrBits_t attrBits;
      ns_FilesetAttrs_t fsattrs;

      attrBits = cast64m(0);
      attrBits = orbit64m(attrBits, NS_FS_ATTRINDEX_FILESET_HANDLE);

      rc = hpss_FilesetGetAttributes(fileset_name, NULL, NULL, NULL, attrBits, &fsattrs);

      if (rc)
        return rc;

      *p_root_hdl = fsattrs.FilesetHandle;
      return 0;

    }
}

/**
 * HPSSFSAL_BuildCos :
 * Fills Cos structures from a CosId.
 *
 * \param CosId (input):
 *        The chosen COSId.
 * \param hints (output):
 *        The filled hpss_cos_hints_t structure.
 * \param hintpri (output):
 *        The filled hpss_cos_priorities_t structure.
 *
 * \return Nothing.
 */

void HPSSFSAL_BuildCos(fsal_uint_t CosId,
                       hpss_cos_hints_t * hints, hpss_cos_priorities_t * hintpri)
{

  /* clears the structures */
  memset(hints, 0, sizeof(hpss_cos_hints_t));
  memset(hintpri, 0, sizeof(hpss_cos_priorities_t));

  hints->COSId = (unsigned32) CosId;
  hintpri->COSIdPriority = REQUIRED_PRIORITY;

  hintpri->COSNamePriority = NO_PRIORITY;
  hintpri->WriteOpsPriority = NO_PRIORITY;
  hintpri->ReadOpsPriority = NO_PRIORITY;
  hintpri->AccessFrequencyPriority = NO_PRIORITY;
  hintpri->TransferRatePriority = NO_PRIORITY;
  hintpri->MinFileSizePriority = NO_PRIORITY;
  hintpri->MaxFileSizePriority = NO_PRIORITY;
  hintpri->OptimumAccessSizePriority = NO_PRIORITY;
  hintpri->AvgLatencyPriority = NO_PRIORITY;
  hintpri->StageCodePriority = NO_PRIORITY;

  return;

}

int HPSSFSAL_IsStaleHandle(ns_ObjHandle_t * p_hdl, TYPE_CRED_HPSS * p_cred)
{
  int rc;

  TakeTokenFSCall();

  rc = HPSSFSAL_GetRawAttrHandle(p_hdl, NULL, p_cred, FALSE,    /* don't solve junctions */
                                 NULL, NULL, NULL);

  ReleaseTokenFSCall();

  if ((rc == HPSS_ENOENT) || (rc == HPSS_ENOTDIR))
    return TRUE;

  return FALSE;
}
