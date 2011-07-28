/**
 * \file    register.h
 * \author  Cédric CABESSA
 * \brief   register.h : registration functions
 *
 */

#ifndef __REGISTER_H__
#define __REGISTER_H__

#include "snmp_adm.h"

/**
 * Register a read only string
 * @param myoid oid
 * @param oid_len oid length
 * @param string string to register.
 * @param phandler output parameter, contain the netsnmp handler registration of the string.
 * @return MIB_REGISTERED_OK on success.
 */
int register_ro_string(oid * myoid, size_t oid_len, char *string,
                       netsnmp_handler_registration ** phandler);

/**
 * Register scalar variable by pointer.
 * we actually register two thing : the type and the value.
 * @param myoid oid
 * @param oid_len oid length
 * @param value pointer on the value to register.
 * @param type type of the value. @see type_number.
 * @param access access right.
 * @param phandler_tab output parameters, contain the netsnmp handlers registration of the value and type.
 * @return MIB_REGISTERED_OK on success.
 */
int reg_scal(oid * myoid, size_t oid_len, void *value, unsigned char type, int access,
             netsnmp_handler_registration * phandler_tab[2]);

/**
 * Register scalar variable by getter setter.
 * we actually register two thing : the type and the value.
 * @param myoid oid
 * @param oid_len oid length
 * @param type type of the value. @see type_number.
 * @param access access right.
 * @param phandler_tab output parameters, contain the netsnmp handlers registration of the value and type.
 * @return MIB_REGISTERED_OK on success.
 */
int reg_get_set(oid * myoid, size_t oid_len, unsigned char type, int access,
                netsnmp_handler_registration * phandler[2]);

/**
 * Register a procedure
 * @param myoid oid
 * @param oid_len oid length
 * @param phandler output parameters, contain the netsnmp handler registration (trigger node).
 * @return MIB_REGISTERED_OK on success.
 */
int reg_proc(oid * myoid, size_t oid_len, netsnmp_handler_registration ** phandler);

int unreg_instance(netsnmp_handler_registration * myreg);

#endif                          /* __REGISTER_H__ */
