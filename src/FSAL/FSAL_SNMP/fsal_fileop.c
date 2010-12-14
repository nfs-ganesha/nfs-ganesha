/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_fileop.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/15 14:26:10 $
 * \version $Revision: 1.11 $
 * \brief   Files operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"

/**
 * FSAL_open_byname:
 * Open a regular file for reading/writing its data content.
 *
 * \param dirhandle (input):
 *        Handle of the directory that contain the file to be read/modified.
 * \param filename (input):
 *        Name of the file to be read/modified
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param openflags (input):
 *        Flags that indicates behavior for file opening and access.
 *        This is an inclusive OR of the following values
 *        ( such of them are not compatible) :
 *        - FSAL_O_RDONLY: opening file for reading only.
 *        - FSAL_O_RDWR: opening file for reading and writing.
 *        - FSAL_O_WRONLY: opening file for writting only.
 *        - FSAL_O_APPEND: always write at the end of the file.
 *        - FSAL_O_TRUNC: truncate the file to 0 on opening.
 * \param file_descriptor (output):
 *        The file descriptor to be used for FSAL_read/write operations.
 * \param file_attributes (optionnal input/output):
 *        Post operation attributes.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_ACCESS       (user doesn't have the permissions for opening the file)
 *      - ERR_FSAL_STALE        (filehandle does not address an existing object)
 *      - ERR_FSAL_INVAL        (filehandle does not address a regular file,
 *                               or open flags are conflicting)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */

fsal_status_t SNMPFSAL_open_by_name(snmpfsal_handle_t * dirhandle,      /* IN */
                                    fsal_name_t * filename,     /* IN */
                                    snmpfsal_op_context_t * p_context,  /* IN */
                                    fsal_openflags_t openflags, /* IN */
                                    snmpfsal_file_t * file_descriptor,  /* OUT */
                                    fsal_attrib_list_t *
                                    file_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t fsal_status;
  snmpfsal_handle_t filehandle;

  if(!dirhandle || !filename || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_name);

  fsal_status =
      SNMPFSAL_lookup(dirhandle, filename, p_context, &filehandle, file_attributes);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  return SNMPFSAL_open(&filehandle, p_context, openflags, file_descriptor,
                       file_attributes);
}

