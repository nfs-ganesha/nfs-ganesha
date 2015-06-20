/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * DISCLAIMER
 * ----------
 * This file is part of FSAL_HPSS.
 * FSAL HPSS provides the glue in-between FSAL API and HPSS CLAPI
 * You need to have HPSS installed to properly compile this file.
 *
 * Linkage/compilation/binding/loading/etc of HPSS licensed Software
 * must occur at the HPSS partner's or licensee's location.
 * It is not allowed to distribute this software as compiled or linked
 * binaries or libraries, as they include HPSS licensed material.
 * -------------
 */

/* xattrs.c
 * VFS object (file|dir) handle object extended attributes
 */

#include "config.h"

#include <assert.h>

#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include <ctype.h>
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_config.h"
#include "FSAL/fsal_commonlib.h"
#include "hpss_methods.h"
#include <stdbool.h>

#include <hpss_limits.h>
#include <hpss_uuid.h>
#include <hpss_xml.h>

#define XATTR_FOR_FILE     0x00000001
#define XATTR_FOR_DIR      0x00000002
#define XATTR_FOR_SYMLINK  0x00000004
#define XATTR_FOR_ALL      0x0000000F
#define XATTR_RO           0x00000100
#define XATTR_RW           0x00000200

typedef int (*xattr_getfunc_t) (struct fsal_obj_handle *,   /* object handle */
				caddr_t,            /* output buff */
				size_t,             /* output buff size */
				size_t *,           /* output size */
				void *arg);         /* optional argument */

typedef int (*xattr_setfunc_t) (struct fsal_obj_handle *,   /* object handle */
				caddr_t,            /* input buff */
				size_t,             /* input size */
				int,                /* creation flag */
				void *arg);         /* optional argument */

struct fsal_xattr_def {
	char xattr_name[MAXNAMLEN];
	xattr_getfunc_t get_func;
	xattr_setfunc_t set_func;
	int flags;
};

/* utility functions */
static int hpss_uda_name_2_fsal(const char *src, char *out)
{
	const char *curr_src = src;
	char *curr = out;

	/* skip first '/' */
	while ((*curr_src == '/') && (*curr_src != '\0'))
		curr_src++;

	if (*curr_src == '\0')
		return ERR_FSAL_INVAL;

	strcpy(curr, curr_src);
	while ((curr = strchr(out, '/')) != NULL)
		*curr = '.';

	return 0;
}

static int fsal_xattr_name_2_uda(const char *src, char *out)
{
	char *curr = out;

	/* add first / */
	*curr = '/';
	curr++;

	/* copy the xattr name */
	strcpy(curr, src);

	/* then replace '.' with '/' */
	while ((curr = strchr(out, '.')) != NULL)
		*curr = '/';

	/* UDA path must start with '/hpss/' */
	if (strncmp(out, "/hpss/", 6) != 0)
		return ERR_FSAL_INVAL;

	return 0;
}


/*
 * DEFINE GET/SET FUNCTIONS
 */

static int hpss_get_ns_handle(struct fsal_obj_handle *fsal_obj_hdl,
			      caddr_t buffer_addr,
			      size_t buffer_size,
			      size_t *p_output_size,
			      void *arg)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	struct ns_ObjHandle *ns_hdl;
	char *tmp_str_uuid;
	int rc;

	/* sanity checks. */
	if (!fsal_obj_hdl || !p_output_size || !buffer_addr)
		return ERR_FSAL_FAULT;

	obj_hdl =
		 container_of(fsal_obj_hdl,
			      struct hpss_fsal_obj_handle,
			      obj_handle);
	ns_hdl = &obj_hdl->handle->ns_handle;

	uuid_to_string(&(ns_hdl->CoreServerUUID), (char **)&tmp_str_uuid, &rc);
	if (rc != 0)
		return hpss2fsal_error(rc);

	*p_output_size = snprintf(buffer_addr,
				  buffer_size,
				  "ObjId: %#"PRIx64"\nFileId: %#"PRIx64
				  "\nType: %hu\nFlags: %hu\nGeneration: %#x\n"
				  "CoreServerUUID: %s\n",
				  ns_hdl->ObjId,
				  ns_hdl->FileId,
				  ns_hdl->Type,
				  ns_hdl->Flags,
				  ns_hdl->Generation,
				  tmp_str_uuid);

