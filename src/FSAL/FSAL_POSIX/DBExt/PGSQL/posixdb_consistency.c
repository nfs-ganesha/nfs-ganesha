#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "posixdb_internal.h"
#include "posixdb_consistency.h"
#include <string.h>

/** 
 * @brief Check the consistency between two fsal_posixdb_fileinfo_t
 * 
 * @param p_info1 
 * @param p_info2
 * 
 * @return 0 if the two fsal_posixdb_fileinfo_t are consistent
 *         another value else (or on error)
 */
int fsal_posixdb_consistency_check(fsal_posixdb_fileinfo_t * p_info1,   /* IN */
                                   fsal_posixdb_fileinfo_t * p_info2 /* IN */ )
{
  int out = 0;

  if(!p_info1 || !p_info2)
    return -1;

  if(isFullDebug(COMPONENT_FSAL))
    {
      if(p_info1->inode != p_info2->inode)
        LogFullDebug(COMPONENT_FSAL, "inode 1 <> inode 2 : %"PRIu64" != %"PRIu64"\n", p_info1->inode, p_info2->inode);

      if(p_info1->devid != p_info2->devid)
        LogFullDebug(COMPONENT_FSAL, "devid 1 <> devid 2 : %"PRIu64" != %"PRIu64"\n", p_info1->devid, p_info2->devid);

      if(p_info1->ftype != p_info2->ftype)
        LogFullDebug(COMPONENT_FSAL, "ftype 1 <> ftype 2 : %u != %u\n", p_info1->ftype, p_info2->ftype);
    }

  out |= (p_info1->inode && p_info2->inode) && (p_info1->inode != p_info2->inode);
  out |= (p_info1->devid && p_info2->devid) && (p_info1->devid != p_info2->devid);
  out |= (p_info1->ftype && p_info2->ftype) && (p_info1->ftype != p_info2->ftype);

  return out;
}
