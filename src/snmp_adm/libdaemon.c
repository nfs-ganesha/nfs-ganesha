/**
 * \file    libdaemon.c
 * \author  Cédric CABESSA
 * \brief   libdaemon.c : snmp_adm API
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include <pthread.h>
#include <unistd.h>

#include "snmp_adm.h"

#include "register.h"

#include "get_set_proc.h"
#include "parse_type.h"
#include "config_daemon.h"

static pthread_t thread_id = 0;

static int running = 1;

static int configured = 0;
static int registered = 0;

static int product_id = 0;

static int issyslog = 0;

/** linked list of registred objects */
register_info *register_info_list = NULL;
/** array of thread id for polling */
pthread_t *polling_threads = NULL;
/** array of polling arg @see polling_arg  */
polling_arg *polling_args = NULL;
/** sizeof polling_thread and polling_args */
int polling_list_size = 0;

oid *root_oid = NULL;
int root_oid_len = 0;

static void *pool(void *v)
{
  while(running)
    {
      agent_check_and_process(1);       /* 0 == don't block */
    }
  return NULL;
}

/**
 * Internal polling
 * FIXME one thread by polling function alarm?
 */
static void *polling_fct(void *arg)
{
  polling_arg *parg = (polling_arg *) arg;

  for(;;)
    {
      if(parg->test_fct(parg->args) == 1)
        snmp_adm_send_trap(parg->type, parg->value);
      sleep(parg->second);
    }
  return NULL;
}

/*get oid from environment variable SNMP_ADM_ROOT
  syntax is .1.3.6.1.4.1.12384.999
*/
static int get_conf_from_env()
{
  char *str_root = getenv("SNMP_ADM_ROOT");
  char *ptr, *save;
  int pos = 0;

  if(!str_root)
    {
      /*no environment var, use default */
      oid tmp_root[] = { DEFAULT_ROOT_OID };
      root_oid_len = sizeof(tmp_root) / sizeof(oid);
      root_oid = malloc(root_oid_len * sizeof(oid));
      memcpy(root_oid, tmp_root, root_oid_len * sizeof(oid));
    }
  else
    {
      /* parse str_root
       */

      /* compute length */
      for(ptr = str_root; *ptr != '\0'; ptr++)
        if(*ptr == '.')
          root_oid_len++;

      root_oid = malloc(root_oid_len * sizeof(oid));

      /* create root array */
      save = str_root + 1;
      for(ptr = str_root + 1; *ptr != '\0'; ptr++)
        if(*ptr == '.' || ptr == '\0')
          {
            *ptr = '\0';
            /*FIXME arch spec? */
            root_oid[pos++] = atol(save);
            save = ++ptr;
          }
      root_oid[pos++] = atol(save);
    }
  return 0;
}

/**
 * Structure about "useful node" in the tree (a stat, a conf or a proc).
 * @param label name of the node, it is the research key used for unregistration
 * @param type SCAL,CONF or PROC.
 * @param reg_len How many node I will record in the tree.
 * return a linked cell in the list of registred object.
 */
static register_info *new_register(char *label, char *desc, int type, int reg_len)
{
  register_info *new = malloc(sizeof(register_info));

  /*fill the struct */
  new->label = strdup(label);
  new->desc = strdup(desc);

  /* set function to NULL (avoid undetermined value if scalar) */
  memset(&(new->function_info), 0, sizeof(new->function_info));

  new->type = type;

  new->reg = malloc(reg_len * sizeof(netsnmp_handler_registration *));
  new->reg_len = reg_len;

  /* link the cell (insert in first) */
  new->next = register_info_list;
  register_info_list = new;

  return new;
}

/*
  register desc and name
 */