/* HPSS returns a string that it has just allocated.
 * Free it to avoid memory leak.
 */
	free(tmp_str_uuid);

	return 0;
}

static int hpss_get_bfid(struct fsal_obj_handle *fsal_obj_hdl,
			 caddr_t buffer_addr,
			 size_t buffer_size,
			 size_t *p_output_size,
			 void *arg)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	sec_cred_t *p_ucreds = arg;
	hpss_vattr_t hpss_vattr;
	char *tmp_str_uuid;
	int rc;

	/* sanity checks. */
	if (!fsal_obj_hdl || !p_output_size || !buffer_addr || !p_ucreds)
		return ERR_FSAL_FAULT;


	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);

	rc = hpss_GetAttrHandle(&(obj_hdl->handle->ns_handle),
				NULL,
				p_ucreds,
				NULL,
				&hpss_vattr);

	/**
	 * /!\ WARNING : When the directory handle is stale, HPSS returns ENOTDIR.
	 *     Thus, in this case, we must double check
	 *     by checking the directory handle.
	 */
	if (rc == HPSS_ENOTDIR)
		if (HPSSFSAL_IsStaleHandle(&obj_hdl->handle->ns_handle,
					  p_ucreds))
			return ERR_FSAL_STALE;

	if (rc)
		return hpss2fsal_error(rc);

	uuid_to_string(&(hpss_vattr.va_soid.ObjectID),
		      (char **)&tmp_str_uuid, &rc);
	if (rc != 0)
		return hpss2fsal_error(rc);

	*p_output_size = snprintf(buffer_addr, buffer_size, "%s", tmp_str_uuid);

	/* HPSS returns a string that it has just allocated.
	 * Free it to avoid memory leak.
	 */
	free(tmp_str_uuid);

	return 0;
}

static int hpss_get_file_cos(struct fsal_obj_handle *fsal_obj_hdl,
			     caddr_t buffer_addr,
			     size_t buffer_size,
			     size_t *p_output_size,
			     void *arg)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	sec_cred_t *p_ucreds = arg;
	hpss_vattr_t hpss_vattr;
	int rc;

	/* sanity checks. */
	if (!fsal_obj_hdl || !p_output_size || !buffer_addr || !p_ucreds)
		return ERR_FSAL_FAULT;

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);

	rc = hpss_GetAttrHandle(&(obj_hdl->handle->ns_handle),
				NULL,
				p_ucreds,
				NULL,
				&hpss_vattr);

	/**
	 * /!\ WARNING : When the directory handle is stale,
	 *     HPSS returns ENOTDIR.
	 *     Thus, in this case, we must double check
	 *     by checking the directory handle.
	 */
	if (rc == HPSS_ENOTDIR)
		if (HPSSFSAL_IsStaleHandle(&obj_hdl->handle->ns_handle,
					  p_ucreds))
			return ERR_FSAL_STALE;

	if (rc)
		return hpss2fsal_error(rc);

	*p_output_size =
		snprintf(buffer_addr, buffer_size, "%i", hpss_vattr.va_cos);

	return 0;
}

