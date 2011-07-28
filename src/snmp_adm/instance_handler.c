/**
 * \file    instance_handler.c
 * \author  Cédric CABESSA
 * \brief   instance_handler: handle callback for different type.
 *          Those function are called when daemon receive a SNMP request.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include <pthread.h>

#include "parse_type.h"
#include "get_set_proc.h"

/** linked list of registred objects */
extern register_info *register_info_list;
extern int root_oid_len;

/* Because I/O procedures are registered like scalars we need to check access.
   If we are a procedure input/output, check the trigger value and send error if need */
static int check_procedure_access(netsnmp_handler_registration * reginfo)
{
  register_info *info;

  /* XXX
   *  In order to check type of current node, we use it length.
   *  This parameter will change after each modification in the tree structure.
   *  
   *  conf_stat_len is the length of a val element if the node is a stat or a conf.
   *  numproc_pos is the numproc position in the oid for a procedure node.
   */
  int conf_stat_len = root_oid_len + 5;
  int numproc_pos = reginfo->rootoid_len - 5;

  if(reginfo->rootoid_len == conf_stat_len)
    /* we are a stat or conf, nothing to do */
    return 0;

  for(info = register_info_list; info; info = info->next)
    if(info->type == PROC &&
       info->function_info.proc->num == reginfo->rootoid[numproc_pos])
      break;

  if(!info || info->function_info.proc->trigger == SNMP_ADM_PROGRESS
     || info->function_info.proc->trigger == SNMP_ADM_DONE
     || info->function_info.proc->trigger == SNMP_ADM_ERROR)
    return 1;
  return 0;

}

int instance_string_handler(netsnmp_mib_handler * handler,
                            netsnmp_handler_registration * reginfo,
                            netsnmp_agent_request_info * reqinfo,
                            netsnmp_request_info * requests)
{
  int len;

  switch (reqinfo->mode)
    {
      /*
       * data requests
       */
    case MODE_GET:
      snmp_set_var_typed_value(requests->requestvb, ASN_OCTET_STR,
                               (u_char *) handler->myvoid, strlen(handler->myvoid));
      break;

    case MODE_SET_ACTION:
      /*
       * update current
       */
      if(check_procedure_access(reginfo))
        {
          netsnmp_request_set_error(requests, SNMP_ERR_READONLY);
          return SNMP_ERR_READONLY;
        }

      len = strlen((char *)requests->requestvb->val.string);
      if(len > SNMP_ADM_MAX_STR)
        netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);
      else
        {
          strncpy(handler->myvoid, (char *)(requests->requestvb->val.string), len);
          ((char *)(handler->myvoid))[len] = '\0';      /* just to be sure */
        }
      break;
      /* XXX
         do not add a default case otherwise netsnmp will never send SET_ACTION (because SET_RESERVE is considered
         as failed) see AGENT.txt section 8
       */

    }
  return SNMP_ERR_NOERROR;
}

int instance_int_handler(netsnmp_mib_handler * handler,
                         netsnmp_handler_registration * reginfo,
                         netsnmp_agent_request_info * reqinfo,
                         netsnmp_request_info * requests)
{
  int *it = (int *)handler->myvoid;

  switch (reqinfo->mode)
    {
      /*
       * data requests
       */
    case MODE_GET:
      snmp_set_var_typed_value(requests->requestvb, ASN_INTEGER,
                               (u_char *) it, sizeof(*it));
      break;

    case MODE_SET_ACTION:
      /*
       * update current
       */
      if(check_procedure_access(reginfo))
        {
          netsnmp_request_set_error(requests, SNMP_ERR_READONLY);
          return SNMP_ERR_READONLY;
        }

      *it = (int)*(requests->requestvb->val.integer);
      break;

    }
  return SNMP_ERR_NOERROR;
}

int instance_real_handler(netsnmp_mib_handler * handler,
                          netsnmp_handler_registration * reginfo,
                          netsnmp_agent_request_info * reqinfo,
                          netsnmp_request_info * requests)
{
  int err;
  double *it = (double *)handler->myvoid;
  double tmp_it;
  char str[256];

  switch (reqinfo->mode)
    {
      /*
       * data requests
       */
    case MODE_GET:
      err = real2str(str, *it);
      if(!err)
        snmp_set_var_typed_value(requests->requestvb, ASN_OCTET_STR,
                                 (u_char *) str, strlen(str));
      else
        netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);
      break;

    case MODE_SET_ACTION:
      /*
       * update current
       */
      if(check_procedure_access(reginfo))
        {
          netsnmp_request_set_error(requests, SNMP_ERR_READONLY);
          return SNMP_ERR_READONLY;
        }
      err = str2real(&tmp_it, (char *)(requests->requestvb->val.string));
      if(!err)
        *it = tmp_it;
      else
        netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);

      break;

    }
  return SNMP_ERR_NOERROR;
}