static int register_meta(oid * myoid, int len, char *name, char *desc,
                         netsnmp_handler_registration ** tab_reg)
{
  int err1, err2;

  /* description */
  myoid[len - 1] = DESC_OID;
  err1 = register_ro_string(myoid, len, desc, &(tab_reg[0]));

  /* label */
  myoid[len - 1] = NAME_OID;
  err2 = register_ro_string(myoid, len, name, &(tab_reg[1]));

  return err1 != MIB_REGISTERED_OK || err2 != MIB_REGISTERED_OK;
}

/* id of the object, incremented after each record */
int branch_num[NUM_BRANCH];

/* fill a oid with root|prod_id|stat_num
 * len value is the oid length from root to the name|desc level +1.
 */
static void get_oid(oid * myoid, int branch, size_t * plen)
{
  *plen = root_oid_len;
  memcpy(myoid, root_oid, sizeof(oid) * root_oid_len);

  myoid[(*plen)++] = product_id;
  myoid[(*plen)++] = branch;
  myoid[(*plen)++] = branch_num[branch]++;
  (*plen)++;

/* output : oid=$(ROOT).prodId.stat.branch_num[branch].***.***.***
                 <-------------- len ---------->
		 <---------- MAX_OID_LEN ---------->
*/
}

static const char *branch_to_str(int branch)
{
  static char bstr[12];
  switch(branch)
    {
      case STAT_OID:   return "stat";
      case LOG_OID:    return "log";
#ifdef _ERROR_INJECTION
      case INJECT_OID: return "inject";
#endif
      case CONF_OID:   return "conf";
      case PROC_OID:   return "proc";
      default:
        snprintf(bstr, sizeof(bstr), "%d", branch);
        return bstr;
     }
}

/* register a scalar in the tree */
static int register_scal_instance(int type, const register_scal * instance)
{
  int err1, err2 = 0;
  register_info *info;

  oid myoid[MAX_OID_LEN];
  size_t len;

  if(instance->value != NULL)
    {
      /* create a register object of scalar type.
         we know we need 4 netsnmp register objects to record a scalar
       */
      info = new_register(instance->label, instance->desc, SCAL, 4);    /* name+desc+type+value = 4 */

      get_oid(myoid, type, &len);

      /* This function will register two value in the tree (name and desc)
       */
      err1 = register_meta(myoid, len, info->label, info->desc, info->reg);

      /* value */
      myoid[len - 1] = VAR_OID;
      /* register a scalar in the tree
         we use the last two netsnmp register (type and value)
       */
      err2 = reg_scal(myoid, len, instance->value, instance->type, instance->access, info->reg + 2);    /* name+desc offset */
    }
  else
    {
      err1 = 1;
      snmp_adm_log("Cannot register NULL value for \"%s\"", instance->label);
    }

  return err1 || err2;
}

/* register a getset in the tree */
static int register_get_set_instance(int branch, const register_get_set * instance)
{
  int err1, err2 = 0;
  register_info *info;
  get_set_info *gs_info;

  oid myoid[MAX_OID_LEN];
  size_t len;

  if(instance->getter != NULL &&
     !(instance->access == SNMP_ADM_ACCESS_RW && instance->setter == NULL))
    {

      get_oid(myoid, branch, &len);
      /* create a register object of get/set type.
         we know we need 4 netsnmp register objects to record a get/set
       */
      info = new_register(instance->label, instance->desc, GET_SET, 4); /* name+desc+type+value = 4 */

      /* add get/set specific information on register */
      gs_info = info->function_info.get_set = malloc(sizeof(get_set_info));

      gs_info->getter = instance->getter;
      gs_info->setter = instance->setter;
      gs_info->type = instance->type;
      gs_info->branch = branch;
      gs_info->num = myoid[len - 2];
      gs_info->opt_arg = instance->opt_arg;

      /* This function will register two value in the tree (name and desc)
       */
      err1 = register_meta(myoid, len, info->label, info->desc, info->reg);
      /* value */
      myoid[len - 1] = VAR_OID;
      /* register a get/set in the tree
         we use the last two netsnmp register (type and value)
       */
      err2 = reg_get_set(myoid, len, instance->type, instance->access, info->reg + 2);  /* name+desc offset */
    }
  else
    {
      err1 = 1;
      snmp_adm_log("Cannot register NULL function for \"%s\"", instance->label);
    }

  return err1 || err2;
}

/* register a proc in the tree */
static int register_proc_instance(const register_proc * instance)
{
  int err1, err2;
  int i, j;
  oid myoid[MAX_OID_LEN];
  size_t len;
  register_info *info;
  proc_info *p_info;
  int tab_reg_offset = 0;

  if(instance->myproc == NULL)
    {
      snmp_adm_log("Cannot register NULL procedure for \"%s\"", instance->label);
      return 1;
    }

  get_oid(myoid, PROC_OID, &len);

  /* create a register object of proc type.
     we know we need 2 + 1  (name,desc + trigger)
     + n_in  * types + n_in  * val
     + n_out * types + n_out * val
     netsnmp register objects to record a proc
     This function will register the first and the second (name and desc)
   */
  info = new_register(instance->label, instance->desc, PROC,
                      3 + 2 * (instance->nb_in + instance->nb_out));

  /* add register specific info */
  p_info = info->function_info.proc = malloc(sizeof(proc_info));

  p_info->num = myoid[len - 2];
  p_info->nb_in = instance->nb_in;
  p_info->nb_out = instance->nb_out;
  p_info->myproc = instance->myproc;
  p_info->trigger = 0;
  p_info->opt_arg = instance->opt_arg;

  /* input */
  p_info->inputs = malloc(p_info->nb_in * sizeof(snmp_adm_type_union *));
  for(j = 0; j < p_info->nb_in; j++)
    p_info->inputs[j] = calloc(1, sizeof(snmp_adm_type_union));

  /* output */
  p_info->outputs = malloc(p_info->nb_out * sizeof(snmp_adm_type_union *));
  for(j = 0; j < p_info->nb_out; j++)
    p_info->outputs[j] = calloc(1, sizeof(snmp_adm_type_union));

  /* This function will register two value in the tree (name and desc)
   */
  err1 = register_meta(myoid, len, info->label, info->desc, info->reg);
  tab_reg_offset += 2;

  myoid[len - 1] = TRIGGER_OID;
  err2 = reg_proc(myoid, len, info->reg + tab_reg_offset);      /* register the instance and save info  */
  tab_reg_offset++;

  /*
   *  register values
   */
  /* we need a longer tree */
  len += 2;
  myoid[len - 3] = VAR_OID;

  /* values are like scalars */
  /* input */
  for(i = 0; i < instance->nb_in; i++)
    {
      myoid[len - 2] = INPUT_OID;
      myoid[len - 1] = i;
      reg_scal(myoid, len, p_info->inputs[i]->string, instance->type_in[i],
               SNMP_ADM_ACCESS_RW, info->reg + tab_reg_offset);
      tab_reg_offset += 2;
    }
  /* output */
  for(i = 0; i < instance->nb_out; i++)
    {
      myoid[len - 2] = OUTPUT_OID;
      myoid[len - 1] = i;
      reg_scal(myoid, len, p_info->outputs[i]->string, instance->type_out[i],
               SNMP_ADM_ACCESS_RO, info->reg + tab_reg_offset);
      tab_reg_offset += 2;
    }
  return err1 || err2;
}

static void free_register_info(register_info * ptr)
{
  int i;
  proc_info *pinfo;
  get_set_info *gsinfo;

  free(ptr->label);
  free(ptr->desc);
  free(ptr->reg);
  ptr->reg = NULL;

  if(ptr->type == PROC)
    {
      pinfo = ptr->function_info.proc;
      for(i = 0; i < pinfo->nb_in; i++)
        free(pinfo->inputs[i]);

      free(pinfo->inputs);
      pinfo->inputs = NULL;

      for(i = 0; i < pinfo->nb_out; i++)
        free(pinfo->outputs[i]);

      free(pinfo->outputs);
      pinfo->outputs = NULL;

      free(pinfo);
    }
  else if(ptr->type == GET_SET)
    {
      gsinfo = ptr->function_info.get_set;
      free(gsinfo);
    }
}

