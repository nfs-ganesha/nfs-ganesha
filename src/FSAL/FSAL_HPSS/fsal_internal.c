/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_internal.c
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.25 $
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 *
 */
#define FSAL_INTERNAL_C
#include "config.h"

#include "fsal.h"

#include "fsal_internal.h"
#include "abstract_mem.h"

#include <pthread.h>


int HPSSFSAL_IsStaleHandle(ns_ObjHandle_t * p_hdl, TYPE_CRED_HPSS * p_cred)
{
  int rc;


  rc = HPSSFSAL_GetRawAttrHandle(p_hdl, NULL, p_cred, false,    /* don't solve junctions */
                                 NULL, NULL, NULL);


  if((rc == HPSS_ENOENT) || (rc == HPSS_ENOTDIR))
    return true;

  return false;
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

void HPSSFSAL_BuildCos(uint32_t CosId,
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

int HPSSFSAL_ucreds_from_opctx(const struct req_op_context *opctx, sec_cred_t *ucreds)
{
  int glen, i;

  if( !opctx || !ucreds )
    return ERR_FSAL_FAULT;

  /* set umask to 0, we will handle it ourselves */
  if(hpss_LoadThreadState(0, 0, NULL))
    {
      return ERR_FSAL_PERM;
    }

  /* get associated user p_cred */
  if(hpss_GetThreadUcred(ucreds))
    {
      return ERR_FSAL_PERM;
    }


  /* fill the structure */
  strcpy(ucreds->Name, "NFS.User");
  ucreds->CurAccount = ACCT_REC_DEFAULT;
  ucreds->DefAccount = ACCT_REC_DEFAULT;
  ucreds->Uid = opctx->creds->caller_uid;
  ucreds->Gid = opctx->creds->caller_gid;

  glen = opctx->creds->caller_glen;
  if(glen > HPSS_NGROUPS_MAX)
    glen = HPSS_NGROUPS_MAX;

  if((glen > 0) && (opctx->creds->caller_garray == NULL))
    return ERR_FSAL_FAULT;

  ucreds->NumGroups = glen;

  for(i = 0; i < glen; i++)
    ucreds->AltGroups[i] = opctx->creds->caller_garray[i];

  return ERR_FSAL_NO_ERROR;
} 
