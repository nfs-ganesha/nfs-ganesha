/**********************************************************************/
/*                                                                    */
/* gssapimodule.c - provides python interface to C gssapi libraries   */
/*                                                                    */
/* Written by Fred Isaman <iisaman@citi.umich.edu>                    */
/* Copyright (C) 2004 University of Michigan, Center for              */
/*                    Information Technology Integration              */
/*                                                                    */
/**********************************************************************/

/* Python includes - must be first */
#include <Python.h>
#include "structmember.h"       /* handle attributes */

#ifdef HEIMDAL
#include <gssapi.h>
#else
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_generic.h>
#include <gssapi/gssapi_krb5.h>
#endif                          /* HEIMDAL */

/**********************************************************************/
/*                                                                    */
/* Helper functions                                                   */
/*                                                                    */
/**********************************************************************/

static int is_string(const char *str, int len)
{
  return !((len == 0) || (strlen(str) != (len - 1)));
}

/**********************************************************************/
/*                                                                    */
/* Wrappers for needed functions in C gssapi library                  */
/*                                                                    */
/**********************************************************************/

/* (major, minor, name) = importName(string, type=gss_nt_service_name) */
static PyObject *gssapi_importName(PyObject * self, PyObject * args)
{
  char *string, *type_name = NULL;
  int len, type_len;
  OM_uint32 major = 0, minor = 0;
  gss_buffer_desc sname;
  gss_OID_desc type, *typep = (gss_OID) GSS_C_NT_HOSTBASED_SERVICE;
  gss_name_t name = NULL;
  PyObject *rv, *bufname;

  if(!PyArg_ParseTuple(args, "s#|s#", &string, &len, &type_name, &type_len))
    return NULL;

  sname.value = string;
  sname.length = len;

  if(type_len && type_name)
    {
      type.length = type_len;
      type.elements = type_name;
      typep = &type;
    }

  major = gss_import_name(&minor, &sname, typep, &name);

  /* FRED - can this be a string? */
  /* Turn name into a PyObect */
  bufname = PyBuffer_FromMemory(name, strlen((char *)name) + 1);

  rv = Py_BuildValue("{sisisN}", "major", major, "minor", minor, "name", bufname);
  if(!rv)
    {
      Py_DECREF(bufname);
    }
  return rv;
}

/* (major, minor, token, context, mech, flags, time) =
   initSecContext(name, token=GSS_C_NO_BUFFER, context=GSS_C_NO_CONTEXT,
                  mech=krb5, cred=GSS_C_NO_CREDENTIAL, flags=GSS_C_MUTUAL_FLAG,
                  time=0)
*/
static PyObject *gssapi_initSecContext(PyObject * self, PyObject * args)
{
  /* STUB - does not allow input of channel bindings */

  static gss_OID_desc krb5oid = { 9, "\052\206\110\206\367\022\001\002\002" };
  /* Input and output variables */
  OM_uint32 major = 0;
  OM_uint32 minor = 0;
  char *in_name = NULL;
  int in_name_len = 0;
  char *in_token = NULL;
  int in_token_len = 0;
  char *in_context = NULL;
  int in_context_len = 0;
  char *in_mech = NULL;
  int in_mech_len = 0;
  char *in_cred = NULL;
  int in_cred_len = 0;
  PyObject *out_context = NULL;
  PyObject *out_mech = NULL;
  OM_uint32 out_flags = 0, in_flags = GSS_C_MUTUAL_FLAG;
  OM_uint32 out_time = 0, in_time = 0;

  /* Local variables */
  gss_cred_id_t cred = GSS_C_NO_CREDENTIAL;
  gss_ctx_id_t ctx = GSS_C_NO_CONTEXT;
  gss_OID actual_mech, mechp = &krb5oid;
  gss_OID_desc mechdata;
  gss_buffer_desc send_token, rcv_token, *rcv_tokenp = GSS_C_NO_BUFFER;
  PyObject *rv;

  if(!PyArg_ParseTuple(args, "t#|z#z#z#z#llO!",
                       &in_name, &in_name_len,
                       &in_token, &in_token_len,
                       &in_context, &in_context_len,
                       &in_mech, &in_mech_len,
                       &in_cred, &in_cred_len, &in_flags, &in_time))
    return NULL;

  if(!is_string(in_name, in_name_len))
    {
      PyErr_SetString(PyExc_TypeError,
                      "'name' must be null-terminated string with no embedded nulls");
      return NULL;
    }

  /* Set defaults */
  if(in_token && in_token_len)
    {
      rcv_token.value = in_token;
      rcv_token.length = in_token_len;
      rcv_tokenp = &rcv_token;
    }
  if(in_context && in_context_len)
    ctx = (void *)in_context;
  if(in_mech && in_mech_len)
    {
      mechdata.length = in_mech_len;
      mechdata.elements = in_mech;
      mechp = &mechdata;
    }
  if(in_cred && in_cred_len)
    cred = (void *)in_cred;

  /* Call function */
  major = gss_init_sec_context(&minor, cred, &ctx, (void *)in_name,
                               mechp, in_flags, in_time, NULL,
                               rcv_tokenp, &actual_mech,
                               &send_token, &out_flags, &out_time);
  out_context = PyBuffer_FromMemory(ctx, 4);
  if(out_context == NULL)
    goto error;
  out_mech = PyBuffer_FromMemory(actual_mech, 4);
  if(out_mech == NULL)
    goto error;

  rv = Py_BuildValue("{sisisNss#sNsisi}", "major", major, "minor", minor,
                     "context", out_context,
                     "token", send_token.value, send_token.length,
                     "mech", out_mech, "flags", out_flags, "time", out_time);
  if(rv)
    return rv;

 error:
  Py_XDECREF(out_context);
  Py_XDECREF(out_mech);

  return NULL;
}