/**
 * Configure daemon.
 * Should be called before registering values.
 * @param agent_x_socket agentX socket name (eg:"/tmp/agentx/master" or "tcp:192.168.67.19:31415").
 * This parametre should be set according to the snmpd config.
 * @param prod_id product id, unique identifier of this daemon.
 * @param filelog file to record log messages or "syslog".
 * @return 0 on success.
 */
int snmp_adm_config_daemon(char *agent_x_socket, char *filelog, int prod_id)
{
  int err_init, err_root;

  product_id = prod_id;

  /* make us a agentx client. */
  netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_AGENT_ROLE, 1);
  netsnmp_ds_set_string(NETSNMP_DS_APPLICATION_ID,
                        NETSNMP_DS_AGENT_X_SOCKET, agent_x_socket);

  /* error logging */
  if(strncmp(filelog, "syslog", 10) == 0)
    {
      snmp_enable_syslog();
      issyslog = 1;
    }
  else
    snmp_enable_filelog(filelog, 1);    /* 1:append 0:write */

  if(get_conf_from_env() == 0)
    {
      int i, len = 1024, write = 0, offset = 0;
      char buf[1024];
      for(i = 0; i < root_oid_len; i++)
        {
          offset += write;
          len -= write;
          write = snprintf(buf + offset, len, ".%ld", root_oid[i]);
        }
      snmp_adm_log("ROOT_OID=%s", buf);
      err_root = 0;
    }
  else
    {
      snmp_adm_log("cannot find a valid ROOT_OID");
      err_root = 1;
      configured = 0;
      return 1;
    }

  /* initialize the agent library */
  err_init = init_agent("libdaemon");
  init_snmp("libdaemon");

  if(!err_init)
    configured = 1;
  return err_init;
}

/**
 * Register scalars.
 * Note for strings : string provided in code MUST be readonly (ACCESS_RO).
 *                    string MUST be allocated with SNMP_ADM_MAX_STR.
 * Note for label & desc in register_scal* : they are copied in the library own
 * memory, so user can safely free them or reuse them after this call.
 * @param branch the branch number (STAT_OID or CONF_OID).
 * @param tab the values to register.
 * @param len the array's length.
 * @return 0 on success.
 */
int snmp_adm_register_scalars(int branch, register_scal * tab, int len)
{
  int i, err;

  for(i = 0; i < len; i++)
    {
      /* register the instance and save info  */
      err = register_scal_instance(branch, &(tab[i]));
      if(err)
        {
          snmp_adm_log("ERROR registering %s %s",
                       branch_to_str(branch), tab[i].label);
          return 1;
        }
      else
        snmp_adm_log("register %s %s", branch_to_str(branch), tab[i].label);
    }
  registered = 1;
  return 0;
}

/**
 * Register get/set functions.
 * Note for label & desc in register_scal* : they are copied in the library own
 * memory, so user can safely free them or reuse them after this call.
 * @param branch the branch number (STAT_OID or CONF_OID).
 * @param tab the functions to register.
 * @param len the array's length.
 * @return 0 on success.
 */
int snmp_adm_register_get_set_function(int branch, register_get_set * tab, int len)
{
  int i, err;

  for(i = 0; i < len; i++)
    {
      /* register the instance and save info  */
      err = register_get_set_instance(branch, &(tab[i]));
      if(err)
        {
          snmp_adm_log("ERROR registering getset %s %s",
                       branch_to_str(branch), tab[i].label);
          return 1;
        }
      else
        snmp_adm_log("register getset %s %s", branch_to_str(branch),
                     tab[i].label);

    }
  registered = 1;

  return 0;
}

