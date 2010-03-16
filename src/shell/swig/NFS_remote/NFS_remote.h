#ifndef _COMMANDS_NFS_REMOTE_H
#define _COMMANDS_NFS_REMOTE_H

#include <string.h>

#include "mount.h"
#include "nfs23.h"
#include "nfs_proto_functions.h"

typedef struct shell_fh3__ {
  u_int data_len;
  char data_val[NFS3_FHSIZE];
} shell_fh3_t;

/** rpc_init */
int rpc_init(char *hostname,	/* IN */
	     char *name,	/* IN */
	     char *proto,	/* IN */
	     FILE * output	/* IN */
    );

/* solves a relative or aboslute path */
int nfs_remote_solvepath(shell_fh3_t * p_mounted_path_hdl,	/* IN - handle of mounted path */
			 char *io_global_path,	/* OUT - global path */
			 int size_global_path,	/* IN - max size for global path */
			 char *i_spec_path,	/* IN - specified path */
			 shell_fh3_t * p_current_hdl,	/* IN - current directory handle */
			 shell_fh3_t * pnew_hdl,	/* OUT - pointer to solved handle */
			 FILE * output	/* IN */
    );

/** nfs_remote_getattr */
int nfs_remote_getattr(shell_fh3_t * p_hdl,	/* IN */
		       fattr3 * attrs,	/* OUT */
		       FILE * output	/* IN */
    );

/** nfs_remote_access */
int nfs_remote_access(shell_fh3_t * p_hdl,	/* IN */
		      nfs3_uint32 * access_mask,	/* IN/OUT */
		      FILE * output	/* IN */
    );

/** nfs_remote_readlink */
int nfs_remote_readlink(shell_fh3_t * p_hdl,	/* IN */
			char *linkcontent,	/* OUT */
			FILE * output	/* IN */
    );

/** nfs_remote_readdirplus */
int nfs_remote_readdirplus(shell_fh3_t * p_dir_hdl,	/* IN */
			   cookie3 cookie,	/* IN */
			   cookieverf3 * p_cookieverf,	/* IN/OUT */
			   dirlistplus3 * dirlist,	/* OUT */
			   nfs_res_t ** to_be_freed,	/* OUT */
			   FILE * output	/* IN */
    );

/** nfs_remote_readdirplus_free */
void nfs_remote_readdirplus_free(nfs_res_t * to_free);

/** nfs_remote_readdir */
int nfs_remote_readdir(shell_fh3_t * p_dir_hdl,	/* IN */
		       cookie3 cookie,	/* IN */
		       cookieverf3 * p_cookieverf,	/* IN/OUT */
		       dirlist3 * dirlist,	/* OUT */
		       nfs_res_t ** to_be_freed,	/* OUT */
		       FILE * output	/* IN */
    );

/** nfs_remote_readdir_free */
void nfs_remote_readdir_free(nfs_res_t * to_free);

/** nfs_remote_create */
int nfs_remote_create(shell_fh3_t * p_dir_hdl,	/* IN */
		      char *obj_name,	/* IN */
		      mode_t posix_mode,	/* IN */
		      shell_fh3_t * p_obj_hdl,	/* OUT */
		      FILE * output	/* IN */
    );

/** nfs_remote_mkdir */
int nfs_remote_mkdir(shell_fh3_t * p_dir_hdl,	/* IN */
		     char *obj_name,	/* IN */
		     mode_t posix_mode,	/* IN */
		     shell_fh3_t * p_obj_hdl,	/* OUT */
		     FILE * output	/* IN */
    );

/** nfs_remote_rmdir */
int nfs_remote_rmdir(shell_fh3_t * p_dir_hdl,	/* IN */
		     char *obj_name,	/* IN */
		     FILE * output	/* IN */
    );

/** nfs_remote_remove */
int nfs_remote_remove(shell_fh3_t * p_dir_hdl,	/* IN */
		      char *obj_name,	/* IN */
		      FILE * output	/* IN */
    );

/** nfs_remote_setattr */
int nfs_remote_setattr(shell_fh3_t * p_obj_hdl,	/* IN */
		       sattr3 * p_attributes,	/* IN */
		       FILE * output	/* IN */
    );

/** nfs_remote_rename */
int nfs_remote_rename(shell_fh3_t * p_src_dir_hdl,	/* IN */
		      char *src_name,	/* IN */
		      shell_fh3_t * p_tgt_dir_hdl,	/* IN */
		      char *tgt_name,	/* IN */
		      FILE * output	/* IN */
    );

/** nfs_remote_link */
int nfs_remote_link(shell_fh3_t * p_file_hdl,	/* IN */
		    shell_fh3_t * p_tgt_dir_hdl,	/* IN */
		    char *tgt_name,	/* IN */
		    FILE * output	/* IN */
    );

/** nfs_remote_symlink */
int nfs_remote_symlink(shell_fh3_t path_hdl,	/* IN */
		       char *link_name,	/* IN */
		       char *link_content,	/* IN */
		       sattr3 * p_setattr,	/* IN */
		       shell_fh3_t * p_link_hdl,	/* OUT */
		       FILE * output	/* IN */
    );

/** nfs_remote_mount */
int nfs_remote_mount(char *str_path,	/* IN */
		     shell_fh3_t * p_mnt_hdl,	/* OUT */
		     FILE * output	/* IN */
    );

#endif
