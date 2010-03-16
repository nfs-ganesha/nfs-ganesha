/**
 *
 * \file    fsal_convert.h
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/27 14:15:57 $
 * \version $Revision: 1.13 $
 * \brief   Your FS to FSAL type converting function.
 *
 */
#ifndef _FSAL_CONVERTION_H
#define _FSAL_CONVERTION_H

#include "fsal.h"

/* convert a slash separated path to a dot separated path */
void PosixPath2SNMP(char *in_path, char *out_path);

/* convert a snmp error code to an FSAL code */
int snmp2fsal_error(int snmp_error);

/* extract the object handle from the variable info.
 */
int snmp_object2handle(netsnmp_variable_list * p_in_var, fsal_handle_t * p_out_handle);

/* extract object name from the mib tree node (when available)
 * else, it returns the string representation of the object subid
 * or the last oid in the handle.
 */
int snmp_object2name(netsnmp_variable_list * p_in_var, struct tree *p_in_node,
                     fsal_handle_t * p_handle, fsal_name_t * p_out_name);

/* print the object value to a string, and return its length (the buffer ends with '\n\0', like /proc files)  */
int snmp_object2str(netsnmp_variable_list * p_in_var, char *p_out_string,
                    size_t * in_out_len);

/* convert SNMP object's access rights to the associated FSAL mode (if mib info is available).
 * else, it returns 666 or 555, depending on the node type.
 * /!\ the p_in_node is the PARENT NODE (access right are stored in the parent node)
 */
fsal_accessmode_t snmp_object2access_mode(nodetype_t obj_type, struct tree *p_in_node);

/* convert the FSAL internal node type (root, node, leaf) to a classical FSAL nodetype (regular file, directory, ...)
 */
fsal_nodetype_t intern2extern_type(nodetype_t internal_type);

/* compute the object id from the handle */
fsal_u64_t build_object_id(fsal_handle_t * p_in_handle);

/* fill the p_fsalattr_out structure depending on the given information.
 * /!\ the p_in_node is the PARENT NODE (access right are stored in the parent node)
 */
int snmp2fsal_attributes(fsal_handle_t * p_handle, netsnmp_variable_list * p_var,
                         struct tree *p_in_node, fsal_attrib_list_t * p_fsalattr_out);

/* return the type for snmp_add_var, given the associated ASN_xxx type */
char ASN2add_var(u_char asn_type);

/* some ideas of conversion functions...

int fsal2fs_openflags( fsal_openflags_t fsal_flags, int * p_fs_flags );

int fsal2fs_testperm(fsal_accessflags_t testperm);

void fsal2fs_mode( fsal_accessmode_t fsal_mode, <your fs mode type (output)> );

fsal_u64_t fs2fsal_64( <your fs 64bits type> );

<your fs 64bits type> fsal2hpss_64( fsal_u64_t fsal_size_in );

fsal_fsid_t fs2fsal_fsid( <you fs fsid type> );

fsal_status_t fsal2fs_attribset( fsal_handle_t  * p_fsal_handle,
                                 fsal_attrib_list_t  * p_attrib_set ,
                                <depends on your fs way of setting attributes> );

fsal_time_t fs2fsal_time( <your fs time structure> );

<your fs time structure> fsal2fs_time(fsal_time_t in_time);
     
*/

#endif