static int hpss_get_file_slevel(struct fsal_obj_handle *fsal_obj_hdl,
				caddr_t buffer_addr,
				size_t buffer_size,
				size_t *p_output_size,
				void *arg)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	sec_cred_t *p_ucreds = arg;
	hpss_xfileattr_t hpss_xattr;
	char tmpstr[1024];
	char *outbuff;
	int rc, i, j;

	/* sanity checks. */
	if (!fsal_obj_hdl || !p_output_size || !buffer_addr || !p_ucreds)
		return ERR_FSAL_FAULT;


	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);


	rc = hpss_FileGetXAttributesHandle(&(obj_hdl->handle->ns_handle),
					   NULL,
					   p_ucreds,
					   API_GET_STATS_FOR_ALL_LEVELS,
					   0,
					   &hpss_xattr);

	if (rc == HPSS_ENOENT)
		return ERR_FSAL_STALE;

	if (rc)
		return hpss2fsal_error(rc);

	/* now write info to buffer */
	outbuff = (char *)buffer_addr;
	outbuff[0] = '\0';

	for (i = 0; i < HPSS_MAX_STORAGE_LEVELS; i++) {
		if (hpss_xattr.SCAttrib[i].Flags == 0)
			continue;

		if (hpss_xattr.SCAttrib[i].Flags & BFS_BFATTRS_LEVEL_IS_DISK)
			snprintf(tmpstr, 1024,
				 "Level %u (disk): %"PRIu64" bytes\n", i,
				 hpss2fsal_64(hpss_xattr.SCAttrib[i].
						BytesAtLevel));
		else
			if (hpss_xattr.SCAttrib[i].Flags &
				 BFS_BFATTRS_LEVEL_IS_TAPE)
				snprintf(tmpstr, 1024,
					 "Level %u (tape): %"PRIu64" bytes\n",
					 i,
					 hpss2fsal_64(hpss_xattr.SCAttrib[i].
						BytesAtLevel));
			else
				snprintf(tmpstr, 1024,
					 "Level %u: %"PRIu64" bytes\n", i,
					 hpss2fsal_64(hpss_xattr.SCAttrib[i].
						BytesAtLevel));

		if (strlen(tmpstr) + strlen(outbuff) < buffer_size)
			strcat(outbuff, tmpstr);
		else
			break;
	 }

	/* free the returned structure (Cf. HPSS ClAPI documentation) */
	for (i = 0; i < HPSS_MAX_STORAGE_LEVELS; i++)
		for (j = 0; j < hpss_xattr.SCAttrib[i].NumberOfVVs; j++)
			if (hpss_xattr.SCAttrib[i].VVAttrib[j].PVList != NULL)
				free(hpss_xattr.SCAttrib[i].VVAttrib[j].
						PVList);

	*p_output_size = strlen(outbuff);

	return 0;
}

/* DEFINE HERE YOUR ATTRIBUTES LIST */

static struct fsal_xattr_def xattr_list[] = {
	/* for all kind of entries */
	{"ns_handle", hpss_get_ns_handle, NULL, XATTR_FOR_ALL|XATTR_RO},

	/* for files only */
	{"bitfile_id", hpss_get_bfid, NULL, XATTR_FOR_FILE|XATTR_RO},
	{"class_of_service", hpss_get_file_cos, NULL, XATTR_FOR_FILE|XATTR_RO},
	{"storage_levels", hpss_get_file_slevel, NULL, XATTR_FOR_FILE|XATTR_RO}
};

#define XATTR_COUNT 5

/* we assume that this number is < 254 */
#if (XATTR_COUNT > 254)
#error "ERROR: xattr count > 254"
#endif

/* test if an object has a given attribute */
static int do_match_type(int xattr_flag, object_file_type_t obj_type)
{
	switch (obj_type) {
	case REGULAR_FILE:
		return ((xattr_flag & XATTR_FOR_FILE) == XATTR_FOR_FILE);

	case DIRECTORY:
		return ((xattr_flag & XATTR_FOR_DIR) == XATTR_FOR_DIR);

	case SYMBOLIC_LINK:
		return ((xattr_flag & XATTR_FOR_SYMLINK) == XATTR_FOR_SYMLINK);

	default:
		return ((xattr_flag & XATTR_FOR_ALL) == XATTR_FOR_ALL);
	}
}

static int attr_is_read_only(unsigned int attr_index)
{
	if (attr_index < XATTR_COUNT)
		if (xattr_list[attr_index].flags & XATTR_RO)
			return TRUE;

	/* else : UDA */
	return FALSE;
}