/**
 * Register procedures.
 * Note for label & desc in register_scal* : they are copied in the library own
 * memory, so user can safely free them or reuse them after this call.
 * @param tab the procedures to register.
 * @param len the array's length.
 * @return 0 on success.
 */
int snmp_adm_register_procedure(register_proc * tab, int len)
{
  int i, err;

  for(i = 0; i < len; i++)
    {
      err = register_proc_instance(&(tab[i]));
      if(err)
        {
          snmp_adm_log("register proc %s", tab[i].label);
          return 1;
        }
      else
        snmp_adm_log("register proc %s", tab[i].label);

    }
  registered = 1;
  return 0;
}

/**
 * Unregister an instance.
 * @param label name of the instance.
 * @return 0 on success.
 */
int snmp_adm_unregister(char *label)
{
  register_info *prev, *ptr;
  int i;

  prev = register_info_list;

  for(ptr = register_info_list; ptr; ptr = ptr->next)
    {
      if(strncmp(ptr->label, label, strlen(label)) == 0)
        break;
      prev = ptr;
    }

  if(ptr)
    {
      /* unlink */
      prev->next = ptr->next;
      /* unreg */
      for(i = 0; i < ptr->reg_len; i++)
        if(unreg_instance(ptr->reg[i]) != MIB_UNREGISTERED_OK)
          return 1;
      /* free */
      free_register_info(ptr);

      return 0;
    }
  return 2;
}

/**
 * Send a SNMPv2 trap.
 * @param type type of the variable.
 * @param value value of the variable.
 */
void snmp_adm_send_trap(unsigned char type, snmp_adm_type_union value)
{
  char str[256];
  int err = 0;
  struct variable_list vars;    /* NetSNMP type */

  /* trap oid==root_oid.999 */
  int len = root_oid_len + 1;
  oid trap[root_oid_len + 1];

  memcpy(trap, root_oid, root_oid_len * sizeof(oid));
  trap[root_oid_len] = 999;

  vars.next_variable = NULL;
  vars.name = trap;
  vars.name_length = len;

  switch (type)
    {
    case SNMP_ADM_INTEGER:
      vars.val.integer = (long *)&(value.integer);
      vars.val_len = sizeof(value.integer);
      vars.type = ASN_INTEGER;
      break;
    case SNMP_ADM_STRING:
      vars.val.string = (unsigned char *)&(value.string);
      vars.val_len = strlen(value.string);
      vars.type = ASN_OCTET_STR;
      break;
    case SNMP_ADM_REAL:
      err = real2str(str, value.real);
      vars.val.string = str;
      vars.val_len = strlen(str);
      vars.type = ASN_OCTET_STR;
      break;
    case SNMP_ADM_BIGINT:
      err = big2str(str, value.real);
      vars.val.string = str;
      vars.val_len = strlen(str);
      vars.type = ASN_OCTET_STR;
      break;
    }
  if(!err)
    send_trap_vars(6, 0, &vars);
}

/**
 * Register a polling fonction.
 * @param second polling time.
 * @param test_fct fonction called each "second", send a trap if return 1.
 * @param args arguments of test_fct.
 * @param type type of the variable.
 * @param value value of the variable.
 */
int snmp_adm_register_poll_trap(unsigned int second, trap_test test_fct, void *args,
                                unsigned char type, snmp_adm_type_union value)
{
  static int capacity = 10;

  if(polling_list_size == 0)
    {
      if( ( polling_threads = malloc(capacity * sizeof(pthread_t)) ) == NULL )
        return -1 ;

      if( ( polling_args = malloc(capacity * sizeof(polling_arg)) ) == NULL )
       {
         free( polling_threads ) ;
         return -1 ;
       }
    }
  if(polling_list_size >= capacity - 1)
    {
      capacity *= 2;

      if ( ( polling_threads = realloc(polling_threads, sizeof(pthread_t) * capacity) ) == NULL )
      {
	free( polling_args ) ;
        free( polling_threads ) ;
        return -1 ;
      }
      if( ( polling_args = realloc(polling_args, sizeof(polling_arg) * capacity) ) == NULL )
      {
	free( polling_args ) ;
        free( polling_threads ) ;
	return -1 ;
      }
    }

  polling_args[polling_list_size].second = second;
  polling_args[polling_list_size].test_fct = test_fct;
  polling_args[polling_list_size].type = type;
  polling_args[polling_list_size].value = value;
  polling_args[polling_list_size].args = args;

  pthread_create(&polling_threads[polling_list_size], NULL, polling_fct,
                 &polling_args[polling_list_size]);
  polling_list_size++;

  return 0;
}

