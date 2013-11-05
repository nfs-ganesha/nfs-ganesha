/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file    nfs4_pseudo.c
 * @brief   Routines used for managing the NFS4 pseudo file system.
 *
 * Routines used for managing the NFS4 pseudo file system.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/param.h>
#include "hashtable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "export_mgr.h"
#include "city.h"

static pseudofs_t gPseudoFs;
#define V4_FH_OPAQUE_SIZE (sizeof(struct alloc_file_handle_v4) - \
			   sizeof(struct file_handle_v4))

/**
 * @brief Construct the fs opaque part of a pseudofs nfsv4 handle
 *
 * Given the components of a pseudofs nfsv4 handle, the nfsv4 handle is
 * created by concatenating the components. This is the fs opaque piece
 * of struct file_handle_v4 and what is sent over the wire.
 *
 * @param[in] pseudopath Full patch of the pseudofs node
 * @param[in] len length of the pseudopath parameter
 * @param[in] hashkey a 64 bit hash of the pseudopath parameter
 *
 * @return The nfsv4 pseudofs file handle as a char *
 */
char *package_pseudo_handle(char *pseudopath, ushort len, uint64 hashkey)
{
	char *buff = NULL;
	int opaque_bytes_used = 0, pathlen = 0;

	/* This is the size of the v4 file handle opaque area used for pseudofs
	 * or FSAL file handles.
	 */
	buff = gsh_malloc(V4_FH_OPAQUE_SIZE);
	if (buff == NULL) {
		LogCrit(COMPONENT_NFS_V4_PSEUDO,
			"Failed to malloc space for pseudofs handle.");
		return NULL;
	}

	memcpy(buff, &hashkey, sizeof(hashkey));
	opaque_bytes_used += sizeof(hashkey);

	/* include length of the path in the handle.
	 * MAXPATHLEN=4096 ... max path length can be contained in a short int.
	 */
	memcpy(buff + opaque_bytes_used, &len, sizeof(ushort));
	opaque_bytes_used += sizeof(ushort);

	/* Either the nfsv4 fh opaque size or the length of the pseudopath.
	 * Ideally we can include entire pseudofs pathname for guaranteed
	 * uniqueness of pseudofs handles.
	 */
	pathlen = MIN(V4_FH_OPAQUE_SIZE - opaque_bytes_used, len);
	memcpy(buff + opaque_bytes_used, pseudopath, pathlen);
	opaque_bytes_used += pathlen;

	/* If there is more space in the opaque handle due to a short pseudofs
	 * path ... zero it.
	 */
	if (opaque_bytes_used < V4_FH_OPAQUE_SIZE) {
		memset(buff + opaque_bytes_used, 0,
		       V4_FH_OPAQUE_SIZE - opaque_bytes_used);
	}

	return buff;
}

/**
 * @brief Creates a hashtable key for a pseudofs node given the fullpath.
 *
 * Creates a hashtable key for a pseudofs node given the fullpath.
 *
 * @param[in] pseudopath Full path of the pseudofs node
 * @param[in] len Length of the full path
 *
 * @return the key
 */
struct gsh_buffdesc create_pseudo_handle_key(char *pseudopath, ushort len)
{
	struct gsh_buffdesc key;
	uint64 hashkey;

	hashkey = CityHash64(pseudopath, len);
	key.addr = package_pseudo_handle(pseudopath, len, hashkey);
	key.len = V4_FH_OPAQUE_SIZE;

	return key;
}

/**
 * @brief Get the root of the pseudo file system
 *
 * Gets the root of the pseudo file system. This is only a wrapper to
 * static variable gPseudoFs.
 *
 * @return The pseudo fs root
 */

pseudofs_t *nfs4_GetPseudoFs(void)
{
	return &gPseudoFs;
}

/**
 * @brief Concatenate a number of pseudofs tokens into a string
 *
 * When reading pseudofs paths from export entries, we divide the
 * path into tokens. This function will recombine a specific number
 * of those tokens into a string.
 *
 * @param[in/out] fullpseudopath Must be not NULL. Tokens are copied to here.
 * @param[in] node for which a full pseudopath needs to be formed.
 * @param[in] maxlen maximum number of chars to copy to fullpseudopath
 *
 * @return void
 */
void fullpath_h(char *fullpseudopath, pseudofs_entry_t *this_node,
		int *currlen, int maxlen)
{
	if (this_node != &(gPseudoFs.root))
		fullpath_h(fullpseudopath, this_node->parent, currlen, maxlen);

	if (*currlen >= maxlen)
		return;

	if (*currlen + strlen(this_node->name) > maxlen) {
		LogWarn(COMPONENT_NFS_V4_PSEUDO,
			"Pseudopath length is very long, can't"
			" create guaranteed unique handle with this.");
		strncpy(fullpseudopath + (*currlen),
			this_node->name,
			maxlen - *currlen - 1);	/* leave room for \0 */
		*currlen += (maxlen - *currlen - 1);
		fullpseudopath[*currlen] = '\0';
		*currlen = maxlen;
		return;
	}
	strncpy(fullpseudopath + (*currlen),
		this_node->name,
		strlen(this_node->name));
	*currlen += strlen(this_node->name);

	if (*currlen != maxlen && this_node != &(gPseudoFs.root)) {
		fullpseudopath[(*currlen)++] = '/';
		fullpseudopath[*currlen] = '\0';
	} else
		fullpseudopath[*currlen] = '\0';
}

void fullpath(char *fullpseudopath, pseudofs_entry_t *new_node,
	      pseudofs_entry_t *this_node, int maxlen)
{
	int currlen = 0;
	fullpath_h(fullpseudopath, this_node, &currlen, maxlen);
	if (currlen + strlen(new_node->name) <= maxlen) {
		strncpy(fullpseudopath + currlen,
			new_node->name,
			strlen(new_node->name));
		currlen += strlen(new_node->name);
		fullpseudopath[currlen] = '\0';
	} else if (currlen != maxlen) {
		strncpy(fullpseudopath + currlen,
			new_node->name,
			maxlen - currlen - 1);	/* leave room for \0 */
		fullpseudopath[maxlen - 1] = '\0';
	}
}

/**
 * @brief Find the node for this path component
 *
 * If not found, create it.  Called from token_to_proc() interator
 *
 * @param token [IN] path name component
 * @param arg   [IN] callback state
 *
 * @return status as bool. false terminates foreach
 */

struct node_state {
	pseudofs_entry_t *this_node;
	exportlist_t *entry;
	int retval;
};