int instance_bigint_handler(netsnmp_mib_handler * handler,
                            netsnmp_handler_registration * reginfo,
                            netsnmp_agent_request_info * reqinfo,
                            netsnmp_request_info * requests)
{
  int err;
  int64_t *it = (int64_t *) handler->myvoid;
  int64_t tmp_it;
  char str[256];

  switch (reqinfo->mode)
    {
      /*
       * data requests
       */
    case MODE_GET:
      err = big2str(str, *it);
      if(!err)
        snmp_set_var_typed_value(requests->requestvb, ASN_OCTET_STR,
                                 (u_char *) str, strlen(str));
      else
        netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);
      break;

    case MODE_SET_ACTION:
      /*
       * update current
       */
      if(check_procedure_access(reginfo))
        {
          netsnmp_request_set_error(requests, SNMP_ERR_READONLY);
          return SNMP_ERR_READONLY;
        }
      err = str2big(&tmp_it, (char *)(requests->requestvb->val.string));
      if(!err)
        *it = tmp_it;
      else
        netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);

      break;

    }
  return SNMP_ERR_NOERROR;
}

int instance_time_handler(netsnmp_mib_handler * handler,
                          netsnmp_handler_registration * reginfo,
                          netsnmp_agent_request_info * reqinfo,
                          netsnmp_request_info * requests)
{
  unsigned int *it = (unsigned int *)handler->myvoid;
  unsigned int tmp_it;

  switch (reqinfo->mode)
    {
      /*
       * data requests
       */
    case MODE_GET:
      tmp_it = (*it) * 100;
      snmp_set_var_typed_value(requests->requestvb, ASN_TIMETICKS,
                               (u_char *) & tmp_it, sizeof(tmp_it));
      break;

    case MODE_SET_ACTION:
      /*
       * update current
       */
      if(check_procedure_access(reginfo))
        {
          netsnmp_request_set_error(requests, SNMP_ERR_READONLY);
          return SNMP_ERR_READONLY;
        }
      tmp_it = *((unsigned int *)(requests->requestvb->val.integer));
      *it = tmp_it / 100;
      break;

      break;

    }
  return SNMP_ERR_NOERROR;
}

int instance_ip_handler(netsnmp_mib_handler * handler,
                        netsnmp_handler_registration * reginfo,
                        netsnmp_agent_request_info * reqinfo,
                        netsnmp_request_info * requests)
{
  in_addr_t *it = (in_addr_t *) handler->myvoid;

  switch (reqinfo->mode)
    {
      /*
       * data requests
       */
    case MODE_GET:
      snmp_set_var_typed_value(requests->requestvb, ASN_IPADDRESS,
                               (u_char *) it, sizeof(*it));
      break;

    case MODE_SET_ACTION:
      /*
       * update current
       */
      if(check_procedure_access(reginfo))
        {
          netsnmp_request_set_error(requests, SNMP_ERR_READONLY);
          return SNMP_ERR_READONLY;
        }
      memcpy(it, requests->requestvb->val.string, sizeof(*it));
      break;
    }
  return SNMP_ERR_NOERROR;
}

