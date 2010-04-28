/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
 * ---------------------------------------
 *
 */

#ifndef _ERR_RPC_H
#define _ERR_RPC_H

#include "log_functions.h"

static family_error_t __attribute__ ((__unused__)) tab_error_rpc[] =
{
#define ERR_SVCUDP_CREATE  0
  {
  ERR_SVCUDP_CREATE, "ERR_SVCUDP_CREATE", "svcudp_create impossible"},
#define ERR_SVCTCP_CREATE  1
  {
  ERR_SVCTCP_CREATE, "ERR_SVCTCP_CREATE", "svctcp_create impossible"},
#define ERR_SVC_CREATE     2
  {
  ERR_SVC_CREATE, "ERR_SVC_CREATE", "svc_create impossible"},
#define ERR_SVC_REGISTER   3
  {
  ERR_SVC_REGISTER, "ERR_SVC_REGISTER", "svc_register impossible"},
#define ERR_CLNTUDP_CREATE 4
  {
  ERR_CLNTUDP_CREATE, "ERR_CLNTUDP_CREATE", "clntudp_create impossible"},
#define ERR_CLNTTCP_CREATE 5
  {
  ERR_CLNTTCP_CREATE, "ERR_CLNTTCP_CREATE", "clnttcp_create impossible"},
#define ERR_GETRPCBYNAME   6
  {
  ERR_GETRPCBYNAME, "ERR_GETRPCBYNAME", "getrpcbyname impossible"},
#define ERR_IOCTL_I_POP    7
  {
  ERR_IOCTL_I_POP, "ERR_IOCTL_I_POP", "ioctl I_POP impossible"},
#define ERR_IOCTL_I_PUSH   8
  {
  ERR_IOCTL_I_PUSH, "ERR_IOCTL_I_PUSH", "ioctl I_PUSH impossible"},
#define ERR_SVC_GETCALLER  9
  {
  ERR_SVC_GETCALLER, "ERR_SVC_GETCALLER", "svc_getcaller impossible"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif
