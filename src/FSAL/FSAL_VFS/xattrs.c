/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* xattrs.c
 * VFS object (file|dir) handle object extended attributes
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <os/xattr.h>
#include <ctype.h>
#include "ganesha_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "vfs_methods.h"
typedef int (*xattr_getfunc_t) (struct fsal_obj_handle *, /* object handle */
				caddr_t,	/* output buff */
				size_t,	/* output buff size */
				size_t *,	/* output size */
				void *arg);	/* optionnal argument */

typedef int (*xattr_setfunc_t) (struct fsal_obj_handle *, /* object handle */
				caddr_t,	/* input buff */
				size_t,	/* input size */
				int,	/* creation flag */
				void *arg);	/* optionnal argument */

struct fsal_xattr_def {
	char xattr_name[MAXNAMLEN + 1];
	xattr_getfunc_t get_func;
	xattr_setfunc_t set_func;
	int flags;
	void *arg;
};

/*
 * DEFINE GET/SET FUNCTIONS
 */

int print_vfshandle(struct fsal_obj_handle *obj_hdl, caddr_t buffer_addr,
		    size_t buffer_size, size_t *p_output_size, void *arg)
{
	*p_output_size =
	    snprintf(buffer_addr, buffer_size, "(not yet implemented)");

	return 0;
}				/* print_fid */

/* DEFINE HERE YOUR ATTRIBUTES LIST */

static struct fsal_xattr_def xattr_list[] = {
	{"vfshandle", print_vfshandle, NULL, XATTR_FOR_ALL | XATTR_RO, NULL},
};

#define XATTR_COUNT 1
#define XATTR_SYSTEM (INT_MAX - 1)

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
	if (attr_index < XATTR_COUNT) {
		if (xattr_list[attr_index].flags & XATTR_RO)
			return TRUE;
	}
	/* else : standard xattr */
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
	 * - fileid (attr index ? or (fileid^((index+1)<<24)) )
	 * - mode (config & file)
	 * - atime, mtime, ctime = these of the object ?
	 * - size=1block, used=1block
	 * - rdev=0
	 * - nlink=1
	 */
	attrmask_t supported =
	    ATTR_MODE | ATTR_FILEID | ATTR_TYPE | ATTR_OWNER | ATTR_GROUP |
	    ATTR_ATIME | ATTR_MTIME | ATTR_CTIME | ATTR_CREATION | ATTR_CHGTIME
	    | ATTR_SIZE | ATTR_SPACEUSED | ATTR_NUMLINKS | ATTR_RAWDEV |
	    ATTR_FSID;
	attrmask_t unsupp;

	if (xattr_attrs->mask == 0) {
		xattr_attrs->mask = supported;

		LogCrit(COMPONENT_FSAL,
			"Error: xattr_attrs->mask was 0 in %s() line %d, file %s",
			__func__, __LINE__, __FILE__);
	}

	unsupp = xattr_attrs->mask & (~supported);

	if (unsupp) {
		LogDebug(COMPONENT_FSAL,
			 "Asking for unsupported attributes in %s(): %#llX removing it from asked attributes",
			 __func__, (long long unsigned int)unsupp);

		xattr_attrs->mask &= (~unsupp);
	}

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
		xattr_attrs->change = timespec_to_nsecs(&xattr_attrs->chgtime);
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
	if ((xattr_attrs->mask & ATTR_OWNER)
	    && (xattr_attrs->mask & ATTR_MODE) && (xattr_attrs->mode == 0)) {
		xattr_attrs->owner = 0;
		xattr_attrs->mode = 0600;
		if (attr_is_read_only(attr_index))
			xattr_attrs->mode &= ~(0200);
	}

	return 0;

}

static int xattr_id_to_name(int fd, unsigned int xattr_id, char *name)
{
	unsigned int index;
	unsigned int curr_idx;
	char names[MAXPATHLEN], *ptr;
	ssize_t namesize;
	size_t len = 0;

	if (xattr_id < XATTR_COUNT)
		return ERR_FSAL_INVAL;

	index = xattr_id - XATTR_COUNT;

	/* get xattrs */

	namesize = flistxattr(fd, names, sizeof(names));

	if (namesize < 0)
		return ERR_FSAL_NOENT;

	errno = 0;

	if (xattr_id == XATTR_SYSTEM) {
		strcpy(name, "system.posix_acl_access");
		return ERR_FSAL_NO_ERROR;
	}

	for (ptr = names, curr_idx = 0; ptr < names + namesize;
	     curr_idx++, ptr += len + 1) {
		len = strlen(ptr);
		if (curr_idx == index) {
			strcpy(name, ptr);
			return ERR_FSAL_NO_ERROR;
		}
	}
	return ERR_FSAL_NOENT;
}

