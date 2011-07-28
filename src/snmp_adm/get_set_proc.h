/**
 * \file    get_set_proc.h
 * \author  Cédric CABESSA
 * \brief   get_set_proc.h : internal structures for registration and unregistration.
 *
 */

#ifndef __GET_SET_PROC_H__
#define __GET_SET_PROC_H__

#include "snmp_adm.h"

/**
 * Information about get/set function.
 * Used for registration.
 */
typedef struct get_set_info_s
{
  fct_get getter;                 /**< @see fct_get */
  fct_set setter;                 /**< @see fct_set */
  int branch;                     /**< conf or stat */
  int num;                        /**< numstat or numconf */
  unsigned char type;             /**< @see type_number */
  void *opt_arg;                  /**< @see register_get_set */
} get_set_info;

/**
 * Information about procedure.
 * Used for registration.
 */
typedef struct proc_info_s
{
  int num;                              /**< numproc */
  int nb_in;                            /**< number of inputs */
  int nb_out;                           /**< number of outputs */
  snmp_adm_type_union **inputs;         /**< array of inputs */
  snmp_adm_type_union **outputs;        /**< array of outputs */
  void *opt_arg;                        /**< optional arguments */
  proc myproc;                          /**< procedure @see proc */
  int trigger;                          /**< trigger state @see enum trigger_state */
} proc_info;

/**
 * Keep track of recorded object.
 * It is usefull for unregistration and to call functions on request.
 * We make a linked list of all recorded objects.
 */
typedef struct register_info_s
{
        /** label, it is the research key for unregistration */
  char *label;
        /** we save the description in our own memory */
  char *desc;
        /** if we are a procedure or a get/set, we need to know where is our functions 
	    to call them on request.
	    Pointer are NULL if scalar.
	*/
  union function_info_u
  {
    proc_info *proc;
    get_set_info *get_set;
  } function_info;

        /** number in the nested union (type_e) */
  int type;
        /** union of type */
  enum type_e
  {
    SCAL,
    GET_SET,
    PROC
  } type_enum;
        /** array of Net-SNMP register, used for unregister*/
  netsnmp_handler_registration **reg;
        /** length of Net-SNMP register's array */
  int reg_len;

        /** next recorded object */
  struct register_info_s *next;
} register_info;

/**
 * Used by polling thread.
 */
typedef struct polling_arg_s
{
  unsigned int second;               /**< polling period*/
  trap_test test_fct;                /**< test function */
  unsigned char type;                /**< variable type */
  snmp_adm_type_union value;         /**< variable sent in the trap */
  void *args;                        /**< arguments */
} polling_arg;

#endif                          /* __GET_SET_PROC_H__ */
