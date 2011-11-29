/*
 * configuration structure management functions
 */

fsal_boolean_t fsal_supports(struct fsal_staticfsinfo_t *info,
			     fsal_fsinfo_options_t option);

fsal_size_t fsal_maxfilesize(struct fsal_staticfsinfo_t *info);

fsal_count_t fsal_maxlink(struct fsal_staticfsinfo_t *info);

fsal_mdsize_t fsal_maxnamelen(struct fsal_staticfsinfo_t *info);

fsal_mdsize_t fsal_maxpathlen(struct fsal_staticfsinfo_t *info);

fsal_fhexptype_t fsal_fh_expire_type(struct fsal_staticfsinfo_t *info);

fsal_time_t fsal_lease_time(struct fsal_staticfsinfo_t *info);

fsal_aclsupp_t fsal_acl_support(struct fsal_staticfsinfo_t *info);

fsal_attrib_mask_t fsal_supported_attrs(struct fsal_staticfsinfo_t *info);

fsal_size_t fsal_maxread(struct fsal_staticfsinfo_t *info);

fsal_size_t fsal_maxwrite(struct fsal_staticfsinfo_t *info);

fsal_accessmode_t fsal_umask(struct fsal_staticfsinfo_t *info);

fsal_accessmode_t fsal_xattr_access_rights(struct fsal_staticfsinfo_t *info);