static bool pseudo_node(char *token, void *arg)
{
	struct node_state *state = (struct node_state *)arg;
	pseudofs_entry_t *node = NULL;
	pseudofs_entry_t *new_node = NULL;
	struct gsh_buffdesc key;
	char fullpseudopath[MAXPATHLEN + 2];
	int j = 0;

	state->retval = 0;	/* start off with no errors */

	LogFullDebug(COMPONENT_NFS_V4_PSEUDO, "token %s", token);
	for (node = state->this_node->sons; node != NULL; node = node->next) {
		/* Looking for a matching entry */
		if (!strcmp(node->name, token)) {
			/* matched entry is new parent node */
			state->this_node = node;
			return true;
		}
		j++;
	}

	/* not found so create a new entry */
	if (gPseudoFs.last_pseudo_id == (MAX_PSEUDO_ENTRY - 1)) {
		LogMajor(COMPONENT_NFS_V4_PSEUDO,
			 "Too many nodes in Export_Id %d Path=\"%s\" Pseudo=\"%s\"",
			 state->entry->id,
			 state->entry->fullpath,
			 state->entry->pseudopath);
		state->retval = ENOMEM;
		return false;
	}
	new_node = gsh_calloc(1, sizeof(pseudofs_entry_t));
	if (new_node == NULL) {
		LogMajor(COMPONENT_NFS_V4_PSEUDO,
			 "Insufficient memory to create pseudo fs node");
		state->retval = ENOMEM;
		return false;
	}

	strcpy(new_node->name, token);
	gPseudoFs.last_pseudo_id++;

	/** @todo: need to fix this ... */
	fullpath(fullpseudopath, new_node, state->this_node, MAXPATHLEN);
	key = create_pseudo_handle_key(fullpseudopath,
				       (ushort) strlen(fullpseudopath));
	new_node->fsopaque = (uint8_t *) key.addr;
	new_node->pseudo_id = *(uint64_t *) new_node->fsopaque;
	gPseudoFs.reverse_tab[gPseudoFs.last_pseudo_id] = new_node;
	new_node->last = new_node;

	if (isMidDebug(COMPONENT_NFS_V4_PSEUDO)) {
		char str[256];

		sprint_mem(str, new_node->fsopaque, V4_FH_OPAQUE_SIZE);

		LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
			    "Built pseudofs entry "
			    "index:%u name:%s path:%s handle:%s",
			    gPseudoFs.last_pseudo_id,
			    new_node->name,
			    fullpseudopath,
			    str);
	}

	/* Step into the new entry and attach it to the tree */
	if (state->this_node->sons == NULL) {
		state->this_node->sons = new_node;
	} else {
		state->this_node->sons->last->next = new_node;
		state->this_node->sons->last = new_node;
	}

	new_node->parent = state->this_node;
	state->this_node = new_node;
	return true;
}

/**
 * @brief  Create a pseudofs entry for this export
 *
 * Iterate through the path finding and/or creating path nodes
 * in the pseudofs directory structure for this export pseudo.
 * Called from foreach_gsh_export() iterator
 *
 * @param exp   [IN] export in question
 * @param arg   [IN] callback state
 *
 * @return status as bool.  false terminates the foreach
 */

struct pseudo_state {
	int retval;
};

static bool export_to_pseudo(struct gsh_export *exp, void *arg)
{
	struct pseudo_state *state = (struct pseudo_state *)arg;
	exportlist_t *entry = &exp->export;
	struct node_state node_state;
	char *tmp_pseudopath;

	int rc;

	state->retval = 0;	/* start off with no errors */

	/* skip exports that aren't for NFS v4 */
	if ((entry->export_perms.options & EXPORT_OPTION_NFSV4) == 0
	    || entry->pseudopath == NULL
	    || (entry->export_perms.options & EXPORT_OPTION_PSEUDO) == 0
	    || entry->id == 0)
		return true;

	LogDebug(COMPONENT_NFS_V4_PSEUDO,
		 "BUILDING PSEUDOFS: Export_Id %d Path %s Pseudo Path %s",
		 entry->id, entry->fullpath, entry->pseudopath);

	/* there must be a leading '/' in the pseudo path */
	if (entry->pseudopath[0] != '/') {
		LogCrit(COMPONENT_NFS_V4_PSEUDO,
			"Pseudo Path '%s' is badly formed",
			entry->pseudopath);
		state->retval = EINVAL;
		return false;
	}

	if (strlen(entry->pseudopath) > MAXPATHLEN) {
		LogCrit(COMPONENT_NFS_V4_PSEUDO,
			"Bad Pseudo=\"%s\", path too long",
			entry->pseudopath);
		state->retval = EINVAL;
		return false;
	}

	/* Parsing the path
	 * Make a copy of the pseudopath since it will be modified,
	 * also, skip the leading '/'.
	 */
	tmp_pseudopath = alloca(strlen(entry->pseudopath) + 2);
	strcpy(tmp_pseudopath, entry->pseudopath + 1);
	node_state.this_node = &(gPseudoFs.root);
	node_state.entry = entry;
	rc = token_to_proc(tmp_pseudopath, '/', pseudo_node, &node_state);
	if (rc == -1) {
		state->retval = node_state.retval;
		return false;
	}

	/* Now that all entries are added to pseudofs tree,
	 * add the junction to the pseudofs */
	node_state.this_node->junction_export = entry;
	entry->exp_mounted_on_file_id = node_state.this_node->pseudo_id;

	return true;
}

/**
 * @brief Build a pseudo fs from an exportlist
 *
 * foreach through the exports to create pseudofs entries.
 *
 * @return status as errno (0 == SUCCESS).
 */

int nfs4_ExportToPseudoFS(void)
{
	struct pseudo_state build_state;
	struct gsh_buffdesc key;

	/* Init Root of the Pseudo FS tree */
	strcpy(gPseudoFs.root.name, "/");
	key = create_pseudo_handle_key(gPseudoFs.root.name,
				       (ushort) strlen(gPseudoFs.root.name));
	gPseudoFs.root.fsopaque = (uint8_t *) key.addr;
	gPseudoFs.root.pseudo_id = *(uint64_t *) gPseudoFs.root.fsopaque;
	gPseudoFs.root.junction_export = NULL;
	gPseudoFs.root.next = NULL;
	gPseudoFs.root.last = NULL;
	gPseudoFs.root.sons = NULL;
	/* root is its own parent */
	gPseudoFs.root.parent = &(gPseudoFs.root);
	gPseudoFs.reverse_tab[0] = &(gPseudoFs.root);

	(void)foreach_gsh_export(export_to_pseudo, &build_state);

	if (isMidDebug(COMPONENT_NFS_V4_PSEUDO)) {
		int i;

		for (i = 0; i <= gPseudoFs.last_pseudo_id; i++) {
			if (gPseudoFs.reverse_tab[i]->junction_export != NULL) {
				LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
					    "pseudo_id %d is %s junction_export %p"
					    "Export_id %d Path %s mounted_on_fileid %"
					    PRIu64, i,
					    gPseudoFs.reverse_tab[i]->name,
					    gPseudoFs.reverse_tab[i]->
					    junction_export,
					    gPseudoFs.reverse_tab[i]->
					    junction_export->id,
					    gPseudoFs.reverse_tab[i]->
					    junction_export->fullpath,
					    (uint64_t) gPseudoFs.
					    reverse_tab[i]->junction_export->
					    exp_mounted_on_file_id);
			} else {
				LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
					    "pseudo_id %d is %s (not a junction)",
					    i,
					    gPseudoFs.reverse_tab[i]->name);
			}
		}
	}

	return build_state.retval;
}