/**
 *  return index if found,
 *  negative value on error.
 */
static int xattr_name_to_id(int fd, const char *name)
{
	unsigned int i;
	char names[MAXPATHLEN], *ptr;
	ssize_t namesize;

	/* get xattrs */

	namesize = flistxattr(fd, names, sizeof(names));

	if (namesize < 0)
		return -ERR_FSAL_NOENT;

	if (!strncmp(name, "system.posix_acl_access", MAXNAMLEN))
		return XATTR_SYSTEM;

	for (ptr = names, i = 0; ptr < names + namesize;
	     i++, ptr += strlen(ptr) + 1) {
		if (!strcmp(name, ptr))
			return i + XATTR_COUNT;
	}
	return -ERR_FSAL_NOENT;
}

fsal_status_t vfs_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				 unsigned int argcookie,
				 fsal_xattrent_t *xattrs_tab,
				 unsigned int xattrs_tabsize,
				 unsigned int *p_nb_returned, int *end_of_list)
{
	unsigned int index;
	unsigned int out_index;
	unsigned int cookie = argcookie;
	struct vfs_fsal_obj_handle *obj_handle = NULL;
	int fd = -1;
	fsal_errors_t fe;

	char names[MAXPATHLEN], *ptr;
	ssize_t namesize;
	int xattr_idx;

	obj_handle =
	    container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	/* Deal with special cookie */
	if (cookie == XATTR_RW_COOKIE)
		cookie = XATTR_COUNT;

	for (index = cookie, out_index = 0;
	     index < XATTR_COUNT && out_index < xattrs_tabsize; index++) {
		if (do_match_type
		    (xattr_list[index].flags, obj_hdl->attributes.type)) {
			/* fills an xattr entry */
			xattrs_tab[out_index].xattr_id = index;
			strncpy(xattr_list[index].xattr_name,
				xattrs_tab[out_index].xattr_name, MAXNAMLEN);
			xattr_list[index].xattr_name[MAXNAMLEN] = '\0';
			xattrs_tab[out_index].xattr_cookie = index + 1;

			/* set asked attributes (all supported) */
			xattrs_tab[out_index].attributes.mask =
			    obj_hdl->attributes.mask;

			if (file_attributes_to_xattr_attrs
			    (&obj_hdl->attributes,
			     &xattrs_tab[out_index].attributes, index)) {
				/* set error flag */
				xattrs_tab[out_index].attributes.mask =
				    ATTR_RDATTR_ERR;
			}

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

	/* get the path of the file in Lustre */
	fd = (obj_hdl->type == DIRECTORY) ?
	    vfs_fsal_open(obj_handle, O_DIRECTORY, &fe) :
	    vfs_fsal_open(obj_handle, O_RDWR, &fe);
	if (fd < 0)
		return fsalstat(fe, -fd);

	/* get xattrs */

	namesize = flistxattr(fd, names, sizeof(names));

	if (namesize >= 0) {
		size_t len = 0;

		errno = 0;

		for (ptr = names, xattr_idx = 0;
		     (ptr < names + namesize) && (out_index < xattrs_tabsize);
		     xattr_idx++, ptr += len + 1) {
			len = strlen(ptr);
			index = XATTR_COUNT + xattr_idx;

			/* skip if index is before cookie */
			if (index < cookie)
				continue;

			/* fills an xattr entry */
			xattrs_tab[out_index].xattr_id = index;
			strncpy(xattrs_tab[out_index].xattr_name, ptr, len + 1);
			xattrs_tab[out_index].xattr_cookie = index + 1;

			/* set asked attributes (all supported) */
			xattrs_tab[out_index].attributes.mask =
			    obj_hdl->attributes.mask;

			if (file_attributes_to_xattr_attrs
			    (&obj_hdl->attributes,
			     &xattrs_tab[out_index].attributes, index)) {
				/* set error flag */
				xattrs_tab[out_index].attributes.mask =
				    ATTR_RDATTR_ERR;
			}

			/* next output slot */
			out_index++;
		}
		/* all xattrs are in the output array */
		if (ptr >= names + namesize)
			*end_of_list = TRUE;
		else
			*end_of_list = FALSE;
	} else			/* no xattrs */
		*end_of_list = TRUE;

	*p_nb_returned = out_index;

	close(fd);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t vfs_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					const char *xattr_name,
					unsigned int *pxattr_id)
{
	unsigned int index;
	int rc;
	bool found = false;
	struct vfs_fsal_obj_handle *obj_handle = NULL;
	int fd = -1;

	obj_handle =
	    container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	for (index = 0; index < XATTR_COUNT; index++) {
		if (!strcmp(xattr_list[index].xattr_name, xattr_name)) {
			found = true;
			break;
		}
	}

	/* search in xattrs */
	if (!found) {
		fsal_errors_t fe;
		fd = (obj_hdl->type == DIRECTORY) ?
			vfs_fsal_open(obj_handle, O_DIRECTORY, &fe) :
			vfs_fsal_open(obj_handle, O_RDWR, &fe);
		if (fd < 0)
			return fsalstat(fe, -fd);

		errno = 0;
		rc = xattr_name_to_id(fd, xattr_name);
		if (rc < 0) {
			int minor = errno;
			close(fd);
			return fsalstat(-rc, minor);
		} else {
			index = rc;
			found = true;
		}
		close(fd);
	}

	*pxattr_id = index;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t vfs_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id,
					 caddr_t buffer_addr,
					 size_t buffer_size,
					 size_t *p_output_size)
{
	struct vfs_fsal_obj_handle *obj_handle = NULL;
	int fd = -1;
	int rc = 0;

	obj_handle =
	    container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	/* check that this index match the type of entry */
	if ((xattr_id < XATTR_COUNT)
	    && !do_match_type(xattr_list[xattr_id].flags,
			      obj_hdl->attributes.type)) {
		return fsalstat(ERR_FSAL_INVAL, 0);
	} else if (xattr_id >= XATTR_COUNT) {
		char attr_name[MAXPATHLEN];
		fsal_errors_t fe;

		fd = (obj_hdl->type == DIRECTORY) ?
			vfs_fsal_open(obj_handle, O_DIRECTORY, &fe) :
			vfs_fsal_open(obj_handle, O_RDWR, &fe);
		if (fd < 0)
			return fsalstat(fe, -fd);

		/* get the name for this attr */
		rc = xattr_id_to_name(fd, xattr_id, attr_name);
		if (rc) {
			int minor = errno;
			close(fd);
			return fsalstat(-rc, minor);
		}

		rc = fgetxattr(fd, attr_name, buffer_addr, buffer_size);
		if (rc < 0) {
			rc = errno;
			close(fd);
			return fsalstat(posix2fsal_error(rc), rc);
		}

		/* the xattr value can be a binary, or a string.
		 * trying to determine its type...
		 */
		*p_output_size = rc;

		close(fd);
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	} else {		/* built-in attr */

		/* get the value */
		rc = xattr_list[xattr_id].get_func(obj_hdl, buffer_addr,
						   buffer_size, p_output_size,
						   xattr_list[xattr_id].arg);
		return fsalstat(rc, 0);
	}
}

fsal_status_t vfs_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   caddr_t buffer_addr,
					   size_t buffer_size,
					   size_t *p_output_size)
{
	struct vfs_fsal_obj_handle *obj_handle = NULL;
	int fd = -1;
	int rc = 0;
	unsigned int index;
	fsal_errors_t fe;

	obj_handle =
	    container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);


	/* sanity checks */
	if (!obj_hdl || !p_output_size || !buffer_addr || !xattr_name)
		return fsalstat(ERR_FSAL_FAULT, 0);


	/* look for this name */
	for (index = 0; index < XATTR_COUNT; index++) {
		if (do_match_type(xattr_list[index].flags,
				  obj_hdl->attributes.type) &&
		    !strcmp(xattr_list[index].xattr_name, xattr_name)) {
			return vfs_getextattr_value_by_id(obj_hdl, index,
							  buffer_addr,
							  buffer_size,
							  p_output_size);
		}
	}

	fd = (obj_hdl->type == DIRECTORY) ?
	    vfs_fsal_open(obj_handle, O_DIRECTORY, &fe) :
	    vfs_fsal_open(obj_handle, O_RDWR, &fe);
	if (fd < 0)
		return fsalstat(fe, -fd);

	/* is it an xattr? */
	rc = fgetxattr(fd, xattr_name, buffer_addr, buffer_size);
	if (rc < 0) {
		rc = errno;
		close(fd);
		return fsalstat(posix2fsal_error(rc), rc);
	}
	/* the xattr value can be a binary, or a string.
	 * trying to determine its type...
	 */
	*p_output_size = rc;

	close(fd);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t vfs_setextattr_value(struct fsal_obj_handle *obj_hdl,
				   const char *xattr_name, caddr_t buffer_addr,
				   size_t buffer_size, int create)
{
	struct vfs_fsal_obj_handle *obj_handle = NULL;
	int fd = -1;
	fsal_errors_t fe;
	int rc = 0;

	obj_handle =
	    container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	fd = (obj_hdl->type == DIRECTORY) ?
	    vfs_fsal_open(obj_handle, O_DIRECTORY, &fe) :
	    vfs_fsal_open(obj_handle, O_RDWR, &fe);
	if (fd < 0)
		return fsalstat(fe, -fd);


	if (buffer_size == 0)
		rc = fsetxattr(fd, xattr_name, "", 1,
			       create ? XATTR_CREATE : XATTR_REPLACE);
	else
		rc = fsetxattr(fd, xattr_name, (char *)buffer_addr, buffer_size,
			       create ? XATTR_CREATE : XATTR_REPLACE);

	if (rc != 0) {
		rc = errno;
		close(fd);
		return fsalstat(posix2fsal_error(rc), rc);
	}

	close(fd);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t vfs_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 unsigned int xattr_id,
					 caddr_t buffer_addr,
					 size_t buffer_size)
{
	char name[MAXNAMLEN];
	struct vfs_fsal_obj_handle *obj_handle = NULL;
	int fd = -1;
	fsal_errors_t fe;
	int rc = 0;

	obj_handle =
	    container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	if (attr_is_read_only(xattr_id))
		return fsalstat(ERR_FSAL_PERM, 0);
	else if (xattr_id < XATTR_COUNT)
		return fsalstat(ERR_FSAL_PERM, 0);

	fd = (obj_hdl->type == DIRECTORY) ?
	    vfs_fsal_open(obj_handle, O_DIRECTORY, &fe) :
	    vfs_fsal_open(obj_handle, O_RDWR, &fe);
	if (fd < 0)
		return fsalstat(fe, -fd);

	rc = xattr_id_to_name(fd, xattr_id, name);
	if (rc) {
		int minor = errno;
		close(fd);
		return fsalstat(-rc, minor);
	}

	close(fd);

	return vfs_setextattr_value(obj_hdl, name, buffer_addr,
				    buffer_size, FALSE);
}

fsal_status_t vfs_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				   unsigned int xattr_id,
				   struct attrlist *p_attrs)
{
	int rc;

