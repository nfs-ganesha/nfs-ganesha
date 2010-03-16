/**
 * \file    instance_handler.h
 * \author  Cédric CABESSA
 * \brief   instance_handler: handle callback for different type.
 *          Those function are called when daemon receive a SNMP request.
 *
 */

#ifndef __INSTANCE_HANDLER_H__
#define __INSTANCE_HANDLER_H__

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

int instance_string_handler(netsnmp_mib_handler * handler,
                            netsnmp_handler_registration * reginfo,
                            netsnmp_agent_request_info * reqinfo,
                            netsnmp_request_info * requests);

int instance_int_handler(netsnmp_mib_handler * handler,
                         netsnmp_handler_registration * reginfo,
                         netsnmp_agent_request_info * reqinfo,
                         netsnmp_request_info * requests);

int instance_real_handler(netsnmp_mib_handler * handler,
                          netsnmp_handler_registration * reginfo,
                          netsnmp_agent_request_info * reqinfo,
                          netsnmp_request_info * requests);

int instance_bigint_handler(netsnmp_mib_handler * handler,
                            netsnmp_handler_registration * reginfo,
                            netsnmp_agent_request_info * reqinfo,
                            netsnmp_request_info * requests);

int instance_time_handler(netsnmp_mib_handler * handler,
                          netsnmp_handler_registration * reginfo,
                          netsnmp_agent_request_info * reqinfo,
                          netsnmp_request_info * requests);

int instance_ip_handler(netsnmp_mib_handler * handler,
                        netsnmp_handler_registration * reginfo,
                        netsnmp_agent_request_info * reqinfo,
                        netsnmp_request_info * requests);

int instance_get_set_handler(netsnmp_mib_handler * handler,
                             netsnmp_handler_registration * reginfo,
                             netsnmp_agent_request_info * reqinfo,
                             netsnmp_request_info * requests);

int instance_proc_handler(netsnmp_mib_handler * handler,
                          netsnmp_handler_registration * reginfo,
                          netsnmp_agent_request_info * reqinfo,
                          netsnmp_request_info * requests);

#endif                          /* __INSTANCE_HANDLER_H__ */