/**
 * @brief Get the attributes for an entry in the pseudofs
 *
 * This function gets the attributes for an entry in the
 * pseudofs. Because pseudo fs structure is very simple (it is
 * read-only and contains only directory that belongs to root), a set
 * of standardized values is returned
 *
 * @param[out] psfsp  Pseudo fs entry on which attributes are queried
 * @param[in]  Fattr  Buffer that will contain the queried attributes
 * @param[in]  data   Compound request data
 * @param[in]  objFH  NFSv4 filehandle, in case they request it
 * @param[in]  Bitmap Bitmap describing the attributes to be returned
 *
 * @retval 0 if successfull.
 * @retval -1 on error, in this case, too many attributes were requested.
 */

int nfs4_PseudoToFattr(pseudofs_entry_t *psfsp, fattr4 *Fattr,
		       compound_data_t *data, nfs_fh4 *objFH,
		       struct bitmap4 *Bitmap)
{
	struct attrlist attrs;
	struct xdr_attrs_args args;

	/* cobble up an inode (attributes) for this pseudo */
	memset(&attrs, 0, sizeof(attrs));
	attrs.type = DIRECTORY;
	attrs.filesize = DEV_BSIZE;
	if (psfsp->junction_export == NULL) {
		attrs.fsid.major = 152LL;
		attrs.fsid.minor = 152LL;
	} else {
		/* @todo BUGAZOMEU : tres cradem mais utile */
		attrs.fsid.major = 153LL;
		attrs.fsid.minor = nfs_htonl64(153LL);
	}
	attrs.fileid = psfsp->pseudo_id;
	attrs.mode = unix2fsal_mode(0555);
	attrs.numlinks = 2;	/* not 1.  for '.' and '../me' */
	attrs.owner = 0;	/* root */
	attrs.group = 2;	/* daemon? */
	attrs.atime = ServerBootTime;
	attrs.ctime = ServerBootTime;
	attrs.chgtime = ServerBootTime;
	attrs.change = ServerBootTime.tv_sec;
	attrs.spaceused = DEV_BSIZE;

	memset(&args, 0, sizeof(args));
	args.attrs = &attrs;
	args.data = data;
	args.hdl4 = objFH;
	args.mounted_on_fileid = psfsp->pseudo_id;

	return nfs4_FSALattr_To_Fattr(&args, Bitmap, Fattr);
}

/**
 * @brief Convert CurrentFH to an id in the pseudo
 *
 * This function converts an NFSv4 file handle to a pseudofs id and
 * checks if the fh is related to a pseudofs entry.
 *
 * @param[in]  data      pointer to compound data
 * @param[out] psfsentry Pseudofs entry
 *
 * @retval NFS4_OK if successfull
 * @retval Appropriate NFS4ERR on failure
 */

static bool nfs4_CurrentFHToPseudo(compound_data_t *data,
				   pseudofs_entry_t **psfsentry)
{
	file_handle_v4_t *pfhandle4;
	unsigned long i;

	/* Map the filehandle to the correct structure */
	pfhandle4 = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

	/* The function must be called with a fh pointed to a pseudofs entry */
	if (pfhandle4 == NULL || pfhandle4->exportid != 0
	    || pfhandle4->fhversion != GANESHA_FH_VERSION) {
		LogDebug(COMPONENT_NFS_V4_PSEUDO,
			 "Pseudo fs handle=%p, is_pseudofs=%d, fhversion=%d",
			 pfhandle4,
			 pfhandle4 != NULL ? pfhandle4->exportid == 0 : 0,
			 pfhandle4 != NULL ? pfhandle4->fhversion : 0);
		return NFS4ERR_BADHANDLE;
	}

	/* Get the object pointer by using the reverse tab in
	 * the pseudofs structure
	 */
	if (isMidDebug(COMPONENT_NFS_V4_PSEUDO)) {
		char str[256];
		sprint_mem(str, pfhandle4->fsopaque, V4_FH_OPAQUE_SIZE);
		LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
			    "Pseudo fs handle opaque %s", str);
	}

	/* Get the object pointer by using the reverse tab in the
	 * pseudofs structure
	 */
	*psfsentry = NULL;
	for (i = 0; i < gPseudoFs.last_pseudo_id; i++) {
		if (isFullDebug(COMPONENT_NFS_V4_PSEUDO)) {
			char str[256];

			sprint_mem(str, gPseudoFs.reverse_tab[i]->fsopaque,
				   V4_FH_OPAQUE_SIZE);

			LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
				     "Compared index:%lu / %u handle:%s", i,
				     gPseudoFs.last_pseudo_id,
				     str);
		}
		if (memcmp(gPseudoFs.reverse_tab[i]->fsopaque,
			   pfhandle4->fsopaque,
			   V4_FH_OPAQUE_SIZE) == 0) {
			*psfsentry = gPseudoFs.reverse_tab[i];
			LogFullDebug(COMPONENT_NFS_V4_PSEUDO, "Matched!");
			break;
		}
	}

	/* If an export was removed and we restarted or reloaded exports then
	 * the PseudoFS entry corresponding to a handle might not exist now.
	 */
	if (*psfsentry == NULL)
		return NFS4ERR_STALE;

	return NFS4_OK;
}				/* nfs4_CurrentFHToPseudo */

/**
 * nfs4_PseudoToFhandle: converts an id in the pseudo fs to a NFSv4 file handle
 *
 * Converts an id in the pseudo fs to a NFSv4 file handle.
 *
 * @param fh4p      [OUT] pointer to nfsv4 filehandle
 * @param psfsentry [IN]  pointer to pseudofs entry
 *
 */

void nfs4_PseudoToFhandle(nfs_fh4 *fh4p, pseudofs_entry_t *psfsentry)
{
	file_handle_v4_t *fhandle4;

	/* clean whole thing */
	memset(fh4p->nfs_fh4_val, 0, sizeof(struct alloc_file_handle_v4));

	fhandle4 = (file_handle_v4_t *) fh4p->nfs_fh4_val;
	fhandle4->fhversion = GANESHA_FH_VERSION;
	fhandle4->exportid = 0;
	memcpy(fhandle4->fsopaque, psfsentry->fsopaque, V4_FH_OPAQUE_SIZE);
	fhandle4->fs_len = V4_FH_OPAQUE_SIZE;

	fh4p->nfs_fh4_len = sizeof(file_handle_v4_t) + fhandle4->fs_len;

	if (isFullDebug(COMPONENT_NFS_V4_PSEUDO)) {
		char str[256];

		sprint_mem(str, psfsentry->fsopaque, V4_FH_OPAQUE_SIZE);

		LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
			     "PSEUDO_TO_FH: name:%s handle:%s",
			     psfsentry->name,
			     str);
	}
}				/* nfs4_PseudoToFhandle */