static int file_attributes_to_xattr_attrs(struct attrlist *file_attrs,
					  struct attrlist *xattr_attrs,
					  unsigned int attr_index)
{
	/* supported attributes are:
	 * - owner (same as the objet)
	 * - group (same as the objet)
	 * - type FSAL_TYPE_XATTR
	 * - fileid (attr index ? or (fileid^((index+1)<<24)))
	 * - mode (config & file)
	 * - atime, mtime, ctime = these of the object ?
	 * - size=1block, used=1block
	 * - rdev=0
	 * - nlink=1
	 */
	const attrmask_t supported = ATTR_MODE | ATTR_FILEID
				   | ATTR_TYPE | ATTR_OWNER | ATTR_GROUP
				   | ATTR_ATIME | ATTR_MTIME | ATTR_CTIME
				   | ATTR_CREATION | ATTR_CHGTIME | ATTR_SIZE
				   | ATTR_SPACEUSED | ATTR_NUMLINKS
				   | ATTR_RAWDEV | ATTR_FSID;

	xattr_attrs->mask = supported & file_attrs->mask;

	if (xattr_attrs->mask & ATTR_MODE) {
		xattr_attrs->mode = file_attrs->mode;

	if (attr_is_read_only(attr_index))
		xattr_attrs->mode &= ~(0222);
	}

	if (xattr_attrs->mask & ATTR_FILEID) {
		unsigned int i;
		unsigned long hash = attr_index + 1;
		char *str = (char *)&file_attrs->fileid;

		for (i = 0; i < sizeof(xattr_attrs->fileid); i++, str++)
			hash = (hash << 5) - hash + (unsigned long)(*str);

		xattr_attrs->fileid = hash;
	}

	if (xattr_attrs->mask & ATTR_TYPE)
		xattr_attrs->type = EXTENDED_ATTR;

	if (xattr_attrs->mask & ATTR_OWNER)
		xattr_attrs->owner = file_attrs->owner;

	if (xattr_attrs->mask & ATTR_GROUP)
		xattr_attrs->group = file_attrs->group;

	if (xattr_attrs->mask & ATTR_ATIME)
		xattr_attrs->atime = file_attrs->atime;

	if (xattr_attrs->mask & ATTR_MTIME)
		xattr_attrs->mtime = file_attrs->mtime;

	if (xattr_attrs->mask & ATTR_CTIME)
		xattr_attrs->ctime = file_attrs->ctime;

	if (xattr_attrs->mask & ATTR_CREATION)
		xattr_attrs->creation = file_attrs->creation;

	if (xattr_attrs->mask & ATTR_CHGTIME) {
		xattr_attrs->chgtime = file_attrs->chgtime;
		xattr_attrs->change = (uint64_t) xattr_attrs->chgtime.tv_sec;
	}

	if (xattr_attrs->mask & ATTR_SIZE)
		xattr_attrs->filesize = DEV_BSIZE;

	if (xattr_attrs->mask & ATTR_SPACEUSED)
		xattr_attrs->spaceused = DEV_BSIZE;

	if (xattr_attrs->mask & ATTR_NUMLINKS)
		xattr_attrs->numlinks = 1;

	if (xattr_attrs->mask & ATTR_RAWDEV) {
		xattr_attrs->rawdev.major = 0;
		xattr_attrs->rawdev.minor = 0;
	}

	if (xattr_attrs->mask & ATTR_FSID)
		xattr_attrs->fsid = file_attrs->fsid;

	/* if mode==0, then owner is set to root and mode is set to 0600 */
	if ((xattr_attrs->mask & ATTR_OWNER) &&
	    (xattr_attrs->mask & ATTR_MODE) && (xattr_attrs->mode == 0)) {
		xattr_attrs->owner = 0;
		xattr_attrs->mode = 0600;
		if (attr_is_read_only(attr_index))
			xattr_attrs->mode &= ~(0200);
	}

	return 0;
}