/**
 * FSAL_open:
 * Open a regular file for reading/writing its data content.
 *
 * \param filehandle (input):
 *        Handle of the file to be read/modified.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param openflags (input):
 *        Flags that indicates behavior for file opening and access.
 *        This is an inclusive OR of the following values
 *        ( such of them are not compatible) : 
 *        - FSAL_O_RDONLY: opening file for reading only.
 *        - FSAL_O_RDWR: opening file for reading and writing.
 *        - FSAL_O_WRONLY: opening file for writting only.
 *        - FSAL_O_APPEND: always write at the end of the file.
 *        - FSAL_O_TRUNC: truncate the file to 0 on opening.
 * \param file_descriptor (output):
 *        The file descriptor to be used for FSAL_read/write operations.
 * \param file_attributes (optionnal input/output):
 *        Post operation attributes.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_ACCESS       (user doesn't have the permissions for opening the file)
 *      - ERR_FSAL_STALE        (filehandle does not address an existing object) 
 *      - ERR_FSAL_INVAL        (filehandle does not address a regular file,
 *                               or open flags are conflicting)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t SNMPFSAL_open(snmpfsal_handle_t * filehandle,     /* IN */
                            snmpfsal_op_context_t * p_context,  /* IN */
                            fsal_openflags_t openflags, /* IN */
                            snmpfsal_file_t * file_descriptor,  /* OUT */
                            fsal_attrib_list_t * file_attributes        /* [ IN/OUT ] */
    )
{

  int rc;

  /* sanity checks.
   * note : file_attributes is optional.
   */
  if(!filehandle || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open);

  /* check if this is a file if the information
   * is stored into the handle */

  if(filehandle->data.object_type_reminder != FSAL_NODETYPE_LEAF)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open);
    }

  /**
   * Given that SNMP data are not real files,
   * open flags can be ambiguous...
   * FSAL_O_RDONLY: valid and meanfull in SNMP
   * FSAL_O_WRONLY: valid and meanfull in SNMP
   * FSAL_O_RDWR: can result in strange behaviors, not supported
   * FSAL_O_APPEND: strange too, we ignore it (data will be written at offset 0, and seek operation will return EINVAL) 
   * FSAL_O_TRUNC: ok, because it is the actually behavior when writting data 
   */

  if(openflags & FSAL_O_RDWR)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open);

  /* FSAL_O_TRUNC and FSAL_O_APPEND are incompatible with reading data */
  if((openflags & FSAL_O_RDONLY) && (openflags & (FSAL_O_TRUNC | FSAL_O_APPEND)))
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open);

  /* FSAL_O_APPEND et FSAL_O_TRUNC cannot be used together */
  if((openflags & FSAL_O_APPEND) && (openflags & FSAL_O_TRUNC))
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open);

  /* save data to filedesc */

  file_descriptor->file_handle = *filehandle;
  file_descriptor->p_context = p_context;

  if(openflags & FSAL_O_RDONLY)
    file_descriptor->rw_mode = FSAL_MODE_READ;
  else
    file_descriptor->rw_mode = FSAL_MODE_WRITE;

  /* if attributes are asked, get them */

  if(file_attributes)
    {
      fsal_status_t status;

      status = SNMPFSAL_getattrs(filehandle, p_context, file_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(file_attributes->asked_attributes);
          FSAL_SET_MASK(file_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_open);

}

/**
 * FSAL_read:
 * Perform a read operation on an opened file.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param seek_descriptor (optional input):
 *        Specifies the position where data is to be read.
 *        If not specified, data will be read at the current position.
 * \param buffer_size (input):
 *        Amount (in bytes) of data to be read.
 * \param buffer (output):
 *        Address where the read data is to be stored in memory.
 * \param read_amount (output):
 *        Pointer to the amount of data (in bytes) that have been read
 *        during this call.
 * \param end_of_file (output):
 *        Pointer to a boolean that indicates whether the end of file
 *        has been reached during this call.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_INVAL        (invalid parameter)
 *      - ERR_FSAL_NOT_OPENED   (tried to read in a non-opened snmpfsal_file_t)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t SNMPFSAL_read(snmpfsal_file_t * file_descriptor,  /* IN */
                            fsal_seek_t * seek_descriptor,      /* [IN] */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* OUT */
                            fsal_size_t * read_amount,  /* OUT */
                            fsal_boolean_t * end_of_file        /* OUT */
    )
{
  int rc;
  fsal_request_desc_t query_desc;
  size_t sz = buffer_size;

  /* sanity checks. */

  if(!file_descriptor || !buffer || !read_amount || !end_of_file)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_read);

  /* check that file has been opened in read mode  */
  if(file_descriptor->rw_mode != FSAL_MODE_READ)
    Return(ERR_FSAL_NOT_OPENED, 0, INDEX_FSAL_read);

  /* seek operations are not allowed */

  if(seek_descriptor && seek_descriptor->whence == FSAL_SEEK_END)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_read);

  if(seek_descriptor && seek_descriptor->whence == FSAL_SEEK_SET
     && seek_descriptor->offset != 0)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_read);

  if(seek_descriptor && seek_descriptor->whence == FSAL_SEEK_CUR
     && seek_descriptor->offset != 0)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_read);

  /* now proceed a SNMP GET request */

  query_desc.request_type = SNMP_MSG_GET;

  TakeTokenFSCall();

  rc = IssueSNMPQuery(file_descriptor->p_context, file_descriptor->file_handle.data.oid_tab,
                      file_descriptor->file_handle.data.oid_len, &query_desc);

  ReleaseTokenFSCall();

  /* convert error code in case of error */
  if(rc != SNMPERR_SUCCESS && snmp2fsal_error(rc) != ERR_FSAL_NOENT)
    Return(snmp2fsal_error(rc), rc, INDEX_FSAL_read);
  else if(snmp2fsal_error(rc) == ERR_FSAL_NOENT)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_read);

  /* copy data to output buffer */

  rc = snmp_object2str(file_descriptor->p_context->snmp_response->variables,
                       (char *)buffer, &sz);

  if(rc != ERR_FSAL_NO_ERROR)
    Return(rc, 0, INDEX_FSAL_read);

  *read_amount = sz;
  *end_of_file = TRUE;

  LogFullDebug(COMPONENT_FSAL, "buffer_size=%llu, read_amount=%llu\n", buffer_size, *read_amount);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_read);

}

/**
 * FSAL_write:
 * Perform a write operation on an opened file.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param seek_descriptor (optional input):
 *        Specifies the position where data is to be written.
 *        If not specified, data will be written at the current position.
 * \param buffer_size (input):
 *        Amount (in bytes) of data to be written.
 * \param buffer (input):
 *        Address in memory of the data to write to file.
 * \param write_amount (output):
 *        Pointer to the amount of data (in bytes) that have been written
 *        during this call.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_INVAL        (invalid parameter)
 *      - ERR_FSAL_NOT_OPENED   (tried to write in a non-opened snmpfsal_file_t)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ERR_FSAL_NOSPC, ERR_FSAL_DQUOT...
 */