/**
 * nfs4_op_getattr_pseudo: Gets attributes for directory in pseudo fs
 *
 * Gets attributes for directory in pseudo fs. These are hardcoded constants.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK
 *
 */

int nfs4_op_getattr_pseudo(struct nfs_argop4 *op, compound_data_t *data,
			   struct nfs_resop4 *resp)
{
	GETATTR4args * const arg_GETATTR4 = &op->nfs_argop4_u.opgetattr;
	GETATTR4res * const res_GETATTR4 = &resp->nfs_resop4_u.opgetattr;
	pseudofs_entry_t *psfsentry;
	resp->resop = NFS4_OP_GETATTR;

	/* Get the pseudo entry related to this fhandle */
	res_GETATTR4->status = nfs4_CurrentFHToPseudo(data, &psfsentry);

	if (res_GETATTR4->status != NFS4_OK)
		return res_GETATTR4->status;

	/* All directories in pseudo fs have the same Fattr */
	if (nfs4_PseudoToFattr(
			psfsentry,
			&res_GETATTR4->GETATTR4res_u.resok4.obj_attributes,
			data,
			&(data->currentFH),
			&(arg_GETATTR4->attr_request)) != 0)
		res_GETATTR4->status = NFS4ERR_RESOURCE;
	else
		res_GETATTR4->status = NFS4_OK;

	LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
		     "attrmask(bitmap4_len)=%d attrlist4_len=%d",
		     res_GETATTR4->GETATTR4res_u.resok4.obj_attributes.attrmask.
		     bitmap4_len,
		     res_GETATTR4->GETATTR4res_u.resok4.obj_attributes.
		     attr_vals.attrlist4_len);

	return res_GETATTR4->status;
}				/* nfs4_op_getattr */

/**
 * nfs4_op_access_pseudo: Checks for object accessibility in pseudo fs.
 *
 * Checks for object accessibility in pseudo fs. All entries in pseudo fs
 * return can't be accessed as ACCESS4_MODIFY|ACCESS4_EXTEND|ACCESS4_DELETE
 * because pseudo fs is behaving as a read-only fs.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK
 *
 */

/* Shorter notation to avoid typos */
int nfs4_op_access_pseudo(struct nfs_argop4 *op, compound_data_t *data,
			  struct nfs_resop4 *resp)
{
	ACCESS4args * const arg_ACCESS4 = &op->nfs_argop4_u.opaccess;
	ACCESS4res * const res_ACCESS4 = &resp->nfs_resop4_u.opaccess;
	resp->resop = NFS4_OP_ACCESS;

	/* All access types are supported */
	res_ACCESS4->ACCESS4res_u.resok4.supported = ACCESS4_READ |
						     ACCESS4_LOOKUP;

	/* DELETE/MODIFY/EXTEND are not supported in the pseudo fs */
	res_ACCESS4->ACCESS4res_u.resok4.access = arg_ACCESS4->access &
						  ~(ACCESS4_MODIFY |
						    ACCESS4_EXTEND |
						    ACCESS4_DELETE);

	return NFS4_OK;
}				/* nfs4_op_access_pseudo */

/**
 * @brief set_compound_data_for_pseudo: fills in compound data for pseudo fs
 *
 * Fills in:
 *
 * data->current_entry
 * data->current_filetype
 * data->export
 * data->export_perms.options
 *
 * @param data  [INOUT] Pointer to the compound request's data
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 */
int set_compound_data_for_pseudo(compound_data_t *data)
{
	pseudofs_entry_t *dummy;

	if (data->current_entry)
		cache_inode_put(data->current_entry);

	if (data->current_ds)
		data->current_ds->ops->put(data->current_ds);

	if (data->req_ctx->export != NULL) {
		put_gsh_export(data->req_ctx->export);
		data->req_ctx->export = NULL;
	}

	/* No cache inode entry for the directory within pseudo fs */
	data->current_ds = NULL;
	data->current_entry = NULL;	/* No cache inode entry */
	data->current_filetype = DIRECTORY;	/* Always a directory */
	data->req_ctx->export = NULL;
	data->export = NULL;	/* No exportlist is related to pseudo fs */
	data->export_perms.options = EXPORT_OPTION_ROOT |
				     EXPORT_OPTION_MD_READ_ACCESS |
				     EXPORT_OPTION_AUTH_TYPES |
				     EXPORT_OPTION_NFSV4 |
				     EXPORT_OPTION_TRANSPORTS;

	/* Make sure the handle is good. */
	return nfs4_CurrentFHToPseudo(data, &dummy);
}

/**
 * @brief Looks up into the pseudo fs.
 *
 * Looks up into the pseudo fs. If a junction traversal is detected,
 * does the necessary stuff for correcting traverse.
 *
 * @param[in]     op    nfs4_op arguments
 * @param[in,out] data  Compound request's data
 * @param[in]     resp  nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 */

/* Shorter notation to avoid typos */

int nfs4_op_lookup_pseudo(struct nfs_argop4 *op, compound_data_t *data,
			  struct nfs_resop4 *resp)
{
	LOOKUP4args * const arg_LOOKUP4 = &op->nfs_argop4_u.oplookup;
	LOOKUP4res * const res_LOOKUP4 = &resp->nfs_resop4_u.oplookup;
	char *name;
	pseudofs_entry_t *psfsentry;
	pseudofs_entry_t *iter = NULL;
	int error = 0;
	cache_inode_status_t cache_status;
	cache_entry_t *entry = NULL;

	resp->resop = NFS4_OP_LOOKUP;

	/* Validate and convert the UFT8 objname to a regular string */
	res_LOOKUP4->status = nfs4_utf8string2dynamic(&arg_LOOKUP4->objname,
						      UTF8_SCAN_ALL,
						      &name);

	if (res_LOOKUP4->status != NFS4_OK)
		goto out;

	/* Get the pseudo fs entry related to the file handle */
	res_LOOKUP4->status = nfs4_CurrentFHToPseudo(data, &psfsentry);

	if (res_LOOKUP4->status != NFS4_OK)
		goto out;

	/* Search for name in pseudo fs directory */
	for (iter = psfsentry->sons; iter != NULL; iter = iter->next) {
		if (!strcmp(iter->name, name))
			break;
	}

	if (iter == NULL) {
		res_LOOKUP4->status = NFS4ERR_NOENT;
		goto out;
	}