/* (major, minor, qop) = verifyMIC(context, message, token) */
static PyObject *gssapi_verifyMIC(PyObject * self, PyObject * args)
{
  /* Input and output variables */
  char *in_context = NULL;
  int in_context_len = 0;
  char *in_msg = NULL;
  int in_msg_len = 0;
  char *in_token = NULL;
  int in_token_len = 0;
  OM_uint32 qop = 0;
  OM_uint32 major = 0;
  OM_uint32 minor = 0;

  /* Local variables */
  gss_buffer_desc token, message;

  if(!PyArg_ParseTuple(args, "z#s#s#",
                       &in_context, &in_context_len,
                       &in_msg, &in_msg_len, &in_token, &in_token_len))
    return NULL;

  message.value = in_msg;
  message.length = in_msg_len;
  token.value = in_token;
  token.length = in_token_len;

  major = gss_verify_mic(&minor, (void *)in_context, &message, &token, &qop);

  return Py_BuildValue("{sisisi}", "major", major, "minor", minor, "qop", qop);
}

/* (major, minor, token) = getMIC(context, message, qop=0) */
static PyObject *gssapi_getMIC(PyObject * self, PyObject * args)
{
  /* Input and output variables */
  char *in_context = NULL;
  int in_context_len = 0;
  char *in_msg = NULL;
  int in_msg_len = 0;
  OM_uint32 qop = 0;
  OM_uint32 major = 0;
  OM_uint32 minor = 0;
  PyObject *rv;

  /* Local variables */
  gss_buffer_desc out_token, message;

  if(!PyArg_ParseTuple(args, "t#s#|i",
                       &in_context, &in_context_len, &in_msg, &in_msg_len, &qop))
    return NULL;

  message.value = in_msg;
  message.length = in_msg_len;

  major = gss_get_mic(&minor, (void *)in_context, qop, &message, &out_token);

  rv = Py_BuildValue("{sisiss#}", "major", major, "minor", minor,
                     "token", out_token.value, out_token.length);
  gss_release_buffer(&minor, &out_token);
  return rv;
}

/* (major, minor, msg, conf) = wrap(context, msg, conf=True, qop=0) */
static PyObject *gssapi_wrap(PyObject * self, PyObject * args)
{
  /* Input and output variables */
  char *in_context = NULL;
  int in_context_len = 0;
  char *in_msg = NULL;
  int in_msg_len = 0;
  OM_uint32 in_confidential = 1;
  OM_uint32 qop = 0;

  OM_uint32 major = 0;
  OM_uint32 minor = 0;
  OM_uint32 out_confidential = 0;
  PyObject *rv;

  /* Local variables */
  gss_buffer_desc out_msg, msg;

  if(!PyArg_ParseTuple(args, "t#s#|ii",
                       &in_context, &in_context_len,
                       &in_msg, &in_msg_len, &in_confidential, &qop))
    return NULL;

  msg.value = in_msg;
  msg.length = in_msg_len;

  major = gss_wrap(&minor, (void *)in_context, in_confidential, qop,
                   &msg, &out_confidential, &out_msg);

  rv = Py_BuildValue("{sisiss#si}", "major", major, "minor", minor,
                     "msg", out_msg.value, out_msg.length, "conf", out_confidential);
  gss_release_buffer(&minor, &out_msg);
  return rv;
}