fsal_status_t SNMPFSAL_write(snmpfsal_file_t * file_descriptor, /* IN */
                             fsal_seek_t * seek_descriptor,     /* IN */
                             fsal_size_t buffer_size,   /* IN */
                             caddr_t buffer,    /* IN */
                             fsal_size_t * write_amount /* OUT */
    )
{
  int rc;
  fsal_request_desc_t query_desc;
  size_t sz = buffer_size;
  char tmp_buf[FSALSNMP_MAX_FILESIZE];

  netsnmp_variable_list *p_var;

  /* sanity checks. */
  if(!file_descriptor || !buffer || !write_amount)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_write);

  /* check that file has been opened in write mode  */
  if(file_descriptor->rw_mode != FSAL_MODE_WRITE)
    Return(ERR_FSAL_NOT_OPENED, 0, INDEX_FSAL_write);

  LogFullDebug(COMPONENT_FSAL, "buffer_size=%llu, content='%.*s'\n", buffer_size, (int)buffer_size, buffer);

  /* seek operations are not allowed */

  if(seek_descriptor && seek_descriptor->whence == FSAL_SEEK_END)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_write);

  if(seek_descriptor && seek_descriptor->whence == FSAL_SEEK_SET
     && seek_descriptor->offset != 0)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_write);

  if(seek_descriptor && seek_descriptor->whence == FSAL_SEEK_CUR
     && seek_descriptor->offset != 0)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_write);

  /* first, we make a get request, in order to know the object type */

  query_desc.request_type = SNMP_MSG_GET;

  TakeTokenFSCall();
  rc = IssueSNMPQuery(file_descriptor->p_context, file_descriptor->file_handle.data.oid_tab,
                      file_descriptor->file_handle.data.oid_len, &query_desc);
  ReleaseTokenFSCall();

  /* convert error code in case of error */
  if(rc != SNMPERR_SUCCESS && snmp2fsal_error(rc) != ERR_FSAL_NOENT)
    Return(snmp2fsal_error(rc), rc, INDEX_FSAL_write);
  else if(snmp2fsal_error(rc) == ERR_FSAL_NOENT)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_write);

  p_var = file_descriptor->p_context->snmp_response->variables;

  /* check type */

  if(p_var == NULL
     || p_var->type == SNMP_NOSUCHOBJECT
     || p_var->type == SNMP_NOSUCHINSTANCE || p_var->type == SNMP_ENDOFMIBVIEW)
    Return(ERR_FSAL_STALE, p_var->type, INDEX_FSAL_write);

  /* copy to a temporary buffer */
  if(buffer_size > FSALSNMP_MAX_FILESIZE)
    Return(ERR_FSAL_FBIG, 0, INDEX_FSAL_write);

  memcpy(tmp_buf, buffer, buffer_size);
  sz = buffer_size;

  /* remove '\n' at the end or before the final '\0' */
  if(sz > 0 && tmp_buf[sz - 1] == '\n')
    {
      tmp_buf[sz - 1] = '\0';
      sz--;
    }
  else if(sz > 1 && tmp_buf[sz - 2] == '\n')
    {
      tmp_buf[sz - 2] = '\0';
      sz--;
    }

  query_desc.request_type = SNMP_MSG_SET;
  query_desc.SET_REQUEST_INFO.value = tmp_buf;
  query_desc.SET_REQUEST_INFO.type = ASN2add_var(p_var->type);

  TakeTokenFSCall();

  rc = IssueSNMPQuery(file_descriptor->p_context, file_descriptor->file_handle.data.oid_tab,
                      file_descriptor->file_handle.data.oid_len, &query_desc);

  ReleaseTokenFSCall();

  /* convert error code in case of error */
  if(rc != SNMPERR_SUCCESS && snmp2fsal_error(rc) != ERR_FSAL_NOENT)
    Return(snmp2fsal_error(rc), rc, INDEX_FSAL_write);
  else if(snmp2fsal_error(rc) == ERR_FSAL_NOENT)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_write);

  *write_amount = buffer_size;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_write);

}

/**
 * FSAL_close:
 * Free the resources allocated by the FSAL_open call.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 *
 * \return Major error codes:
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */

fsal_status_t SNMPFSAL_close(snmpfsal_file_t * file_descriptor  /* IN */
    )
{

  int rc;

  /* sanity checks. */
  if(!file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_close);

  memset(file_descriptor, 0, sizeof(snmpfsal_file_t));

  /* release your read/write internal resources */

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_close);

}

/* Some unsupported calls used in FSAL_PROXY, just for permit the ganeshell to compile */
fsal_status_t SNMPFSAL_open_by_fileid(snmpfsal_handle_t * filehandle,   /* IN */
                                      fsal_u64_t fileid,        /* IN */
                                      snmpfsal_op_context_t * p_context,        /* IN */
                                      fsal_openflags_t openflags,       /* IN */
                                      snmpfsal_file_t * file_descriptor,        /* OUT */
                                      fsal_attrib_list_t *
                                      file_attributes /* [ IN/OUT ] */ )
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_open_by_fileid);
}

fsal_status_t SNMPFSAL_close_by_fileid(snmpfsal_file_t * file_descriptor /* IN */ ,
                                       fsal_u64_t fileid)
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_open_by_fileid);
}

unsigned int SNMPFSAL_GetFileno(snmpfsal_file_t * pfile)
{
  return 0;
}
