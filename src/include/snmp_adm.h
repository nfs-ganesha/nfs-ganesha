/**
 * \file    snmp_adm.h
 * \author  Cedric CABESSA
 * \brief   snmp_adm.h : snmp_adm API
 *
 */

#ifndef __LIBDAEMON_H__
#define __LIBDAEMON_H__

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

/** Read only value */
#define SNMP_ADM_ACCESS_RO   HANDLER_CAN_RONLY
/** Read write value */
#define SNMP_ADM_ACCESS_RW   HANDLER_CAN_RWRITE

enum
{
  NAME_OID,
  DESC_OID,
  VAR_OID
};

/** A var is made of this two */
enum
{
  TYPE_OID,       /**< contain a string with the type */
  VAL_OID         /**< contain the value */
};

enum
{
  STAT_OID,
  LOG_OID,
#ifdef _ERROR_INJECTION
  INJECT_OID,
#endif
  CONF_OID,
  PROC_OID,
  NUM_BRANCH
};

/** Enum of available type number */
enum type_number
{
  SNMP_ADM_INTEGER,       /**< 32 bits integer */
  SNMP_ADM_STRING,        /**< null terminated */
  SNMP_ADM_IP,            /**< Ip address (4 octets in network byte-order) */
  SNMP_ADM_REAL,          /**< 64 bits floating point (double) */
  SNMP_ADM_BIGINT,        /**< 64 bits integer */
  SNMP_ADM_TIMETICKS      /**< Match ASN_TIMETICKS meaning hundredths of a second since some epoch (not EPOCH).
			     It is encoded in a unsigned int, so time range is quite short.
			     You should use description field to identifies the reference epoch.
			     
			     WARNING As people use second in their code, values are converted in second.
			     The value pointed for a register have to be in second (it is probably what you already use), but the
			     set request have to be in 1/100s (as SNMP says)			     
			    */
};

/** OID number for trigger. */
#define TRIGGER_OID   3
/** OID number for inputs. */
#define INPUT_OID     0
/** OID number for outputs. */
#define OUTPUT_OID    1

/**
 * The different states of the trigger branch of a procedure.
 * ROOT.prodid.PROC_OID.numproc.TRIGGER_OID.
 */
enum trigger_state
{
  SNMP_ADM_READY,              /**< A set (whatever the value) will call the procedure. */
  SNMP_ADM_PROGRESS,           /**< Procedure not terminated. Cannot set trigger or inputs */
  SNMP_ADM_DONE,               /**< Procedure terminated with success, user can read values.
				  User must set the value to 0 to pass in READY state.
				  Other values are ignored.
				  Inputs cannot be set.
			       */
  SNMP_ADM_ERROR               /**< like DONE but procedure was terminated with error */
};

/** Maximum length for a string */
#define SNMP_ADM_MAX_STR    4096

/**
 * The type of variables handle by the library.
 */
typedef union type_union_e
{
  int integer;                          /**< SNMP_ADM_INTEGER */
  char string[SNMP_ADM_MAX_STR];        /**< SNMP_ADM_STRING */
  in_addr_t ip;                         /**< SNMP_ADM_IP */
  double real;                          /**< SNMP_ADM_REAL */
  int64_t bigint;                       /**< SNMP_ADM_BIGINT */
  unsigned int time;                    /**< SNMP_ADM_TIMETICKS */
} snmp_adm_type_union;

/**
 * Scalar information.
 */
typedef struct register_scal_s
{
  char *label;                         /**< The variable's name */
  char *desc;                          /**< A useful description */
  unsigned char type;                  /**< The value's type @see type_number */
  int access;                          /**< Access right SNMP_ADM_ACCESS_RO or SNMP_ADM_ACCESS_RW */
  void *value;                         /**< Pointer on the scalar */
} register_scal;

/**
 * A getter.
 * @param param the function have to fill the value.
 * @param opt_arg optionnal argument, the optional argument set during registration is available here.
 * @see register_get_set_s
 * @return value should be 0 on success.
 */
typedef int (*fct_get) (snmp_adm_type_union * param, void *opt_arg);

/**
 * A setter.
 * @param param the function have to read this value to change internal data.
 * @param opt_arg optionnal argument, the optional argument set during registration is available here.
 * @see register_get_set_s
 * @return value should be 0 on success.
 */
typedef int (*fct_set) (const snmp_adm_type_union * param, void *opt_arg);

/**
 * get/set information.
 */