	/* A matching entry was found */
	if (iter->junction_export == NULL) {
		/* The entry is not a junction, we stay within the pseudo fs */
		nfs4_PseudoToFhandle(&(data->currentFH), iter);

		/* Mark current_stateid as invalid */
		data->current_stateid_valid = false;
	} else {
		/* The entry is a junction */
		LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
			    "A junction in pseudo fs is traversed: name = %s, id = %d",
			    iter->name, iter->junction_export->id);
		/**
		 * @todo Danger Will Robinson!!
		 * We do a get_gsh_export here to take a reference on the export
		 * The whole pseudo fs tree is build directly from the
		 * exportlist without gsh_export reference even though the
		 * export list itself was created via get_gsh_export().
		 * We need a reference here because we are transitioning the
		 * junction and need to reference the export on the other side.
		 * What really needs to happen is that junction_export is of
		 * type struct gsh_export * and we fill it with get_gsh_export.
		 * A junction crossing would put this (the pseudo's export) and
		 * get the one on the other side. But then, that would require
		 * the pseudo fs to be a FSAL... Hence, we hack it here until
		 * the next dev cycle.
		 */

		data->req_ctx->export =
		    get_gsh_export(iter->junction_export->id, true);

		assert(data->req_ctx->export != NULL);
		data->export = &data->req_ctx->export->export;

		/* Build credentials */
		res_LOOKUP4->status = nfs4_MakeCred(data);

		/* Test for access error (export should not be visible). */
		if (res_LOOKUP4->status == NFS4ERR_ACCESS) {
			/* If return is NFS4ERR_ACCESS then this client doesn't
			 * have access to this export, return NFS4ERR_NOENT to
			 * hide it. It was not visible in READDIR response.
			 */
			LogDebug(COMPONENT_NFS_V4_PSEUDO,
				 "NFS4ERR_ACCESS Hiding Export_Id %d Path %s with NFS4ERR_NOENT",
				 data->export->id, data->export->fullpath);
			res_LOOKUP4->status = NFS4ERR_NOENT;

			if (name)
				gsh_free(name);

			return res_LOOKUP4->status;
		}

		if (res_LOOKUP4->status != NFS4_OK) {
			LogMajor(COMPONENT_NFS_V4_PSEUDO,
				 "PSEUDO FS JUNCTION TRAVERSAL: Failed to get FSAL credentials for %s, id=%d",
				 data->export->fullpath, data->export->id);
			goto out;
		}

		cache_status = nfs_export_get_root_entry(data->export, &entry);

		if (cache_status != CACHE_INODE_SUCCESS) {
			LogMajor(COMPONENT_NFS_V4_PSEUDO,
				 "PSEUDO FS JUNCTION TRAVERSAL: Failed to get root for %s, id=%d, status = %s",
				 data->export->fullpath,
				 data->export->id,
				 cache_inode_err_str(cache_status));

			res_LOOKUP4->status = nfs4_Errno(cache_status);
			goto out;
		}

		cache_inode_lru_ref(entry, LRU_REQ_INITIAL);

		if (data->currentFH.nfs_fh4_val == NULL) {
			error = nfs4_AllocateFH(&data->currentFH);

			if (error != NFS4_OK) {
				LogMajor(COMPONENT_NFS_V4_PSEUDO,
					 "PSEUDO FS JUNCTION TRAVERSAL: Failed to allocate the first file handle");
				res_LOOKUP4->status = error;
				goto out;
			}
		}

		/* Build the nfs4 handle */
		if (!nfs4_FSALToFhandle(&data->currentFH, entry->obj_handle)) {
			LogMajor(COMPONENT_NFS_V4_PSEUDO,
				 "PSEUDO FS JUNCTION TRAVERSAL: Failed to build the first file handle");
			res_LOOKUP4->status = NFS4ERR_SERVERFAULT;
			goto out;
		}

		/* Mark current_stateid as invalid */
		data->current_stateid_valid = false;

		/* Deref the old one, ref and keep the entry within
		 * the compound data
		 */
		if (data->current_entry) {
			cache_inode_lru_unref(data->current_entry,
					      LRU_FLAG_NONE);
		}

		data->current_entry = entry;
		data->current_filetype = entry->type;
		entry = NULL;

	}			/* else */

	res_LOOKUP4->status = NFS4_OK;

 out:

	if (entry)
		cache_inode_lru_unref(entry, LRU_FLAG_NONE);
	if (name)
		gsh_free(name);

	return res_LOOKUP4->status;
}

/**
 * nfs4_op_lookupp_pseudo: looks up into the pseudo fs for the parent directory
 *
 * looks up into the pseudo fs for the parent directory of the current file
 * handle.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 */

int nfs4_op_lookupp_pseudo(struct nfs_argop4 *op, compound_data_t *data,
			   struct nfs_resop4 *resp)
{
	LOOKUPP4res * const res_LOOKUPP4 = &resp->nfs_resop4_u.oplookupp;
	pseudofs_entry_t *psfsentry;

	resp->resop = NFS4_OP_LOOKUPP;

	/* Get the pseudo fs entry related to the file handle */
	res_LOOKUPP4->status = nfs4_CurrentFHToPseudo(data, &psfsentry);

	if (res_LOOKUPP4->status != NFS4_OK)
		return res_LOOKUPP4->status;

	/* lookupp on the root on the pseudofs should return NFS4ERR_NOENT
	 * (RFC3530, page 166)
	 */
	if (psfsentry->parent == psfsentry) {
		res_LOOKUPP4->status = NFS4ERR_NOENT;
		return res_LOOKUPP4->status;
	}

	/* A matching entry was found */
	nfs4_PseudoToFhandle(&(data->currentFH), psfsentry->parent);

	/* Mark current_stateid as invalid */
	data->current_stateid_valid = false;

	/* Return the reference to the old current entry */
	if (data->current_entry)
		cache_inode_put(data->current_entry);

	data->current_entry = NULL;

	/* Fill in compound data */
	res_LOOKUPP4->status = set_compound_data_for_pseudo(data);

	return res_LOOKUPP4->status;
}				/* nfs4_op_lookupp_pseudo */

/**
 * nfs4_op_lookupp_pseudo_by_exp: looks up into the pseudo fs for the parent
 * directory
 *
 * looks up into the pseudo fs for the parent directory of the export.
 *
 * @param op             [IN]    pointer to nfs4_op arguments
 * @param data           [INOUT] Pointer to the compound request's data
 * @paranm exp_root_data [IN]    Pointer to the export root data
 * @param resp           [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 */
int nfs4_op_lookupp_pseudo_by_exp(struct nfs_argop4 *op, compound_data_t *data,
				  struct nfs_resop4 *resp)
{
	LOOKUPP4res * const res_LOOKUPP4 = &resp->nfs_resop4_u.oplookupp;
	pseudofs_entry_t *psfsentry = NULL;
	int i;

	resp->resop = NFS4_OP_LOOKUPP;