/* (major, minor, msg, conf, qop) = unwrap(context, msg) */
static PyObject *gssapi_unwrap(PyObject * self, PyObject * args)
{
  /* Input and output variables */
  char *in_context = NULL;
  int in_context_len = 0;
  char *in_msg = NULL;
  int in_msg_len = 0;

  OM_uint32 major = 0;
  OM_uint32 minor = 0;
  OM_uint32 confidential = 0;
  OM_uint32 qop = 0;
  PyObject *rv;

  /* Local variables */
  gss_buffer_desc out_msg, msg;

  if(!PyArg_ParseTuple(args, "t#s#", &in_context, &in_context_len, &in_msg, &in_msg_len))
    return NULL;

  msg.value = in_msg;
  msg.length = in_msg_len;

  major = gss_unwrap(&minor, (void *)in_context, &msg, &out_msg, &confidential, &qop);

  rv = Py_BuildValue("{sisiss#sisi}", "major", major, "minor", minor,
                     "msg", out_msg.value, out_msg.length,
                     "conf", confidential, "qop", qop);
  gss_release_buffer(&minor, &out_msg);
  return rv;
}

/**********************************************************************/
/*                                                                    */
/* Table of wrappers                                                  */
/*                                                                    */
/**********************************************************************/

static PyMethodDef GssapiMethods[] = {
  {"importName", gssapi_importName, METH_VARARGS,
   "(major,minor,name) = importName(string, type=gss_nt_service_name)"},
  {"verifyMIC", gssapi_verifyMIC, METH_VARARGS,
   "(major, minor, qop) = verifyMIC(context, message, token)"},
  {"getMIC", gssapi_getMIC, METH_VARARGS,
   "(major, minor, token) = getMIC(context, message, qop=0)"},
  {"wrap", gssapi_wrap, METH_VARARGS,
   "(major, minor, msg, conf) = wrap(context, msg, conf=True, qop=0)"},
  {"unwrap", gssapi_unwrap, METH_VARARGS,
   "(major, minor, msg, conf, qop) = unwrap(context, msg)"},
  {"initSecContext", gssapi_initSecContext, METH_VARARGS,
   "(major, minor, token, context, mech=0, flags, time=0) = \n"
   "initSecContext(name, token=NULL, context=NULL, mech=krb5, cred=NULL,\n"
   "               flags=GSS_C_MUTUAL_FLAG, time=0, binding=NULL)"},
  {NULL, NULL, 0, NULL}         /* Sentinel */
};

/**********************************************************************/
/*                                                                    */
/* Module initialization                                              */
/*                                                                    */
/**********************************************************************/

static struct
{
  char *name;
  unsigned long int value;
} major_codes[] =
{
  {
  "GSS_S_COMPLETE", 0x00000000},
  {
  "GSS_S_CONTINUE_NEEDED", 0x00000001},
  {
  "GSS_S_DUPLICATE_TOKEN", 0x00000002},
  {
  "GSS_S_OLD_TOKEN", 0x00000004},
  {
  "GSS_S_UNSEQ_TOKEN", 0x00000008},
  {
  "GSS_S_GAP_TOKEN", 0x00000010},
  {
  "GSS_S_BAD_MECH", 0x00010000},
  {
  "GSS_S_BAD_NAME", 0x00020000},
  {
  "GSS_S_BAD_NAMETYPE", 0x00030000},
  {
  "GSS_S_BAD_BINDINGS", 0x00040000},
  {
  "GSS_S_BAD_STATUS", 0x00050000},
  {
  "GSS_S_BAD_MIC", 0x00060000},
  {
  "GSS_S_BAD_SIG", 0x00060000},
  {
  "GSS_S_NO_CRED", 0x00070000},
  {
  "GSS_S_NO_CONTEXT", 0x00080000},
  {
  "GSS_S_DEFECTIVE_TOKEN", 0x00090000},
  {
  "GSS_S_DEFECTIVE_CREDENTIAL", 0x000a0000},
  {
  "GSS_S_CREDENTIALS_EXPIRED", 0x000b0000},
  {
  "GSS_S_CONTEXT_EXPIRED", 0x000c0000},
  {
  "GSS_S_FAILURE", 0x000d0000},
  {
  "GSS_S_BAD_QOP", 0x000e0000},
  {
  "GSS_S_UNAUTHORIZED", 0x000f0000},
  {
  "GSS_S_UNAVAILABLE", 0x00100000},
  {
  "GSS_S_DUPLICATE_ELEMENT", 0x00110000},
  {
  "GSS_S_NAME_NOT_MN", 0x00120000},
  {
  "GSS_S_CALL_INACCESSIBLE_READ", 0x01000000},
  {
  "GSS_S_CALL_INACCESSIBLE_WRITE", 0x02000000},
  {
  "GSS_S_CALL_BAD_STRUCTURE", 0x03000000},
  {
  NULL, 0}
};

/* Module initialization...the only nonstatic entity */
#ifndef PyMODINIT_FUNC          /* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC initgssapi(void)
{
  int i;
  PyObject *m;

  /* This is the critical line */
  m = Py_InitModule3("gssapi", GssapiMethods, "Wrapper for C gssapi routines");
  if(m == NULL)
    return;

  /* Add GSS_S_* constants */
  for(i = 0; major_codes[i].name; i++)
    {
      PyModule_AddIntConstant(m, major_codes[i].name, major_codes[i].value);
    }
}