int instance_get_set_handler(netsnmp_mib_handler * handler,
                             netsnmp_handler_registration * reginfo,
                             netsnmp_agent_request_info * reqinfo,
                             netsnmp_request_info * requests)
{

  snmp_adm_type_union var;
  unsigned char type;
  register_info *info = NULL;
  int branch, num_stat_conf, err = 0, err_fct;
  char str[256];
  unsigned int tmp_it;

        /** XXX
	 *  Yet another tree structure specific computation
	 */
  /* what node do you want? */
  num_stat_conf = reginfo->rootoid[reginfo->rootoid_len - 3];
  branch = reginfo->rootoid[reginfo->rootoid_len - 4];

  /* look for our get_set */
  for(info = register_info_list; info; info = info->next)
    if(info->type == GET_SET &&
       info->function_info.get_set->num == num_stat_conf &&
       info->function_info.get_set->branch == branch)
      break;

  if(!info)
    {                           /* not found */
      netsnmp_request_set_error(requests, SNMP_ERR_GENERR);
      return SNMP_ERR_GENERR;
    }

  type = info->function_info.get_set->type;

  switch (reqinfo->mode)
    {
    case MODE_GET:
      /*
       * data requests
       */
      /* call the function */
      err =
          info->function_info.get_set->getter(&var, info->function_info.get_set->opt_arg);
      if(err)
        {
          snmp_set_var_typed_value(requests->requestvb, ASN_OCTET_STR,
                                   (u_char *) "SNMP_ADM_ERROR", strlen("SNMP_ADM_ERROR"));

          return SNMP_ERR_NOERROR;
        }
      switch (type)
        {
        case SNMP_ADM_INTEGER:
          snmp_set_var_typed_value(requests->requestvb, ASN_INTEGER,
                                   (u_char *) & (var.integer), sizeof(var.integer));
          break;
        case SNMP_ADM_STRING:
          snmp_set_var_typed_value(requests->requestvb, ASN_OCTET_STR,
                                   (u_char *) var.string, strlen((char *)var.string));
          break;
        case SNMP_ADM_REAL:
          err = real2str(str, var.real);
          if(!err)
            snmp_set_var_typed_value(requests->requestvb, ASN_OCTET_STR,
                                     (u_char *) str, strlen(str));
          else
            netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);
          break;
        case SNMP_ADM_BIGINT:
          err = big2str(str, var.bigint);
          if(!err)
            snmp_set_var_typed_value(requests->requestvb, ASN_OCTET_STR,
                                     (u_char *) str, strlen(str));
          else
            netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);
          break;
        case SNMP_ADM_TIMETICKS:
          tmp_it = (var.time) * 100;

          snmp_set_var_typed_value(requests->requestvb, ASN_TIMETICKS,
                                   (u_char *) & (tmp_it), sizeof(tmp_it));
          break;

        default:
          netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);
        }

      break;
    case MODE_SET_ACTION:
      switch (type)
        {
        case SNMP_ADM_INTEGER:
          var.integer = *(int *)requests->requestvb->val.integer;
          break;
        case SNMP_ADM_STRING:
          strncpy(var.string, (char *)(requests->requestvb->val.string),
                  SNMP_ADM_MAX_STR);
          break;
        case SNMP_ADM_REAL:
          err = str2real(&(var.real), (char *)(requests->requestvb->val.string));
          break;
        case SNMP_ADM_BIGINT:
          err = str2big(&(var.bigint), (char *)(requests->requestvb->val.string));
          break;
        case SNMP_ADM_TIMETICKS:
          tmp_it = *(unsigned int *)requests->requestvb->val.integer;
          var.time = tmp_it / 100;
          break;
        default:
          netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);
        }

      if(!err)
        {
          /* call the function */
          err_fct =
              info->function_info.get_set->setter(&var,
                                                  info->function_info.get_set->opt_arg);

          if(err_fct)
            netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);
        }
      else
        netsnmp_request_set_error(requests, SNMP_ERR_BADVALUE);
      break;
    }

  return SNMP_ERR_NOERROR;
}

static void *launch_proc(void *arg)
{
  proc_info *pinfo = arg;
  int err;

  pinfo->trigger = SNMP_ADM_PROGRESS;
  err =
      pinfo->myproc((const snmp_adm_type_union **)pinfo->inputs, pinfo->outputs,
                    pinfo->opt_arg);

  if(err)
    pinfo->trigger = SNMP_ADM_ERROR;
  else
    pinfo->trigger = SNMP_ADM_DONE;

  return NULL;
}

int instance_proc_handler(netsnmp_mib_handler * handler,
                          netsnmp_handler_registration * reginfo,
                          netsnmp_agent_request_info * reqinfo,
                          netsnmp_request_info * requests)
{
  register_info *info;
  proc_info *pinfo;
  int i = 0, num_proc;
  pthread_t thread_id;

        /** XXX
	 *  Yet another tree structure specific computation
	 */
  /* what node do you want? */
  num_proc = reginfo->rootoid[reginfo->rootoid_len - 2];

  for(info = register_info_list; info; info = info->next)
    if(info->type == PROC && info->function_info.proc->num == num_proc)
      break;

  if(!info)
    {                           /* not found */
      netsnmp_request_set_error(requests, SNMP_ERR_GENERR);
      return SNMP_ERR_GENERR;
    }
  pinfo = info->function_info.proc;
  switch (reqinfo->mode)
    {
    case MODE_GET:
      snmp_set_var_typed_value(requests->requestvb, ASN_INTEGER,
                               (u_char *) & (pinfo->trigger), sizeof(pinfo->trigger));
      break;
    case MODE_SET_ACTION:
      switch (pinfo->trigger)
        {
        case SNMP_ADM_READY:
          /* call the proc */
          pthread_create(&thread_id, NULL, launch_proc, pinfo);
          break;
        case SNMP_ADM_PROGRESS:
          netsnmp_request_set_error(requests, SNMP_ERR_GENERR);
          return SNMP_ERR_GENERR;
        case SNMP_ADM_DONE:
        case SNMP_ADM_ERROR:
          if((*requests->requestvb->val.integer) == 0)
            {
              /* raz */
              for(i = 0; i < pinfo->nb_in; i++)
                memset(pinfo->inputs[i], 0, sizeof(snmp_adm_type_union));

              for(i = 0; i < pinfo->nb_out; i++)
                memset(pinfo->outputs[i], 0, sizeof(snmp_adm_type_union));

              pinfo->trigger = SNMP_ADM_READY;
            }
          break;
        }
    }
  return SNMP_ERR_NOERROR;
}
