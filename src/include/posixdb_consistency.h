
#ifndef _POSIXDB_CONSISTENCY_H
#define _POSIXDB_CONSISTENCY_H

/** 
 * @brief Check the consistency between two fsal_posixdb_fileinfo_t
 * 
 * @param p_info1 
 * @param p_info2
 * 
 * @return 0 if the two fsal_posixdb_fileinfo_t are consistent
 *         1 else (or on error)
 */
int fsal_posixdb_consistency_check(fsal_posixdb_fileinfo_t * p_info1,   /* IN */
                                   fsal_posixdb_fileinfo_t * p_info2 /* IN */ );

#endif
