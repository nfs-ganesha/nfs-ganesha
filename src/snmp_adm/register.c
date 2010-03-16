/**
 * \file    register.c
 * \author  Cédric CABESSA
 * \brief   register.c: registration routine
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "snmp_adm.h"

#include "instance_handler.h"

/**
 * Register a read only string
 * @param myoid oid
 * @param oid_len oid length
 * @param string string to register.
 * @param phandler output parameter, contain the netsnmp handler registration of the string.
 * @return MIB_REGISTERED_OK on success.
 */
int register_ro_string(oid * myoid, int oid_len, char *string,
                       netsnmp_handler_registration ** phandler)
{
  netsnmp_handler_registration *myreg;

  myreg =
      netsnmp_create_handler_registration("libdemon",
                                          instance_string_handler,
                                          myoid, oid_len, HANDLER_CAN_RONLY);
  myreg->handler->myvoid = (void *)string;

  *phandler = myreg;
  return netsnmp_register_handler(myreg);

}

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
             netsnmp_handler_registration * phandler_tab[2])
{
  netsnmp_handler_registration *myreg_type, *myreg_val;
        /**
	 * myoid has enough memory
	 */
  oid_len++;
  myoid[oid_len - 1] = TYPE_OID;

  /* register type */
  myreg_type = netsnmp_create_handler_registration("libdemon",
                                                   instance_string_handler,
                                                   myoid, oid_len, HANDLER_CAN_RONLY);
  /* register value and set type */
  switch (type)
    {
    case SNMP_ADM_STRING:
      myoid[oid_len - 1] = VAL_OID;
      myreg_val = netsnmp_create_handler_registration("libdemon", instance_string_handler,
                                                      myoid, oid_len, access);
      myreg_type->handler->myvoid = (void *)"STRING";

      break;

    case SNMP_ADM_INTEGER:
      myoid[oid_len - 1] = VAL_OID;
      myreg_val = netsnmp_create_handler_registration("libdemon", instance_int_handler,
                                                      myoid, oid_len, access);
      myreg_type->handler->myvoid = (void *)"INTEGER";

      break;
    case SNMP_ADM_REAL:
      myoid[oid_len - 1] = VAL_OID;
      myreg_val = netsnmp_create_handler_registration("libdemon", instance_real_handler,
                                                      myoid, oid_len, access);
      myreg_type->handler->myvoid = (void *)"REAL";

      break;
    case SNMP_ADM_BIGINT:
      myoid[oid_len - 1] = VAL_OID;
      myreg_val = netsnmp_create_handler_registration("libdemon", instance_bigint_handler,
                                                      myoid, oid_len, access);
      myreg_type->handler->myvoid = (void *)"BIGINT";
      break;
    case SNMP_ADM_TIMETICKS:
      myoid[oid_len - 1] = VAL_OID;
      myreg_val = netsnmp_create_handler_registration("libdemon", instance_time_handler,
                                                      myoid, oid_len, access);
      myreg_type->handler->myvoid = (void *)"TIMETICKS";
      break;
    case SNMP_ADM_IP:
      myoid[oid_len - 1] = VAL_OID;
      myreg_val = netsnmp_create_handler_registration("libdemon", instance_ip_handler,
                                                      myoid, oid_len, access);
      myreg_type->handler->myvoid = (void *)"IP";
      break;

    default:
      /* unknow type */
      return 1;

    }
  netsnmp_register_handler(myreg_type);

  /* set value */
  myreg_val->handler->myvoid = value;
  /* give netsnmp handlers to the calling function */
  phandler_tab[0] = myreg_val;
  phandler_tab[1] = myreg_type;

  return netsnmp_register_handler(myreg_val);
}

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
                netsnmp_handler_registration * phandler_tab[2])
{
  netsnmp_handler_registration *myreg_val, *myreg_type;

  oid_len++;

  myoid[oid_len - 1] = TYPE_OID;

  /* register type */
  myreg_type = netsnmp_create_handler_registration("libdemon",
                                                   instance_string_handler,
                                                   myoid, oid_len, HANDLER_CAN_RONLY);
  switch (type)
    {
    case SNMP_ADM_STRING:
      myreg_type->handler->myvoid = (void *)"STRING";
      break;
    case SNMP_ADM_INTEGER:
      myreg_type->handler->myvoid = (void *)"INTEGER";
      break;
    case SNMP_ADM_REAL:
      myreg_type->handler->myvoid = (void *)"REAL";
      break;
    case SNMP_ADM_BIGINT:
      myreg_type->handler->myvoid = (void *)"BIGINT";
      break;
    case SNMP_ADM_TIMETICKS:
      myreg_type->handler->myvoid = (void *)"TIMETICKS";
      break;
    case SNMP_ADM_IP:
      myreg_type->handler->myvoid = (void *)"IP";
      break;
    default:
      /* unknow type */
      return 1;

    }
  netsnmp_register_handler(myreg_type);

  /* register get/set, we do not use "myvoid", we will find the get/set in
     instance_get_set_handler */
  myoid[oid_len - 1] = VAL_OID;
  myreg_val = netsnmp_create_handler_registration("libdemon",
                                                  instance_get_set_handler,
                                                  myoid, oid_len, access);

  /* give netsnmp handlers to the calling function */
  phandler_tab[0] = myreg_type;
  phandler_tab[1] = myreg_val;

  return netsnmp_register_handler(myreg_val);
}

/**
 * Register a procedure
 * @param myoid oid
 * @param oid_len oid length
 * @param phandler output parameters, contain the netsnmp handler registration (trigger node).
 * @return MIB_REGISTERED_OK on success.
 */
int reg_proc(oid * myoid, size_t oid_len, netsnmp_handler_registration ** phandler)
{
  netsnmp_handler_registration *myreg;

  myreg = netsnmp_create_handler_registration("libdemon", instance_proc_handler,
                                              myoid, oid_len, SNMP_ADM_ACCESS_RW);
  *phandler = myreg;
  return netsnmp_register_handler(myreg);
}

int unreg_instance(netsnmp_handler_registration * myreg)
{
/** FIXME bug snmp 
    valgrind say  Invalid read of size 1
    sometime snmpd crash on exit
*/
  return netsnmp_unregister_handler(myreg);
}
