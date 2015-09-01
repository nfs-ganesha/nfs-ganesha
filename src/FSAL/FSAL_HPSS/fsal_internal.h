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

/**
 *
 * \file    fsal_internal.h
 * \brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 *
 */


#include  "fsal.h"

/*
 * FS relative includes
 */
#include <hpss_version.h>

#include <u_signed64.h>         /* for cast64 function */
#include <hpss_api.h>
#include <acct_hpss.h>
#include <hpss_mech.h>
#include <hpss_String.h>

/* -------------------------------------------
 *      HPSS dependent definitions
 * ------------------------------------------- */

/* Filesystem and fsal handle
 * handle doesn't need to be a pointer but this seems idiomatic...
 */

struct hpss_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct attrlist attributes;

	struct hpss_file_handle *handle;
	union {
		struct {
			fsal_openflags_t openflags;
			int fd;
		} file;
	struct {
		unsigned char *link_content;
		int link_size;
	} symlink;
	} u;
};

struct hpss_file_handle {
	/* The object type */
	object_file_type_t obj_type;

	/* The hpss handle */
	ns_ObjHandle_t ns_handle;
};

/** FSAL security context */
struct hpssfsal_export_context {
	ns_ObjHandle_t fileset_root_handle;
	unsigned int default_cos;
};


/** HPSS specific init info */
struct hpss_specific_initinfo {
	/* Settings actually set in the config file */
	struct behaviors {
		char AuthnMech;
		char NumRetries;
		char BusyDelay;
		char BusyRetries;
		char MaxConnections;
		char DebugPath;
	} behaviors;

	/* client API configuration */
	api_config_t hpss_config;

	/* other configuration info */
	char Principal[MAXNAMLEN];
	char KeytabPath[MAXPATHLEN];

	uint32_t CredentialLifetime;
	uint32_t ReturnInconsistentDirent;

	int default_cos;
	char filesetname[MAXPATHLEN];
};

#define HPSS_DEFAULT_CREDENTIAL_LIFETIME 3600

#if HPSS_LEVEL >= 730
#define HAVE_XATTR_CREATE 1
#endif

/* defined the set of attributes supported with HPSS */
#define HPSS_SUPPORTED_ATTRIBUTES (                        \
	ATTR_TYPE     | ATTR_SIZE      |                 \
	ATTR_FSID     | ATTR_FILEID    |                 \
	ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     | \
	ATTR_GROUP    | ATTR_ATIME    | ATTR_CREATION  | \
	ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED | \
	ATTR_CHGTIME | ATTR_ATIME_SERVER | \
	ATTR_MTIME_SERVER | ATTR_RAWDEV)

/* fsal_convert stuff */
int hpss2fsal_error(int hpss_errorcode);
int fsal2hpss_testperm(fsal_accessflags_t testperm);
object_file_type_t  hpss2fsal_type(unsigned32 hpss_type_in);
struct timespec hpss2fsal_time(timestamp_sec_t tsec);

#define fsal2hpss_time(_time_) ((timestamp_sec_t)(_time_).tv_sec)

u_signed64 fsal2hpss_64(uint64_t fsal_size_in);
uint64_t hpss2fsal_64(u_signed64 fsal_size_in);
fsal_fsid_t hpss2fsal_fsid(u_signed64 hpss_fsid_in);

uint32_t hpss2fsal_mode(unsigned32 uid_bit,
			unsigned32 gid_bit,
			unsigned32 sticky_bit,
			unsigned32 user_perms,
			unsigned32 group_perms,
			unsigned32 other_perms);

void fsal2hpss_mode(uint32_t fsal_mode,
		    unsigned32 *mode_perms,
		    unsigned32 *user_perms,
		    unsigned32 *group_perms, unsigned32 *other_perms);

fsal_status_t hpss2fsal_attributes(ns_ObjHandle_t *p_hpss_handle_in,
				   hpss_Attrs_t *p_hpss_attr_in,
				   struct attrlist *p_fsalattr_out);

fsal_status_t hpss2fsal_vattributes(hpss_vattr_t *p_hpss_vattr_in,
				    struct attrlist *p_fsalattr_out);

fsal_status_t hpssHandle2fsalAttributes(ns_ObjHandle_t *p_hpsshandle_in,
					struct attrlist *p_fsalattr_out);

fsal_status_t fsal2hpss_attribset(struct fsal_obj_handle *p_fsal_handle,
				  struct attrlist *p_attrib_set,
				  hpss_fileattrbits_t *p_hpss_attrmask,
				  hpss_Attrs_t *p_hpss_attrs);

/* fsal_internal.c */

int HPSSFSAL_IsStaleHandle(ns_ObjHandle_t *p_hdl, sec_cred_t *p_cred);

void HPSSFSAL_BuildCos(uint32_t CosId,
		       hpss_cos_hints_t *hints,
		       hpss_cos_priorities_t *hintpri);

int HPSSFSAL_ucreds_from_opctx(const struct req_op_context *opctx,
			       sec_cred_t *ucreds);