	/* Get the object pointer by using the reverse tab in
	 * the pseudofs structure
	 */
	LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
		    "Looking for pseudofs node with id:%" PRIu64,
		    data->req_ctx->export->export.exp_mounted_on_file_id);

	/* Get the pseudo fs entry related to the export */
	for (i = 0; i < gPseudoFs.last_pseudo_id; i++) {
		if (memcmp(
		     gPseudoFs.reverse_tab[i]->fsopaque,
		     &data->req_ctx->export->export.exp_mounted_on_file_id,
		     sizeof(data->req_ctx->export->export.
			    exp_mounted_on_file_id)) == 0) {
			psfsentry = gPseudoFs.reverse_tab[i];
			break;
		}
		if (isFullDebug(COMPONENT_NFS_V4_PSEUDO)) {
			char str[256];

			sprint_mem(str,
				   gPseudoFs.reverse_tab[i]->fsopaque,
				   V4_FH_OPAQUE_SIZE);

			LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
				     "pseudo_by_exp: Compared id:%" PRIu64
				     " handle:%s",
				     *(uint64_t *) gPseudoFs.reverse_tab[i]->
				     fsopaque, str);
		}
	}

	if (psfsentry == NULL)
		LogFatal(COMPONENT_NFS_V4_PSEUDO,
			 "Failed to find PseudoFS node associated with export");

	LogDebug(COMPONENT_NFS_V4_PSEUDO,
		 "LOOKUPP Traversing junction from Export_Id %d Pseudo %s back to pseudo fs id %"
		 PRIu64, data->req_ctx->export->export.id,
		 data->req_ctx->export->export.pseudopath,
		 (uint64_t) data->req_ctx->export->export.
		 exp_mounted_on_file_id);

	/* lookupp on the root on the pseudofs should return
	 * NFS4ERR_NOENT (RFC3530, page 166)
	 */
	if (psfsentry->parent == psfsentry) {
		LogDebug(COMPONENT_NFS_V4_PSEUDO,
			 "Returning NFS4ERR_NOENT because pseudo_id == 0");
		res_LOOKUPP4->status = NFS4ERR_NOENT;
		return res_LOOKUPP4->status;
	}

	/* A matching entry was found */
	nfs4_PseudoToFhandle(&(data->currentFH), psfsentry->parent);

	/* Mark current_stateid as invalid */
	data->current_stateid_valid = false;

	/* Return the reference to the old current entry */
	if (data->current_entry) {
		cache_inode_put(data->current_entry);
		data->current_entry = NULL;
	}

	/* Fill in compound data */
	res_LOOKUPP4->status = set_compound_data_for_pseudo(data);

	return res_LOOKUPP4->status;
}

enum junction_return_codes {
	JUNCTION_OK,
	JUNCTION_SKIP,
	JUNCTION_BAD_STATUS,
};

/**
 * @brief Fills in a junction readdir entry.
 *
 * Fills in a junction readdir entry.
 *
 * @param[in]  data          Compound request's data
 * @param[in]  arg_READDIR4  readdir args
 * @param[out] res_READDIR4  readdir results
 * @param[out] attrs         entry's attributes
 *
 * @return NFS4_OK if successfull, other values show an error.
 */
static enum junction_return_codes
fill_junction_ent(compound_data_t *data,
		  READDIR4args * const arg_READDIR4,
		  READDIR4res * const res_READDIR4,
		  fattr4 *attrs,
		  nfs_fh4 *entryFH,
		  pseudofs_entry_t *iter)
{
	exportlist_t *save_export;
	export_perms_t save_export_perms;
	struct gsh_export *saved_gsh_export;
	cache_entry_t *entry = NULL;
	cache_inode_status_t cache_status;

	/* This is a junction. Code used to not recognize this
	 * which resulted in readdir giving different attributes
	 * (including FH, FSid, etc...) to clients from a
	 * lookup. AIX refused to list the directory because of
	 * this. Now we go to the junction to get the
	 * attributes.
	 */
	LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
		    "Offspring DIR %s pseudo_id %" PRIu64
		    " is a junction Export_id %d Path %s",
		    iter->name, iter->pseudo_id,
		    iter->junction_export->id,
		    iter->junction_export->fullpath);
	/* Save the compound data context */
	save_export = data->export;
	save_export_perms = data->export_perms;
	saved_gsh_export = data->req_ctx->export;

	data->export = iter->junction_export;
	data->req_ctx->export =
	    get_gsh_export(iter->junction_export->id, true);

	/* Build the credentials */
	/* XXX Is this really necessary for doing a lookup and
	 * getting attributes?
	 * The logic is borrowed from the process invoked above
	 * in this code
	 * when the target directory is a junction.
	 */
	res_READDIR4->status = nfs4_MakeCred(data);

	if (res_READDIR4->status == NFS4ERR_ACCESS) {
		/* If return is NFS4ERR_ACCESS then this client
		 * doesn't have access to this export, quietly
		 * skip the export.
		 */
		LogDebug(COMPONENT_NFS_V4_PSEUDO,
			 "NFS4ERR_ACCESS Skipping Export_Id %d Path %s",
			 data->export->id,
			 data->export->fullpath);

		put_gsh_export(data->req_ctx->export);
		data->export = save_export;
		data->export_perms = save_export_perms;
		data->req_ctx->export = saved_gsh_export;
		return JUNCTION_SKIP;
	}

	if (res_READDIR4->status == NFS4ERR_WRONGSEC) {
		/* Client isn't using the right SecType for this
		 * export, we will report NFS4ERR_WRONGSEC in
		 * FATTR4_RDATTR_ERROR.
		 *
		 * If the ONLY attributes requested are
		 * FATTR4_RDATTR_ERROR and
		 * FATTR4_MOUNTED_ON_FILEID we will not return
		 * an error and instead will return success with
		 * FATTR4_MOUNTED_ON_FILEID. AIX clients make
		 * this request and expect it to succeed.
		 */
		LogDebug(COMPONENT_NFS_V4_PSEUDO,
			 "NFS4ERR_WRONGSEC On ReadDir Export_Id %d Path %s",
			 data->export->id,
			 data->export->fullpath);

		if (check_for_wrongsec_ok_attr(&arg_READDIR4->attr_request)) {
			/* Client is requesting attr that are
			 * allowed when NFS4ERR_WRONGSEC occurs.
			 *
			 * Because we are not asking for any
			 * attributes which are a property of
			 * the exported file system's root,
			 * really just asking for
			 * MOUNTED_ON_FILEID, we can just get
			 * the attr for this pseudo fs node
			 * since it will result in the correct
			 * value for MOUNTED_ON_FILEID since
			 * pseudo fs FILEID and
			 * MOUNTED_ON_FILEID are always the
			 * same. FILEID of pseudo fs node is
			 * what we actually want here...
			 */
			if (nfs4_PseudoToFattr(iter,
					       attrs,
					       data,
					       NULL, /* don't need the FH */
					       &(arg_READDIR4->attr_request))
			    != 0) {
				LogFatal(COMPONENT_NFS_V4_PSEUDO,
					 "nfs4_PseudoToFattr failed to convert pseudo fs attr");
			}
			/* next step */
		} else
			if (check_for_rdattr_error(&arg_READDIR4->attr_request)
			   ) {
				/* report NFS4ERR_WRONGSEC */
				if (nfs4_Fattr_Fill_Error(attrs,
							  NFS4ERR_WRONGSEC)
				    != 0) {
					LogFatal(COMPONENT_NFS_V4_PSEUDO,
						 "nfs4_Fattr_Fill_Error failed to fill in RDATTR_ERROR");
			}
		} else {
			put_gsh_export(data->req_ctx->export);
			data->export = save_export;
			data->export_perms = save_export_perms;
			data->req_ctx->export = saved_gsh_export;
			return JUNCTION_BAD_STATUS;
		}
	} else {
		nfsstat4 status;

		/* Traverse junction to get attrs */
		cache_status = nfs_export_get_root_entry(iter->junction_export,
							 &entry);

		status = nfs4_Errno(cache_status);

		if (status != NFS4_OK) {
			LogMajor(COMPONENT_NFS_V4_PSEUDO,
				 "PSEUDO FS JUNCTION TRAVERSAL: Failed to get root for %s ,"
				 " id=%d, status = %s",
				 iter->junction_export->fullpath,
				 iter->junction_export->id,
				 cache_inode_err_str(cache_status));
		}
		/* Build the nfs4 handle. Again, we do this
		 * unconditionally.
		 */
		else if (!nfs4_FSALToFhandle(entryFH, entry->obj_handle)) {
			LogMajor(COMPONENT_NFS_V4_PSEUDO,
				 "PSEUDO FS JUNCTION TRAVERSAL: Failed to build the first file handle for %s, id=%d",
				 data->export->fullpath,
				 data->export->id);
			status = NFS4ERR_RESOURCE;
		} else {
			status = cache_entry_To_Fattr(
				entry,
				attrs,
				data,
				entryFH,
				&arg_READDIR4->attr_request);

			if (status != NFS4_OK) {
				LogMajor(COMPONENT_NFS_V4_PSEUDO,
					 "nfs4_FSALattr_To_Fattr failed to convert attr");
			}
		}
		if (status != NFS4_OK) {
			if (nfs4_Fattr_Fill_Error(attrs, status) != 0) {
				LogMajor(COMPONENT_NFS_V4_PSEUDO,
					 "nfs4_Fattr_Fill_Error failed to fill in RDATTR_ERROR");
				/* We just skip this entry,
				 * something bad has happened.
				 */
				put_gsh_export(data->req_ctx->export);
				data->req_ctx->export = saved_gsh_export;
				data->export = save_export;
				data->export_perms = save_export_perms;
				return JUNCTION_SKIP;
			}
		}
	}

	put_gsh_export(data->req_ctx->export);
	data->req_ctx->export = saved_gsh_export;
	data->export = save_export;
	data->export_perms = save_export_perms;

	return JUNCTION_OK;
}