/**
 * Close the snmp thread.
 */
void snmp_adm_close()
{
  int err, i;
  register_info *ptr, *next;

  running = 0;

  if(thread_id)
    {
      err = pthread_cancel(thread_id);
      if(!err)
        pthread_join(thread_id, NULL);
    }
  for(ptr = register_info_list; ptr;)
    {
      next = ptr->next;

      for(i = 0; i < ptr->reg_len; i++)
        unreg_instance(ptr->reg[i]);
      free_register_info(ptr);
      free(ptr);

      ptr = next;
    }
  register_info_list = NULL;

  for(i = 0; i < polling_list_size; i++)
    {
      err = pthread_cancel(polling_threads[i]);
      if(!err)
        pthread_join(polling_threads[i], NULL);
    }
  free(polling_threads);
  free(polling_args);

  polling_threads = NULL;
  polling_args = NULL;

  if(root_oid)
    free(root_oid);

  snmp_adm_log("terminated");
  snmp_shutdown("libdaemon");
}

/**
 * Launch the thread.
 * FIXME after this, nothing should be registered, net-snmp bug?
 * @return 1: nothing was recorded.
 *         2: daemon not configured, or problem with snmpd.
 *         3: cannot create the thread.
 */
int snmp_adm_start()
{
  int err;

  if(registered == 0)
    {
      snmp_adm_log("Warning nothing has been registered !");
      return 1;
    }
  if(configured == 0)
    {
      snmp_adm_log("Warning snmp is not configured !\t"
                   "Did you called config_daemon? snmpd is running?");
      return 2;
    }

  err = pthread_create(&thread_id, NULL, pool, NULL);
  if(err)
    {
      snmp_adm_log("cannot create thread");
      snmp_adm_close();
      return 3;
    }
  snmp_adm_log("started");
  return 0;
}

/**
 * Log a message.
 * @see init_daemon.
 */
void snmp_adm_log(char *format, ...)
{
  va_list pa;
  char msg_buf[256];
  struct tm the_date;

  va_start(pa, format);

  if(issyslog)
    {
      vsnprintf(msg_buf, 256, format, pa);
      snmp_log(LOG_NOTICE, "%s", msg_buf);
    }
  else
    {
      /* emulate a syslog like format on a file */

      char now[128];
      time_t clock = time(NULL);

      static pid_t pid = 0;
      static char constant_buf[256];

      if(!pid)
        {
          int name_len;
          /* first call, let's configure */
          gethostname(constant_buf, 256);
          pid = getpid();
          name_len = strlen(constant_buf);
          snprintf(constant_buf + name_len, 256 - name_len, ": snmp_adm-%d: ", pid);
        }

      localtime_r(&clock, &the_date);
      snprintf(now, 128, "%.2d/%.2d/%.4d %.2d:%.2d:%.2d epoch=%ld: ",
               the_date.tm_mday, the_date.tm_mon + 1, 1900 + the_date.tm_year,
               the_date.tm_hour, the_date.tm_min, the_date.tm_sec, clock);

      vsnprintf(msg_buf, 256, format, pa);

      snmp_log(LOG_NOTICE, "%s => %s : %s\n", now, constant_buf, msg_buf);
    }
  va_end(pa);
}