fsal_status_t hpss_list_ext_attrs(struct fsal_obj_handle *fsal_obj_hdl,
				  unsigned int cookie,
				  fsal_xattrent_t *xattrs_tab,
				  unsigned int xattrs_tabsize,
				  unsigned int *p_nb_returned,
				  int *end_of_list)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	int index, out_index, rc;
	sec_cred_t ucreds;
	hpss_userattr_list_t attr_list;

	if (!fsal_obj_hdl || !xattrs_tab || !p_nb_returned || !end_of_list)
		return fsalstat(ERR_FSAL_FAULT, 0);

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);
	HPSSFSAL_ucreds_from_opctx(op_ctx, &ucreds);

	for (index = cookie,
	     out_index = 0;
	     index < XATTR_COUNT && out_index < xattrs_tabsize; index++) {
		if (do_match_type(xattr_list[index].flags,
				  fsal_obj_hdl->type)) {

			/* fills an xattr entry */
			xattrs_tab[out_index].xattr_id = index;
			memcpy(xattrs_tab[out_index].xattr_name,
			       xattr_list[index].xattr_name,
			       strlen(xattr_list[index].xattr_name)+1);
			xattrs_tab[out_index].xattr_cookie = index + 1;

			/* set asked attributes */
			file_attributes_to_xattr_attrs(
					&obj_hdl->attributes,
					&xattrs_tab[out_index].attributes,
					index);

			/* next output slot */
			out_index++;
		}
	}

	/* save a call if output array is full */
	if (out_index == xattrs_tabsize) {
		*end_of_list = FALSE;
		*p_nb_returned = out_index;

		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	/* get list of UDAs for this entry */
	memset(&attr_list, 0, sizeof(hpss_userattr_list_t));

	rc = hpss_UserAttrListAttrHandle(&(obj_hdl->handle->ns_handle),
					 NULL,
					 &ucreds,
					 &attr_list,
					 XML_ATTR);

	if (rc == -ENOENT)
		attr_list.len = 0;
	else
		if (rc)
			return fsalstat(hpss2fsal_error(rc), -rc);

	unsigned int i;

	for (i = 0; (i < attr_list.len) && (out_index < xattrs_tabsize); i++) {
		char attr_name[MAXNAMLEN];

		/* the id is XATTR_COUNT + index of HPSS UDA */
		index = XATTR_COUNT + i;

		/* continue while index < cookie */
		if (index < cookie)
			continue;

		xattrs_tab[out_index].xattr_id = index;

		if (strlen(attr_list.Pair[i].Key) >= MAXNAMLEN)
			return fsalstat(ERR_FSAL_NAMETOOLONG, 0);

		/* HPSS UDAs namespace is slash-separated.
		 * we convert '/' to '.'
		 */
		rc = hpss_uda_name_2_fsal(attr_list.Pair[i].Key, attr_name);

		if (rc != ERR_FSAL_NO_ERROR)
			return fsalstat(rc, 0);

		memcpy(xattrs_tab[out_index].xattr_name,
		       attr_name,
		       strlen(attr_name)+1);
		xattrs_tab[out_index].xattr_cookie = index + 1;

		/* set asked attributes */
		file_attributes_to_xattr_attrs(
					&obj_hdl->attributes,
					&xattrs_tab[out_index].attributes,
					index);

		/* we know the size here (+2 for \n\0) */
		if (attr_list.Pair[i].Value != NULL)
			xattrs_tab[out_index].attributes.filesize =
				 strlen(attr_list.Pair[i].Value) + 2;

	      /* next output slot */
	      out_index++;
	}

	/* Allocated by hpss - use free */
	for (index = 0; index < attr_list.len; index++) {
		free(attr_list.Pair[index].Key);
		free(attr_list.Pair[index].Value);
	}
	free(attr_list.Pair);

	/* not end of list if there is more UDAs */
	if (i < attr_list.len)
		*end_of_list = FALSE;
	else
		*end_of_list = TRUE;

	*p_nb_returned = out_index;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t hpss_getextattr_id_by_name(struct fsal_obj_handle *fsal_obj_hdl,
					 const char *xattr_name,
					 unsigned int *pxattr_id)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	unsigned int index;
	int found = FALSE;

	if (!fsal_obj_hdl || !xattr_name || !pxattr_id)
		return fsalstat(ERR_FSAL_FAULT, 0);

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);

	for (index = 0; index < XATTR_COUNT; index++) {
		if (do_match_type(xattr_list[index].flags,
				  fsal_obj_hdl->type) &&
		    !strcmp(xattr_list[index].xattr_name,
			    xattr_name)) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
		/* search for name in UDAs */
		hpss_userattr_list_t attr_list;
		unsigned int i;
		int rc;
		char attrpath[MAXNAMLEN];
		sec_cred_t ucreds;

		HPSSFSAL_ucreds_from_opctx(op_ctx, &ucreds);

		/* convert FSAL xattr name to HPSS attr path.
		 * returns error if it is not a UDA name.
		 */
		if (fsal_xattr_name_2_uda(xattr_name,
					  attrpath) == 0) {
			memset(&attr_list, 0, sizeof(hpss_userattr_list_t));

			LogFullDebug(COMPONENT_FSAL,
				     "looking for xattr '%s' in UDAs",
				     xattr_name);

		/* get list of UDAs, and return the good index*/

		rc = hpss_UserAttrListAttrHandle(&(obj_hdl->handle->ns_handle),
						 NULL,
						 &ucreds,
						 &attr_list,
						 XML_ATTR);

		if (rc == 0)
			for (i = 0; i < attr_list.len; i++)
				if (!strcmp(attr_list.Pair[i].Key, attrpath)) {
					/* xattr index is XATTR_COUNT +
					 * UDA index */
					index = XATTR_COUNT + i;
					found = TRUE;
					break;
				}

		/* Allocated by hpss - use free */
		for (i = 0; i < attr_list.len; i++) {
			free(attr_list.Pair[i].Key);
			free(attr_list.Pair[i].Value);
		}
		free(attr_list.Pair);
		}
	}

	if (found) {
		*pxattr_id = index;
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	} else
		return fsalstat(ERR_FSAL_NOENT, ENOENT);
}

