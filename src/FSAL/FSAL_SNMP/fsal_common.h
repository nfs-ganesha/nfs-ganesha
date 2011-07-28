/**
 * Common FS tools for internal use in the FSAL.
 *
 */

#ifndef FSAL_COMMON_H
#define FSAL_COMMON_H

#include "fsal.h"

/* GETBULK request options */
typedef struct getbulk_info__
{
  long non_repeaters;
  long max_repetitions;
} getbulk_info_t;

/* SET request options */
typedef struct set_info__
{
  char *value;
  char type;
} set_info_t;

/* SNMP request descriptor  */
typedef struct fsal_request_desc__
{
  int request_type;             /* the request type */

  union                         /* options depending on the request type */
  {
    getbulk_info_t getbulk_info;
    set_info_t set_info;
  } option_u;

} fsal_request_desc_t;

#define GETBULK_REQUEST_INFO option_u.getbulk_info
#define SET_REQUEST_INFO     option_u.set_info

/* usefull functions for accessing SNMP  */

/* builds the root handle  */
void BuildRootHandle(fsal_handle_t * p_hdl);

/* converts a path to an oid.
 * @todo : use SNMP error codes ?
 */
int ParseSNMPPath(char *in_path, fsal_handle_t * out_handle);

/**
 * proceed a SNMP request
 * @param p_context   the current thread SNMP session info
 * @param oid_tab     the SNMP oid on which we want to issue the request
 * @param oid_len     the SNMP oid size
 * @param p_req_desc  describes the request to be issued
 * @return SNMPERR_SUCCESS on succesfull completion
 *         or a SNMP error code if an error occurred.
 *         In case of an internal error, SNMPERR_MAX-1 is returned.
 */
int IssueSNMPQuery(fsal_op_context_t * p_context, oid * oid_tab, int oid_len,
                   fsal_request_desc_t * p_req_desc);

/**
 * It has the same behavior as get_tree in net-snmp library,
 * except that it always NULL if it didn't find.
 */
struct tree *FSAL_GetTree(oid * objid, int objidlen, struct tree *subtree,
                          int return_nearest_parent);

/**
 * Retrieve the MIB tree node associated to the FSAL handle.
 * @param p_context   the current thread SNMP session info
 * @param p_handle    the SNMP oid for the MIB node we want 
 * @return a MIB subtree whose the root node is the resquested node
 *         if it exists, NULL else.
 */
struct tree *GetMIBNode(fsal_op_context_t * p_context, fsal_handle_t * p_handle,
                        int return_nearest_parent);

/**
 * Retrieve the list of mib childs asssociated to the FSAL handle.
 * @param p_context   the current thread SNMP session info
 * @param p_handle    the SNMP oid for the MIB child list we want 
 * @return a pointer to the first child if it exists, NULL else.
 */
struct tree *GetMIBChildList(fsal_op_context_t * p_context, fsal_handle_t * p_handle);

/* test is a given oid is in the subtree whose parent_oid is the root */
int IsSNMPChild(oid * parent_oid, int parent_oid_len, oid * child_oid, int child_oid_len);

/* check (using an SNMP GETNEXT request) if the snmp object has some childs.
 * NB: the object_type_reminder handle's field is not used in this call.
 * @return 0 if it is not a parent node, 1 if it is, -1 on SNMP error.
 */
int HasSNMPChilds(fsal_op_context_t * p_context, fsal_handle_t * p_handle);

/**
 * get the next response for a GETBULK response sequence.
 * @param p_context the current request context.
 */
netsnmp_variable_list *GetNextResponse(fsal_op_context_t * p_context);

/**
 * this macro copy a full or partial oid (for a FSAL handle or cookie)
 */
#define FSAL_OID_DUP( _p_hdl_or_cookie_ , _src_oid_tab_, _oid_len_ ) do {	\
		memcpy( (_p_hdl_or_cookie_)->data.oid_tab, (_src_oid_tab_) , (_oid_len_) * sizeof(oid) ); \
		(_p_hdl_or_cookie_)->data.oid_len = _oid_len_ ;			\
	} while ( 0 )

/**
 * this macro increments the last oid of an snmp path
 */
#define FSAL_OID_INC( _p_hdl_or_cookie_ ) \
		((_p_hdl_or_cookie_)->data.oid_tab[ (_p_hdl_or_cookie_)->data.oid_len - 1 ] ++)

/**
 * this function compares the <count> first oids of the 2 SNMP paths
 * returns a negative value when oid_tab1 < oid_tab2,
 * a positive value when oid_tab1 > oid_tab2,
 * 0 if they are equal.
 */
int fsal_oid_cmp(oid * oid_tab1, oid * oid_tab2, unsigned int count);

long StrToSNMPVersion(char *str);

#endif