/**
 * @brief Reads a directory in the pseudo fs
 *
 * Reads a directory in the pseudo fs.
 *
 * @param[in]     op    nfs4_op arguments
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 */

int nfs4_op_readdir_pseudo(struct nfs_argop4 *op, compound_data_t *data,
			   struct nfs_resop4 *resp)
{
	READDIR4args * const arg_READDIR4 = &op->nfs_argop4_u.opreaddir;
	READDIR4res * const res_READDIR4 = &resp->nfs_resop4_u.opreaddir;
	unsigned long dircount = 0;
	unsigned long maxcount = 0;
	unsigned long estimated_num_entries = 0;
	unsigned long i = 0;
	nfs_cookie4 cookie;
	verifier4 cookie_verifier;
	unsigned long space_used = 0;
	pseudofs_entry_t *psfsentry;
	pseudofs_entry_t *iter = NULL;
	entry4 *entry_nfs_array = NULL;
	nfs_fh4 entryFH = { 0, NULL };
	cache_inode_status_t cache_status;
	int error = 0;
	size_t namelen = 0;
	cache_entry_t *entry = NULL;

	resp->resop = NFS4_OP_READDIR;
	res_READDIR4->status = NFS4_OK;

	LogDebug(COMPONENT_NFS_V4_PSEUDO, "Entering NFS4_OP_READDIR_PSEUDO");

	/* get the caracteristic value for readdir operation */
	dircount = arg_READDIR4->dircount;
	maxcount = arg_READDIR4->maxcount;
	cookie = arg_READDIR4->cookie;
	space_used = sizeof(entry4);

	/* dircount is considered meaningless by many nfsv4 client
	 * (like the CITI one). we use maxcount instead
	 */
	estimated_num_entries = maxcount / sizeof(entry4);

	LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
		    "dircount=%lu, maxcount=%lu, cookie=%" PRIu64
		    ", sizeof(entry4)=%lu num_entries=%lu",
		    dircount, maxcount,
		    (uint64_t) cookie, space_used, estimated_num_entries);

	/* If maxcount is too short, return NFS4ERR_TOOSMALL */
	if (maxcount < sizeof(entry4) || estimated_num_entries == 0) {
		res_READDIR4->status = NFS4ERR_TOOSMALL;
		return res_READDIR4->status;
	}

	/* Now resolve the file handle to pseudo fs */
	res_READDIR4->status = nfs4_CurrentFHToPseudo(data, &psfsentry);

	if (res_READDIR4->status != NFS4_OK)
		return res_READDIR4->status;

	LogMidDebug(COMPONENT_NFS_V4_PSEUDO, "PSEUDOFS READDIR in %s",
		    psfsentry->name);

	/* If this a junction filehandle ? */
	if (psfsentry->junction_export != NULL) {
		/* This is a junction */
		LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
			    "DIR %s id=%u is a junction", psfsentry->name,
			    psfsentry->junction_export->id);

		/* Step up the compound data */

		/**
		 * @todo Danger Will Robinson!!
		 * This is the same hack as above in pseudo lookup
		 */
		data->req_ctx->export =
		    get_gsh_export(psfsentry->junction_export->id, true);

		data->export = &data->req_ctx->export->export;

		/* Build the credentials */
		res_READDIR4->status = nfs4_MakeCred(data);

		if (res_READDIR4->status != NFS4_OK) {
			LogMajor(COMPONENT_NFS_V4_PSEUDO,
				 "PSEUDO FS JUNCTION TRAVERSAL: Failed to get FSAL credentials for %s, id=%d",
				 data->export->fullpath, data->export->id);
			return res_READDIR4->status;
		}

		cache_status = nfs_export_get_root_entry(data->export, &entry);

		if (cache_status != CACHE_INODE_SUCCESS) {
			LogMajor(COMPONENT_NFS_V4_PSEUDO,
				 "PSEUDO FS JUNCTION TRAVERSAL: Failed to get root for %s, id=%d, status = %s",
				 data->export->fullpath, data->export->id,
				 cache_inode_err_str(cache_status));
			res_READDIR4->status = nfs4_Errno(cache_status);
			return res_READDIR4->status;
		}

		if (data->currentFH.nfs_fh4_val == NULL) {
			error = nfs4_AllocateFH(&data->currentFH);

			if (error != NFS4_OK) {
				LogMajor(COMPONENT_NFS_V4_PSEUDO,
					 "PSEUDO FS JUNCTION TRAVERSAL: Failed to allocate the first file handle");
				res_READDIR4->status = error;
				return res_READDIR4->status;
			}
		}

		/* Build the nfs4 handle */
		if (!nfs4_FSALToFhandle(&data->currentFH, entry->obj_handle)) {
			LogMajor(COMPONENT_NFS_V4_PSEUDO,
				 "PSEUDO FS JUNCTION TRAVERSAL: Failed to build the first file handle");
			res_READDIR4->status = NFS4ERR_SERVERFAULT;
			return res_READDIR4->status;
		}

		/* Mark current_stateid as invalid */
		data->current_stateid_valid = false;

		/* Keep the entry within the compound data */

		cache_inode_lru_ref(entry, LRU_REQ_INITIAL);

		if (data->current_entry)
			cache_inode_lru_unref(data->current_entry,
					      LRU_FLAG_NONE);

		data->current_entry = entry;
		data->current_filetype = entry->type;

		/* redo the call on the other side of the junction */
		return nfs4_op_readdir(op, data, resp);
	}

	/* Allocation of the entries array */
	entry_nfs_array = gsh_calloc(estimated_num_entries, sizeof(entry4));

	if (entry_nfs_array == NULL) {
		LogError(COMPONENT_NFS_V4_PSEUDO, ERR_SYS, ERR_MALLOC, errno);
		res_READDIR4->status = NFS4ERR_RESOURCE;
		return res_READDIR4->status;
	}

	/* Cookie verifier has the value of the
	 * Server Boot Time for pseudo fs
	 */
	memset(cookie_verifier, 0, NFS4_VERIFIER_SIZE);