fsal_status_t hpss_getextattr_value_by_id(struct fsal_obj_handle *fsal_obj_hdl,
					  unsigned int xattr_id,
					  caddr_t buffer_addr,
					  size_t buffer_size,
					  size_t *p_output_size)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	int rc, i;
	sec_cred_t ucreds;
	hpss_userattr_list_t attr_list;

	if (!fsal_obj_hdl || !p_output_size)
		return fsalstat(ERR_FSAL_FAULT, 0);

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);
	HPSSFSAL_ucreds_from_opctx(op_ctx, &ucreds);

	/* check that this index match the type of entry */
	if (xattr_id < XATTR_COUNT) {
		if (!do_match_type(xattr_list[xattr_id].flags,
				   fsal_obj_hdl->type))
			return fsalstat(ERR_FSAL_INVAL, 0);

	rc = xattr_list[xattr_id].get_func(fsal_obj_hdl,
					   buffer_addr,
					   buffer_size,
					   p_output_size,
					   &ucreds);
	} else
		if (xattr_id >= XATTR_COUNT) {
			memset(&attr_list, 0, sizeof(hpss_userattr_list_t));

			LogFullDebug(COMPONENT_FSAL,
				     "Getting value for UDA #%u",
				     xattr_id - XATTR_COUNT);

		/* get list of UDAs for this entry, and
		 * return the good value */
		rc = hpss_UserAttrListAttrHandle(&(obj_hdl->handle->ns_handle),
						 NULL,
						 &ucreds,
						 &attr_list,
						 XML_ATTR);

		if (rc != 0)
			return fsalstat(hpss2fsal_error(rc), rc);

		else
			if (xattr_id - XATTR_COUNT >= attr_list.len)
				return fsalstat(ERR_FSAL_STALE, 0);

		if ((attr_list.Pair[xattr_id - XATTR_COUNT].Value != NULL) &&
		    (attr_list.Pair[xattr_id - XATTR_COUNT].Value[0] != '\0'))
			*p_output_size =
				 snprintf((char *)buffer_addr,
					  buffer_size, "%s\n",
			attr_list.Pair[xattr_id - XATTR_COUNT].Value);
		else {
			((char *)buffer_addr)[0] = '\0';
			*p_output_size = 0;
		}

		/* Allocated by hpss - use free */
		for (i = 0; i < attr_list.len; i++) {
			free(attr_list.Pair[i].Key);
			free(attr_list.Pair[i].Value);
		}
		free(attr_list.Pair);

		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}
	return fsalstat(rc, 0);
}


