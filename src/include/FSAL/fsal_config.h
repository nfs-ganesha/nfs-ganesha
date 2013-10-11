/*
 * configuration structure management functions
 */

bool fsal_supports(struct fsal_staticfsinfo_t *info,
		   fsal_fsinfo_options_t option);

uint64_t fsal_maxfilesize(struct fsal_staticfsinfo_t *info);

uint32_t fsal_maxlink(struct fsal_staticfsinfo_t *info);

uint32_t fsal_maxnamelen(struct fsal_staticfsinfo_t *info);

uint32_t fsal_maxpathlen(struct fsal_staticfsinfo_t *info);

struct timespec fsal_lease_time(struct fsal_staticfsinfo_t *info);

fsal_aclsupp_t fsal_acl_support(struct fsal_staticfsinfo_t *info);

attrmask_t fsal_supported_attrs(struct fsal_staticfsinfo_t *info);

uint32_t fsal_maxread(struct fsal_staticfsinfo_t *info);

uint32_t fsal_maxwrite(struct fsal_staticfsinfo_t *info);

uint32_t fsal_umask(struct fsal_staticfsinfo_t *info);

uint32_t fsal_xattr_access_rights(struct fsal_staticfsinfo_t *info);