	/* check that this index match the type of entry */
	if (xattr_id < XATTR_COUNT &&
	    !do_match_type(xattr_list[xattr_id].flags,
			   obj_hdl->attributes.type)) {
		return fsalstat(ERR_FSAL_INVAL, 0);
	} else if (xattr_id >= XATTR_COUNT) {
		/* This is user defined xattr */
		LogFullDebug(COMPONENT_FSAL, "Getting attributes for xattr #%u",
			     xattr_id - XATTR_COUNT);
	}

	rc = file_attributes_to_xattr_attrs(&obj_hdl->attributes, p_attrs,
					    xattr_id);
	if (rc != 0)
		return fsalstat(ERR_FSAL_INVAL, rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t vfs_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
				       unsigned int xattr_id)
{
	int rc;
	char name[MAXNAMLEN];
	struct vfs_fsal_obj_handle *obj_handle = NULL;
	int fd = -1;
	fsal_errors_t fe;

	obj_handle =
	    container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);
	fd = (obj_hdl->type == DIRECTORY) ?
	    vfs_fsal_open(obj_handle, O_DIRECTORY, &fe) :
	    vfs_fsal_open(obj_handle, O_RDWR, &fe);
	if (fd < 0)
		return fsalstat(fe, -fd);

	rc = xattr_id_to_name(fd, xattr_id, name);
	if (rc) {
		int minor = errno;
		close(fd);
		return fsalstat(-rc, minor);
	}

	rc = fremovexattr(fd, name);
	if (rc) {
		rc = errno;
		close(fd);
		return fsalstat(posix2fsal_error(rc), rc);
	}


	close(fd);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t vfs_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					 const char *xattr_name)
{
	struct vfs_fsal_obj_handle *obj_handle = NULL;
	int fd = -1;
	int rc = 0;
	fsal_errors_t fe;

	obj_handle =
	    container_of(obj_hdl, struct vfs_fsal_obj_handle, obj_handle);

	fd = (obj_hdl->type == DIRECTORY) ?
	    vfs_fsal_open(obj_handle, O_DIRECTORY, &fe) :
	    vfs_fsal_open(obj_handle, O_RDWR, &fe);
	if (fd < 0)
		return fsalstat(fe, -fd);

	rc = fremovexattr(fd, xattr_name);
	if (rc) {
		rc = errno;
		close(fd);
		return fsalstat(posix2fsal_error(rc), rc);
	}

	close(fd);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