fsal_status_t hpss_getextattr_value_by_name(
				struct fsal_obj_handle *fsal_obj_hdl,
				const char *xattr_name,
				caddr_t buffer_addr,
				size_t buffer_size,
				size_t *p_output_size)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	int rc, index;
	sec_cred_t ucreds;
	hpss_userattr_list_t attr;
	char attrpath[MAXNAMLEN];
	char attrval[MAXPATHLEN];

	if (!fsal_obj_hdl || !p_output_size)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* check if this is an indexed fake xattr */
	for (index = 0; index < XATTR_COUNT; index++)
		if (do_match_type(xattr_list[index].flags,
				  fsal_obj_hdl->type)
		    && !strcmp(xattr_list[index].xattr_name, xattr_name))
			return hpss_getextattr_value_by_id(fsal_obj_hdl,
							   index,
							   buffer_addr,
							   buffer_size,
							   p_output_size);

	obj_hdl =
		 container_of(fsal_obj_hdl,
			      struct hpss_fsal_obj_handle,
			      obj_handle);
	HPSSFSAL_ucreds_from_opctx(op_ctx, &ucreds);


	if (fsal_xattr_name_2_uda(xattr_name, attrpath) == 0) {
		attr.len = 1;
		/* use malloc because HPSS may free it */
		attr.Pair = malloc(sizeof(hpss_userattr_t));
		if (attr.Pair == NULL)
			return fsalstat(ERR_FSAL_NOMEM, errno);

		attr.Pair[0].Key = attrpath;
		attr.Pair[0].Value = attrval;

		rc = hpss_UserAttrGetAttrHandle(&(obj_hdl->handle->ns_handle),
						NULL,
						&ucreds,
						&attr,
						UDA_API_VALUE);
		if (rc) {
			free(attr.Pair);
			return fsalstat(hpss2fsal_error(rc), rc);
		}

		if (attr.len > 0) {
			if (attr.Pair[0].Value != NULL) {
				char *noxml = hpss_ChompXMLHeader(
							attr.Pair[0].Value,
							NULL);
				strcpy(attrval, noxml);
				free(noxml);
				strncpy((char *)buffer_addr,
					 attrval,
					 buffer_size);
				*p_output_size = strlen(attrval) + 1;
			} else {
				strcpy((char *)buffer_addr, "");
				*p_output_size = 1;
			}

			free(attr.Pair);
			rc = 0;
		} else {
			free(attr.Pair);
			rc = ERR_FSAL_NOENT;
		}
	}

	return fsalstat(rc, 0);
}

fsal_status_t hpss_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				    unsigned int xattr_id,
				    struct attrlist *p_attrs)
{
	int rc;

	/* sanity checks */
	if (!obj_hdl || !p_attrs)
		return fsalstat(ERR_FSAL_FAULT, 0);


