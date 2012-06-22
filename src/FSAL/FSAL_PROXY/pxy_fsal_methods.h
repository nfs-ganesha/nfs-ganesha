#ifndef _PXY_FSAL_METHODS_H
#define _PXY_FSAL_METHODS_H

#ifdef _HANDLE_MAPPING
#include "handle_mapping/handle_mapping.h"
#endif

typedef struct
{
  unsigned int retry_sleeptime;
  unsigned int srv_addr;
  unsigned int srv_prognum;
  unsigned int srv_sendsize;
  unsigned int srv_recvsize;
  unsigned int srv_timeout;
  unsigned short srv_port;
  unsigned int use_privileged_client_port ;
  char srv_proto[MAXNAMLEN];
  char remote_principal[MAXNAMLEN];
  char keytab[MAXPATHLEN];
  unsigned int cred_lifetime;
  unsigned int sec_type;
  bool_t active_krb5;

  /* initialization info for handle mapping */
  int enable_handle_mapping;

#ifdef _HANDLE_MAPPING
  handle_map_param_t hdlmap;
#endif
} proxyfs_specific_initinfo_t;

struct pxy_fsal_module {
      struct fsal_module module;
      fsal_staticfsinfo_t fsinfo;
      fsal_init_info_t init;
      proxyfs_specific_initinfo_t special;
      struct fsal_ops pxy_ops;
};

struct pxy_export {
        struct fsal_export exp;
        const proxyfs_specific_initinfo_t *info;
};

int pxy_init_rpc(proxyfs_specific_initinfo_t *);

fsal_status_t
pxy_getextattrs(struct fsal_obj_handle *obj_hdl,
		fsal_extattrib_list_t * object_attributes);

fsal_status_t
pxy_list_ext_attrs(struct fsal_obj_handle *obj_hdl, unsigned int cookie,
		   fsal_xattrent_t * xattrs_tab, unsigned int xattrs_tabsize,
		   unsigned int *p_nb_returned, int *end_of_list);

fsal_status_t
pxy_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
			  const char *xattr_name, unsigned int *pxattr_id);

fsal_status_t
pxy_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
			     const char *xattr_name, caddr_t buffer_addr,
			     size_t buffer_size, size_t * len);

fsal_status_t
pxy_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
			   unsigned int xattr_id, caddr_t buf, size_t sz,
			   size_t *len);

fsal_status_t
pxy_setextattr_value(struct fsal_obj_handle *obj_hdl,
		     const char *xattr_name, caddr_t buf, size_t sz,
		     int create);

fsal_status_t
pxy_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
			   unsigned int xattr_id, caddr_t buf, size_t sz);

fsal_status_t
pxy_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
		     unsigned int xattr_id, fsal_attrib_list_t * attrs);

fsal_status_t
pxy_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
			 unsigned int xattr_id);

fsal_status_t
pxy_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
			   const char *xattr_name);

fsal_status_t
pxy_lookup_path(struct fsal_export *exp_hdl, const char *path,
                struct fsal_obj_handle **handle);

fsal_status_t
pxy_create_handle(struct fsal_export *exp_hdl,
                  struct fsal_handle_desc *hdl_desc,
                  struct fsal_obj_handle **handle);

fsal_status_t
pxy_create_export(struct fsal_module *fsal_hdl,
                  const char *export_path,
                  const char *fs_options,
                  struct exportlist__ *exp_entry,
                  struct fsal_module *next_fsal,
                  struct fsal_export **export);

fsal_status_t
pxy_get_dynamic_info(struct fsal_export *, fsal_dynamicfsinfo_t *);


fsal_status_t
pxy_extract_handle(struct fsal_export *, fsal_digesttype_t,
                   struct fsal_handle_desc *);

#endif