typedef struct register_get_set_s
{
  char *label;                    /**< The variable's name */
  char *desc;                     /**< A useful description */
  unsigned char type;             /**< The value's type @see type_number */
  int access;                     /**< Access right SNMP_ADM_ACCESS_RO or SNMP_ADM_ACCESS_RW */
  fct_get getter;                 /**< getter function @see fct_get */
  fct_set setter;                 /**< getter function @see fct_set */
  void *opt_arg;                  /**< optional argument (could be NULL) */
} register_get_set;

/**
 * A procedure.
 * @param tab_in Array of inputs. The length is set during registration.
 * @param tab_in Array of outputs. The length is set during registration.
 * @param opt_arg optionnal argument, the optional argument set during registration is available here.
 * @see register_get_set_s
 * @return value should be 0 on success.
 */
typedef int (*proc) (const snmp_adm_type_union ** tab_in, snmp_adm_type_union ** tab_out,
                     void *opt_arg);

/**
 * Procedure information.
 */
typedef struct register_proc_s
{
  char *label;                    /**< The variable's name */
  char *desc;                     /**< A useful description */
  int nb_in;                      /**< Number of inputs values */
  unsigned char *type_in;         /**< Array of input type. @see type_number */
  int nb_out;                     /**< Number of outputs values */
  unsigned char *type_out;        /**< Array of output type */
  void *opt_arg;                  /**< Optional argument, could be null*/
  proc myproc;                    /**< Pointer on the procedure @see proc */
} register_proc;

/**
 * A trap testing function.
 * @param arg argument of the function. @see snmp_adm_register_poll_trap.
 * @return if not 0, the trap is sent.
 */
typedef int (*trap_test) (void *arg);

/**
 * Configure daemon.
 * Should be called before registering values.
 * @param agent_x_socket agentX socket name (eg:"/tmp/agentx" or "tcp:192.168.67.19:31415").
 * This parametre should be set according to the snmpd config.
 * @param prod_id product id, unique identifier of this daemon.
 * @param filelog file to record log messages or "syslog".
 * @return 0 on success. 
 */
int snmp_adm_config_daemon(char *agent_x_socket, char *filelog, int prod_id);

/**
 * Register scalars.
 * Note for strings : string provided in code MUST be readonly (ACCESS_RO).
 *                    string MUST be allocated with SNMP_ADM_MAX_STR.
 * Note for label and desc in snmp_adm_register_* : they are copied in the 
 * library own  memory, so user can safely free them or reuse them 
 * after this call.
 * @param branch the branch number (STAT_OID or CONF_OID).
 * @param tab the values to register.
 * @param len the array's length.
 * @return 0 on success.
 */
int snmp_adm_register_scalars(int branch, register_scal * tab, int len);

/**
 * Register get/set functions.
 * Note for label and desc in snmp_adm_register_* : they are copied in the 
 * library own  memory, so user can safely free them or reuse them 
 * after this call.
 * @param branch the branch number (STAT_OID or CONF_OID).
 * @param tab the functions to register.
 * @param len the array's length.
 * @return 0 on success.
 */
int snmp_adm_register_get_set_function(int branch, register_get_set * tab, int len);

/**
 * Register procedures.
 * Note for label and desc in snmp_adm_register_* : they are copied in the 
 * library own  memory, so user can safely free them or reuse them 
 * after this call.
 * @param tab the procedures to register.
 * @param len the array's length.
 * @return 0 on success.
 */
int snmp_adm_register_procedure(register_proc * tab, int len);

/**
 * Unregister an instance.
 * @param label name of the instance.
 * @return 0 on success.
 */
int snmp_adm_unregister(char *label);

/**
 * Send a SNMPv2 trap.
 * @param type type of the variable.
 * @param value value of the variable.
 * @return 0 on success.
 */
void snmp_adm_send_trap(unsigned char type, snmp_adm_type_union value);

/**
 * Register a polling fonction.
 * @param second polling time.
 * @param test_fct fonction called each "second", send a trap if return 1.
 * @param args arguments of test_fct.
 * @param type type of the variable.
 * @param value value of the variable.
 * @return 0 on success.
 */
int snmp_adm_register_poll_trap(unsigned int second, trap_test test_fct, void *args,
                                unsigned char type, snmp_adm_type_union value);

/**
 * Launch the thread.
 * @return 1: nothing was recorded.
 *         2: daemon not configured, or problem with snmpd.
 *         3: cannot create the thread.
 * @return 0 on success.
 */
int snmp_adm_start();

/**
 * Close the snmp thread.
 */
void snmp_adm_close();

/**
 * Log a message.
 * @see init_daemon.
 */
void snmp_adm_log(char *format, ...);

#endif                          /* __LIBDAEMON_H__ */