#ifdef _WITH_COOKIE_VERIFIER
	/* BUGAZOMEU: management of the cookie verifier */
	if (NFS_SpecificConfig.UseCookieVerf == 1) {
		memcpy(cookie_verifier,
		       &ServerBootTime.tv_sec,
		       sizeof(cookie_verifier));

		if (cookie != 0) {
			if (memcmp(cookie_verifier,
				   arg_READDIR4->cookieverf,
				   NFS4_VERIFIER_SIZE) != 0) {
				res_READDIR4->status = NFS4ERR_BAD_COOKIE;
				gsh_free(entry_nfs_array);
				return res_READDIR4->status;
			}
		}
	}
#endif
	/* Cookie delivered by the server and used by the client SHOULD not
	 * be 0, 1 or 2 (cf RFC3530, page192)
	 * because theses value are reserved for special use.
	 *      0 - cookie for first READDIR
	 *      1 - reserved for . on client handside
	 *      2 - reserved for .. on client handside
	 * Entries '.' and '..' are not returned also
	 * For these reason, there will be an offset of 3 between NFS4 cookie
	 * and HPSS cookie
	 */

	/* make sure to start at the right position given by the cookie */
	iter = psfsentry->sons;
	if (cookie != 0) {
		for (; iter != NULL; iter = iter->next)
			if (iter->pseudo_id == cookie) {
				iter = iter->next;
				break;
			}
	}

	/* Here, where are sure that iter is set to the position
	 * indicated eventually by the cookie
	 */
	i = 0;

	for (; iter != NULL; iter = iter->next) {
		LogMidDebug(COMPONENT_NFS_V4_PSEUDO,
			    "PSEUDO FS: Found entry %s pseudo_id %" PRIu64,
			    iter->name, iter->pseudo_id);

		namelen = strlen(iter->name);
		entry_nfs_array[i].name.utf8string_len = namelen;

		entry_nfs_array[i].name.utf8string_val =
		     gsh_malloc(namelen + 1);

		if (entry_nfs_array[i].name.utf8string_val == NULL) {
			LogError(COMPONENT_NFS_V4_PSEUDO, ERR_SYS, ERR_MALLOC,
				 errno);
			res_READDIR4->status = NFS4ERR_RESOURCE;
			return res_READDIR4->status;
		}

		memcpy(entry_nfs_array[i].name.utf8string_val,
		       iter->name,
		       namelen);

		entry_nfs_array[i].name.utf8string_val[namelen] = '\0';

		entry_nfs_array[i].cookie = iter->pseudo_id;

		/* This used to be in an if with a bogus check for
		 * FATTR4_FILEHANDLE. Such a common case, elected
		 * to set up FH for call to xxxx_ToFattr unconditionally.
		 */
		if (entryFH.nfs_fh4_val == NULL) {
			if (nfs4_AllocateFH(&entryFH) != NFS4_OK)
				return res_READDIR4->status;
		}

		/* Do the case where we stay within the pseudo file system. */
		if (iter->junction_export == NULL) {

			nfs4_PseudoToFhandle(&entryFH, iter);

			if (nfs4_PseudoToFattr(iter,
					       &entry_nfs_array[i].attrs,
					       data,
					       &entryFH,
					       &arg_READDIR4->attr_request)
			    != 0) {
				LogFatal(COMPONENT_NFS_V4_PSEUDO,
					 "nfs4_PseudoToFattr failed to convert pseudo fs attr");
			}
		} else {
			switch (fill_junction_ent(data,
						  arg_READDIR4,
						  res_READDIR4,
						  &entry_nfs_array[i].attrs,
						  &entryFH,
						  iter)) {
				case JUNCTION_OK:
					break;

				case JUNCTION_SKIP:
					continue;

				case JUNCTION_BAD_STATUS:
					return res_READDIR4->status;
			}
		}

		/* Chain the entry together */
		entry_nfs_array[i].nextentry = NULL;
		if (i != 0)
			entry_nfs_array[i - 1].nextentry = &entry_nfs_array[i];

		/* Increment the counter */
		i += 1;

		/* Did we reach the maximum number of entries */
		if (i == estimated_num_entries)
			break;
	}

	/* Build the reply */
	memcpy(res_READDIR4->READDIR4res_u.resok4.cookieverf, cookie_verifier,
	       NFS4_VERIFIER_SIZE);
	if (i == 0)
		res_READDIR4->READDIR4res_u.resok4.reply.entries = NULL;
	else
		res_READDIR4->READDIR4res_u.resok4.reply.entries =
		    entry_nfs_array;

	/* did we reach the end ? */
	if (iter == NULL) {
		/* Yes, we did */
		res_READDIR4->READDIR4res_u.resok4.reply.eof = TRUE;
	} else {
		/* No, there are some more entries */
		res_READDIR4->READDIR4res_u.resok4.reply.eof = FALSE;
	}

	/* Exit properly */
	res_READDIR4->status = NFS4_OK;

	return NFS4_OK;
}				/* nfs4_op_readdir_pseudo */