	/* check that this index match the type of entry */
	if (xattr_id < XATTR_COUNT &&
	    !do_match_type(xattr_list[xattr_id].flags,
			   obj_hdl->attrs->type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	if (xattr_id >= XATTR_COUNT)
		LogFullDebug(COMPONENT_FSAL,
			     "Getting attributes for xattr #%u",
			      xattr_id - XATTR_COUNT);

	rc = file_attributes_to_xattr_attrs(obj_hdl->attrs,
					    p_attrs, xattr_id);
	if (rc)
		return fsalstat(ERR_FSAL_INVAL, rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t hpss_setextattr_value(struct fsal_obj_handle *fsal_obj_hdl,
				    const char *xattr_name,
				    caddr_t buffer_addr,
				    size_t buffer_size,
				    int create)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	int rc, index;
	sec_cred_t ucreds;
	hpss_userattr_list_t attr;
	char attrpath[MAXNAMLEN];

	if (!fsal_obj_hdl || !xattr_name || !buffer_addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	if (((char *)buffer_addr)[0] == '\0')
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	/* check if this is an indexed fake xattr */
	for (index = 0; index < XATTR_COUNT; index++)
		if (do_match_type(xattr_list[index].flags,
				  fsal_obj_hdl->type) &&
		    !strcmp(xattr_list[index].xattr_name, xattr_name))
			return hpss_setextattr_value_by_id(fsal_obj_hdl,
							   index,
							   buffer_addr,
							   buffer_size);

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);
	  HPSSFSAL_ucreds_from_opctx(op_ctx, &ucreds);

	/* convert FSAL xattr name to HPSS attr path.
	 * returns error if it is not a UDA name.
	 */
	if (fsal_xattr_name_2_uda(xattr_name, attrpath) == 0) {
		attr.len = 1;
		/* use malloc because HPSS may free it */
		attr.Pair = malloc(sizeof(hpss_userattr_t));
		if (attr.Pair == NULL)
			return fsalstat(ERR_FSAL_NOMEM, errno);

		attr.Pair[0].Key = attrpath;
		attr.Pair[0].Value = buffer_addr;

		rc = hpss_UserAttrSetAttrHandle(&(obj_hdl->handle->ns_handle),
						NULL,
						&ucreds,
						&attr,
						UDA_API_VALUE);
		free(attr.Pair);
		if (rc)
			return fsalstat(hpss2fsal_error(rc), rc);
		else
			return fsalstat(ERR_FSAL_INVAL, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t hpss_setextattr_value_by_id(struct fsal_obj_handle *fsal_obj_hdl,
					  unsigned int xattr_id,
					  caddr_t buffer_addr,
					  size_t buffer_size)
{
	struct hpss_fsal_obj_handle *obj_hdl;
	int rc, index, listlen;
	sec_cred_t ucreds;
	hpss_userattr_list_t attr_list;

	if (!fsal_obj_hdl || !buffer_addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	if (attr_is_read_only(xattr_id))
		return fsalstat(ERR_FSAL_PERM, 0);

	if (((char *)buffer_addr)[0] == '\0')
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	obj_hdl = container_of(fsal_obj_hdl,
			       struct hpss_fsal_obj_handle,
			       obj_handle);
	HPSSFSAL_ucreds_from_opctx(op_ctx, &ucreds);

	if (xattr_id < XATTR_COUNT)
		rc = xattr_list[xattr_id].set_func(fsal_obj_hdl,
						   buffer_addr,
						   buffer_size,
						   0,
						   &ucreds);
	else {
		memset(&attr_list, 0, sizeof(hpss_userattr_list_t));

		LogFullDebug(COMPONENT_FSAL, "Setting value for UDA #%u",
			     xattr_id - XATTR_COUNT);

		/* get list of UDAs for this entry, and return the good value */
		rc = hpss_UserAttrListAttrHandle(&(obj_hdl->handle->ns_handle),
						 NULL,
						 &ucreds,
						 &attr_list,
						 XML_ATTR);

		if (rc != 0)
			return fsalstat(hpss2fsal_error(rc), rc);

		else
			if (xattr_id - XATTR_COUNT >= attr_list.len)
				return fsalstat(ERR_FSAL_STALE, 0);

		listlen = attr_list.len;

		attr_list.Pair[0].Key =
			 attr_list.Pair[xattr_id - XATTR_COUNT].Key;
		attr_list.Pair[0].Value = buffer_addr;
		attr_list.len = 1;

		rc = hpss_UserAttrSetAttrHandle(&(obj_hdl->handle->ns_handle),
						NULL,
						&ucreds,
						&attr_list,
						UDA_API_VALUE);

		/* Allocated by hpss - use free */
		for (index = 0; index < listlen; index++) {
			free(attr_list.Pair[index].Key);
			free(attr_list.Pair[index].Value);
		}
		free(attr_list.Pair);

		if (rc)
			rc = hpss2fsal_error(rc);
	}

	  return fsalstat(rc, 0);
}

fsal_status_t hpss_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					unsigned int xattr_id)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

fsal_status_t hpss_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					  const char *xattr_name)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0);
}

