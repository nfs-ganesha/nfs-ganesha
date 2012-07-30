/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_errors.c
 * \date    $Date: 2005/07/27 13:30:26 $
 * \version $Revision: 1.3 $
 * \brief   Routines for handling errors.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "fsal.h"

/* Function names for logging and SNMP stats etc. */

family_error_t __attribute__ ((__unused__)) tab_errstatus_FSAL[] =
{
  {
  ERR_FSAL_NO_ERROR, "ERR_FSAL_NO_ERROR", "No error"},
  {
  ERR_FSAL_PERM, "ERR_FSAL_PERM", "Forbidden action"},
  {
  ERR_FSAL_NOENT, "ERR_FSAL_NOENT", "No such file or directory"},
  {
  ERR_FSAL_IO, "ERR_FSAL_IO", "I/O error"},
  {
  ERR_FSAL_NXIO, "ERR_FSAL_NXIO", "No such device or address"},
  {
  ERR_FSAL_NOMEM, "ERR_FSAL_NOMEM", "Not enough memory"},
  {
  ERR_FSAL_ACCESS, "ERR_FSAL_ACCESS", "Permission denied"},
  {
  ERR_FSAL_FAULT, "ERR_FSAL_FAULT", "Bad address"},
  {
  ERR_FSAL_EXIST, "ERR_FSAL_EXIST", "This object already exists"},
  {
  ERR_FSAL_XDEV, "ERR_FSAL_XDEV", "This operation can't cross filesystems"},
  {
  ERR_FSAL_NOTDIR, "ERR_FSAL_NOTDIR", "This object is not a directory"},
  {
  ERR_FSAL_ISDIR, "ERR_FSAL_ISDIR", "Directory used in a nondirectory operation"},
  {
  ERR_FSAL_INVAL, "ERR_FSAL_INVAL", "Invalid object type"},
  {
  ERR_FSAL_FBIG, "ERR_FSAL_FBIG", "File exceeds max file size"},
  {
  ERR_FSAL_NOSPC, "ERR_FSAL_NOSPC", "No space left on filesystem"},
  {
  ERR_FSAL_ROFS, "ERR_FSAL_ROFS", "Read-only filesystem"},
  {
  ERR_FSAL_MLINK, "ERR_FSAL_MLINK", "Too many hard links"},
  {
  ERR_FSAL_DQUOT, "ERR_FSAL_DQUOT", "Quota exceeded"},
  {
  ERR_FSAL_NAMETOOLONG, "ERR_FSAL_NAMETOOLONG", "Max name length exceeded"},
  {
  ERR_FSAL_NOTEMPTY, "ERR_FSAL_NOTEMPTY", "The directory is not empty"},
  {
  ERR_FSAL_STALE, "ERR_FSAL_STALE", "The file no longer exists"},
  {
  ERR_FSAL_BADHANDLE, "ERR_FSAL_BADHANDLE", "Illegal filehandle"},
  {
  ERR_FSAL_BADCOOKIE, "ERR_FSAL_BADCOOKIE", "Invalid cookie"},
  {
  ERR_FSAL_NOTSUPP, "ERR_FSAL_NOTSUPP", "Operation not supported"},
  {
  ERR_FSAL_TOOSMALL, "ERR_FSAL_TOOSMALL", "Output buffer too small"},
  {
  ERR_FSAL_SERVERFAULT, "ERR_FSAL_SERVERFAULT", "Undefined server error"},
  {
  ERR_FSAL_BADTYPE, "ERR_FSAL_BADTYPE", "Invalid type for create operation"},
  {
  ERR_FSAL_DELAY, "ERR_FSAL_DELAY", "File busy, retry"},
  {
  ERR_FSAL_FHEXPIRED, "ERR_FSAL_FHEXPIRED", "Filehandle expired"},
  {
  ERR_FSAL_SYMLINK, "ERR_FSAL_SYMLINK",
        "This is a symbolic link, should be file/directory"},
  {
  ERR_FSAL_ATTRNOTSUPP, "ERR_FSAL_ATTRNOTSUPP", "Attribute not supported"},
  {
  ERR_FSAL_NOT_INIT, "ERR_FSAL_NOT_INIT", "Filesystem not initialized"},
  {
  ERR_FSAL_ALREADY_INIT, "ERR_FSAL_ALREADY_INIT", "Filesystem already initialised"},
  {
  ERR_FSAL_BAD_INIT, "ERR_FSAL_BAD_INIT", "Filesystem initialisation error"},
  {
  ERR_FSAL_SEC, "ERR_FSAL_SEC", "Security context error"},
  {
  ERR_FSAL_NO_QUOTA, "ERR_FSAL_NO_QUOTA", "No Quota available"},
  {
  ERR_FSAL_NOT_OPENED, "ERR_FSAL_NOT_OPENED", "File/directory not opened"},
  {
  ERR_FSAL_DEADLOCK, "ERR_FSAL_DEADLOCK", "Deadlock"},
  {
  ERR_FSAL_OVERFLOW, "ERR_FSAL_OVERFLOW", "Overflow"},
  {
  ERR_FSAL_INTERRUPT, "ERR_FSAL_INTERRUPT", "Operation Interrupted"},
  {
  ERR_FSAL_BLOCKED, "ERR_FSAL_BLOCKED", "Lock Blocked"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

const char * msg_fsal_err(fsal_errors_t fsal_err)
{
  int i;

  for(i = 0; tab_errstatus_FSAL[i].numero != fsal_err &&
             tab_errstatus_FSAL[i].numero != ERR_NULL; i++)
    ;

  return tab_errstatus_FSAL[i].msg;
}

const char * label_fsal_err(fsal_errors_t fsal_err)
{
  int i;

  for(i = 0; tab_errstatus_FSAL[i].numero != fsal_err &&
             tab_errstatus_FSAL[i].numero != ERR_NULL; i++)
    ;

  return tab_errstatus_FSAL[i].label;
}
