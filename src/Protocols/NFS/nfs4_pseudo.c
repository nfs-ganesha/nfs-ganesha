/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"

#define NB_TOK_ARG 10
#define NB_OPT_TOK 10
#define NB_TOK_PATH 20

static pseudofs_t gPseudoFs;

/**
 * nfs4_PseudoToId: TConverts a file handle (to a pseudo object) to the id of this pseudo object in the pseudofs
 * 
 *  This routine merely extracts a field from the file handle which is not seen as opaque in this case. Because
 *  file handle are opaque structure, it is prefered to have a dedicated function for this and so hiding the
 *  file handle internal structure.
 * 
 *  @param fh4p       [IN]  pointer to the file handle to process. 
 * 
 *  @return the pseudo id found
 *  @see   nfs_GetPseudoFs
 * 
 */

uint64_t nfs4_PseudoToId(nfs_fh4 * fh4p)
{
  file_handle_v4_t *pfhandle4;
  uint64_t out = 0LL;

  pfhandle4 = (file_handle_v4_t *) (fh4p->nfs_fh4_val);

  out = (uint64_t) (pfhandle4->pseudofs_id);
  return out;
}                               /* nfs4_PseudoToId */

/**
 * nfs4_GetPseudoFs: Gets the root of the pseudo file system.
 * 
 * Gets the root of the pseudo file system. This is only a wrapper to static variable gPseudoFs. 
 *
 * @return the pseudo fs root 
 * 
 */

pseudofs_t *nfs4_GetPseudoFs(void)
{
  return &gPseudoFs;
}                               /*  nfs4_GetExportList */

/**
 * nfs4_ExportToPseudoFS: Build a pseudo fs from an exportlist
 * 
 * Build a pseudo fs from an exportlist. This export list itself is obtained by reading the configuration file. 
 *
 * @return the pseudo fs root 
 * 
 */

int nfs4_ExportToPseudoFS(exportlist_t * pexportlist)
{
  exportlist_t *entry;
  exportlist_t *next;           /* exportlist entry   */
  int i = 0;
  int j = 0;
  int found = 0;
  char tmp_pseudopath[MAXPATHLEN];
  char *PathTok[NB_TOK_PATH];
  int NbTokPath;
  pseudofs_t *PseudoFs = NULL;
  pseudofs_entry_t *PseudoFsRoot = NULL;
  pseudofs_entry_t *PseudoFsCurrent = NULL;
  pseudofs_entry_t *newPseudoFsEntry = NULL;
  pseudofs_entry_t *iterPseudoFs = NULL;

  entry = pexportlist;

  PseudoFs = &gPseudoFs;

  /* Init Root of the Pseudo FS tree */
  strncpy(PseudoFs->root.name, "/", MAXNAMLEN);
  strncpy(PseudoFs->root.fullname, "(nfsv4root)", MAXPATHLEN);
  PseudoFs->root.pseudo_id = 0;
  PseudoFs->root.junction_export = NULL;
  PseudoFs->root.next = NULL;
  PseudoFs->root.last = PseudoFsRoot;
  PseudoFs->root.sons = NULL;
  PseudoFs->root.parent = &(PseudoFs->root);    /* root is its own parent */

  /* Allocation of the parsing table */
  for(i = 0; i < NB_TOK_PATH; i++)
    if((PathTok[i] = gsh_malloc(MAXNAMLEN)) == NULL)
      return ENOMEM;

  while(entry)
    {
      /* To not forget to init "/" entry */
      PseudoFsCurrent = &(PseudoFs->root);
      PseudoFs->reverse_tab[0] = &(PseudoFs->root);

      /* skip exports that aren't for NFS v4 */
      if((entry->options & EXPORT_OPTION_NFSV4) == 0)
        {
          next = entry->next;
          entry = next;
          continue;
        }

      if(entry->options & EXPORT_OPTION_PSEUDO)
        {
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: Id          = %d",
                       entry->id);
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: ANON        = %d",
                       entry->anonymous_uid);
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: Path        = %s",
                       entry->fullpath);
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: Options     = 0x%x",
                       entry->options);
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: Num Clients = %d",
                       entry->clients.num_clients);

          /* A pseudo path is to ne managed */
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                       "BUILDING PSEUDOFS: Now managing %s seen as %s",
                       entry->fullpath, entry->pseudopath);

          /* Parsing the path */
          strncpy(tmp_pseudopath, entry->pseudopath, MAXPATHLEN);
          if((NbTokPath =
              nfs_ParseConfLine(PathTok, NB_TOK_PATH, tmp_pseudopath, find_slash,
                                find_endLine)) < 0)
            {
              /* Path is badly formed */
              LogCrit(COMPONENT_NFS_V4_PSEUDO,
                      "BUILDING PSEUDOFS: Invalid 'pseudo' option: %s",
                      entry->pseudopath);
              next = entry->next;
              entry = next;
              continue;
            }

          /* there must be a leading '/' in the pseudo path */
          if(entry->pseudopath[0] != '/')
            {
              /* Path is badly formed */
              LogCrit(COMPONENT_NFS_V4_PSEUDO,
                      "Pseudo Path '%s' is badly formed",
                      entry->pseudopath);
              next = entry->next;
              entry = next;
              continue;
            }

          /* Loop on each token. Because first character in pseudo path is '/'
           * we can avoid looking at PathTok[0] which is necessary '\0'. That's 
           * the reason why we start looping at pos = 1 */
          for(j = 1; j < NbTokPath; j++)
            LogFullDebug(COMPONENT_NFS_V4, "     tokens are #%s#", PathTok[j]);

          for(j = 1; j < NbTokPath; j++)
            {
              found = 0;
              for(iterPseudoFs = PseudoFsCurrent->sons; iterPseudoFs != NULL;
                  iterPseudoFs = iterPseudoFs->next)
                {
                  /* Looking for a matching entry */
                  if(!strcmp(iterPseudoFs->name, PathTok[j]))
                    {
                      found = 1;
                      break;
                    }
                }               /* for iterPseudoFs */

              if(found)
                {
                  /* a matching entry was found in the tree */
                  PseudoFsCurrent = iterPseudoFs;
                }
              else
                {
                  /* a new entry is to be created */
                  if((newPseudoFsEntry =
                      gsh_malloc(sizeof(pseudofs_entry_t))) == NULL)
                    return ENOMEM;

                  /* Creating the new entry, allocate an id for it and add it to reverse tab */
                  strncpy(newPseudoFsEntry->name, PathTok[j], MAXNAMLEN);
                  newPseudoFsEntry->pseudo_id = PseudoFs->last_pseudo_id + 1;
                  PseudoFs->last_pseudo_id = newPseudoFsEntry->pseudo_id;
                  PseudoFs->reverse_tab[PseudoFs->last_pseudo_id] = newPseudoFsEntry;
                  newPseudoFsEntry->junction_export = NULL;
                  newPseudoFsEntry->last = newPseudoFsEntry;
                  newPseudoFsEntry->next = NULL;
                  newPseudoFsEntry->sons = NULL;
                  snprintf(newPseudoFsEntry->fullname, MAXPATHLEN, "%s/%s",
                           PseudoFsCurrent->fullname, PathTok[j]);

                  /* Step into the new entry and attach it to the tree */
                  if(PseudoFsCurrent->sons == NULL)
                    PseudoFsCurrent->sons = newPseudoFsEntry;
                  else
                    {
                      PseudoFsCurrent->sons->last->next = newPseudoFsEntry;
                      PseudoFsCurrent->sons->last = newPseudoFsEntry;
                    }
                  newPseudoFsEntry->parent = PseudoFsCurrent;
                  PseudoFsCurrent = newPseudoFsEntry;
                }

            }                   /* for j */

          /* Now that all entries are added to pseudofs tree, add the junction to the pseudofs */
          PseudoFsCurrent->junction_export = entry;

        }
      /* if( entry->options & EXPORT_OPTION_PSEUDO ) */
      next = entry->next;

      entry = next;
    }                           /* while( entry ) */

  /* desalocation of the parsing table */
  for(i = 0; i < NB_TOK_PATH; i++)
    gsh_free(PathTok[i]);

  return (0);
}

/**
 * nfs4_PseudoToFattr: Gets the attributes for an entry in the pseudofs
 * 
 * Gets the attributes for an entry in the pseudofs. Because pseudo fs structure is very simple (it is read-only and contains
 * only directory that belongs to root), a set of standardized values is returned
 * 
 * @param psfp       [IN]    pointer to the pseudo fs entry on which attributes are queried
 * @param Fattr      [OUT]   Pointer to the buffer that will contain the queried attributes
 * @param data       [INOUT] Pointer to the compound request's data
 * @param Bitmap     [IN]    Pointer to a bitmap that describes the attributes to be returned
 * 
 * @return 0 if successfull, -1 if something wrong occured. In this case, the reason is that too many attributes were asked.
 * 
 */

int nfs4_PseudoToFattr(pseudofs_entry_t * psfsp,
                       fattr4 * Fattr,
                       compound_data_t * data, nfs_fh4 * objFH, struct bitmap4 * Bitmap)
{
	struct attrlist attrs;

	/* cobble up an inode (attributes) for this pseudo */
	memset(&attrs, 0, sizeof(attrs));
	attrs.type = DIRECTORY;
	attrs.filesize = DEV_BSIZE;
	if(psfsp->junction_export == NULL) {
		attrs.fsid.major = 152LL;
		attrs.fsid.minor = 152LL;
	} else {
		attrs.fsid.major = 153LL;  /* @todo BUGAZOMEU : tres cradem mais utile */
		attrs.fsid.minor = nfs_htonl64(153LL);
	}
	attrs.fileid = psfsp->pseudo_id;
	attrs.mode = unix2fsal_mode(0555);
	attrs.numlinks = 2; /* not 1.  for '.' and '../me' */
	attrs.owner = 0; /* root */
	attrs.group = 2; /* daemon? */
	attrs.atime.seconds = ServerBootTime;
	attrs.ctime.seconds = ServerBootTime;
	attrs.chgtime.seconds = ServerBootTime;
	attrs.change = ServerBootTime;
	attrs.spaceused = DEV_BSIZE;
	attrs.mounted_on_fileid = psfsp->pseudo_id;
	return nfs4_FSALattr_To_Fattr(&attrs, Fattr, data, objFH, Bitmap);
}                               /* nfs4_PseudoToFattr */

/**
 * nfs4_FhandleToPseudo: converts  a NFSv4 file handle fs to an id in the pseudo
 * 
 * Converts  a NFSv4 file handle fs to an id in the pseudo, and check if the fh is related to a pseudo entry
 *
 * @param fh4p      [IN] pointer to nfsv4 filehandle
 * @param psfsentry [OUT]  pointer to pseudofs entry
 * 
 * @return TRUE if successfull, FALSE if an error occured (this means the fh4 was not related to a pseudo entry)
 * 
 */
int nfs4_FhandleToPseudo(nfs_fh4 * fh4p, pseudofs_t * psfstree,
                         pseudofs_entry_t * psfsentry)
{
  file_handle_v4_t *pfhandle4;

  /* Map the filehandle to the correct structure */
  pfhandle4 = (file_handle_v4_t *) (fh4p->nfs_fh4_val);

  /* The function must be called with a fh pointed to a pseudofs entry */
  if(!(pfhandle4->pseudofs_flag))
    return false;

  /* Get the object pointer by using the reverse tab in the pseudofs structure */
  memcpy(psfsentry, psfstree->reverse_tab[pfhandle4->pseudofs_id],
         sizeof(pseudofs_entry_t));

  return true;
}                               /* nfs4_FhandleToPseudo */

/**
 * nfs4_PseudoToFhandle: converts an id in the pseudo fs to a NFSv4 file handle
 * 
 * Converts an id in the pseudo fs to a NFSv4 file handle. 
 *
 * @param fh4p      [OUT] pointer to nfsv4 filehandle
 * @param psfsentry [IN]  pointer to pseudofs entry
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */

int nfs4_PseudoToFhandle(nfs_fh4 * fh4p, pseudofs_entry_t * psfsentry)
{
  file_handle_v4_t *fhandle4;

  memset(fh4p->nfs_fh4_val, 0, sizeof(struct alloc_file_handle_v4)); /* clean whole thing */
  fhandle4 = (file_handle_v4_t *)fh4p->nfs_fh4_val;
  fhandle4->fhversion = GANESHA_FH_VERSION;
  fhandle4->pseudofs_flag = true;
  fhandle4->pseudofs_id = psfsentry->pseudo_id;

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO, "PSEUDO_TO_FH: Pseudo id = %d -> %d",
               psfsentry->pseudo_id, fhandle4->pseudofs_id);

  fh4p->nfs_fh4_len = sizeof(file_handle_v4_t); /* no handle in opaque */

  return true;
}                               /* nfs4_PseudoToFhandle */

/**
 * nfs4_CreateROOTFH: Creates the file handle for the "/" of the pseudo file system
 *
 *  Creates the file handle for the "/" of the pseudo file syste.
 * 
 * @param fh4p [OUT]   pointer to the file handle to be allocated
 * @param data [INOUT] pointer to the compound request's data
 * 
 * @return NFS4_OK if successfull, NFS4ERR_BADHANDLE if an error occured when creating the file handle.
 *
 */

int nfs4_CreateROOTFH4(nfs_fh4 * fh4p, compound_data_t * data)
{
  pseudofs_entry_t psfsentry;
  int status = 0;

  psfsentry = *(data->pseudofs->reverse_tab[0]);

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "CREATE ROOTFH (pseudo): root to pseudofs = #%s#",
               psfsentry.name);

  if((status = nfs4_AllocateFH(&(data->rootFH))) != NFS4_OK)
    return status;

  if(!nfs4_PseudoToFhandle(&(data->rootFH), &psfsentry))
    {
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                   "CREATE ROOTFH (pseudo): Creation of root fh is impossible");
      return NFS4ERR_BADHANDLE;
    }

  /* Test */
  if(isFullDebug(COMPONENT_NFS_V4))
    {
      char str[LEN_FH_STR];
      sprint_fhandle4(str, &data->rootFH);
      LogFullDebug(COMPONENT_NFS_V4, "CREATE ROOT FH: %s", str);
    }

  return NFS4_OK;
}                               /* nfs4_CreateROOTFH4 */

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

#define arg_GETATTR4 op->nfs_argop4_u.opgetattr
#define res_GETATTR4 resp->nfs_resop4_u.opgetattr

int nfs4_op_getattr_pseudo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp)
{
  pseudofs_entry_t psfsentry;
  resp->resop = NFS4_OP_GETATTR;

  /* Get the pseudo entry related to this fhandle */
  if(!nfs4_FhandleToPseudo(&(data->currentFH), data->pseudofs, &psfsentry))
    {
      res_GETATTR4.status = NFS4ERR_BADHANDLE;
      return res_GETATTR4.status;
    }

  /* All directories in pseudo fs have the same Fattr */
  if(nfs4_PseudoToFattr(&psfsentry,
                        &(res_GETATTR4.GETATTR4res_u.resok4.obj_attributes),
                        data, &(data->currentFH), &(arg_GETATTR4.attr_request)) != 0)
    res_GETATTR4.status = NFS4ERR_SERVERFAULT;
  else
    res_GETATTR4.status = NFS4_OK;

  LogFullDebug(COMPONENT_NFS_V4,
               "Apres nfs4_PseudoToFattr: attrmask(bitmap4_len)=%d attrlist4_len=%d",
               res_GETATTR4.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len,
               res_GETATTR4.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len);

  return res_GETATTR4.status;
}                               /* nfs4_op_getattr */

/**
 * nfs4_op_access_pseudo: Checks for object accessibility in pseudo fs. 
 * 
 * Checks for object accessibility in pseudo fs. All entries in pseudo fs return can't be accessed as 
 * ACCESS4_MODIFY|ACCESS4_EXTEND|ACCESS4_DELETE because pseudo fs is behaving as a read-only fs.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK 
 * 
 */

/* Shorter notation to avoid typos */
#define res_ACCESS4 resp->nfs_resop4_u.opaccess
#define arg_ACCESS4 op->nfs_argop4_u.opaccess

int nfs4_op_access_pseudo(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp)
{
  resp->resop = NFS4_OP_ACCESS;

  /* All access types are supported */
  res_ACCESS4.ACCESS4res_u.resok4.supported = ACCESS4_READ | ACCESS4_LOOKUP;

  /* DELETE/MODIFY/EXTEND are not supported in the pseudo fs */
  res_ACCESS4.ACCESS4res_u.resok4.access =
      arg_ACCESS4.access & ~(ACCESS4_MODIFY | ACCESS4_EXTEND | ACCESS4_DELETE);

  return NFS4_OK;
}                               /* nfs4_op_access_pseudo */

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
#define arg_LOOKUP4 op->nfs_argop4_u.oplookup
#define res_LOOKUP4 resp->nfs_resop4_u.oplookup

int nfs4_op_lookup_pseudo(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp)
{
  char name[MAXNAMLEN];
  pseudofs_entry_t psfsentry;
  pseudofs_entry_t *iter = NULL;
  bool found = false;
  bool pseudo_is_slash = false;
  int error = 0;
  cache_inode_status_t cache_status = 0;
  fsal_status_t fsal_status;
/*   cache_inode_fsal_data_t fsdata; */
  char pathfsal[MAXPATHLEN] ;
  struct fsal_export *exp_hdl;
  struct fsal_obj_handle *fsal_handle;
  cache_entry_t *pentry = NULL;

  resp->resop = NFS4_OP_LOOKUP;

  /* UTF8 strings may not end with \0, but they carry their length */
  utf82str(name, sizeof(name), &arg_LOOKUP4.objname);

  /* Get the pseudo fs entry related to the file handle */
  if(!nfs4_FhandleToPseudo(&(data->currentFH), data->pseudofs, &psfsentry))
    {
      res_LOOKUP4.status = NFS4ERR_BADHANDLE;
      return res_LOOKUP4.status;
    }

 
  /* If "/" is set as pseudopath, then gPseudoFS.root.junction_export is not NULL but 
   * gPseudoFS.root has no son */
  if( ( gPseudoFs.root.junction_export != NULL ) && ( gPseudoFs.root.sons == NULL )  )
   {
	iter = &gPseudoFs.root ;
        pseudo_is_slash = true ;
        found = true ;
   }
  else
   {
     found = false;
     for(iter = psfsentry.sons; iter != NULL; iter = iter->next)
       {
         if(!strcmp(iter->name, name))
           {
             found = true;
             break;
           } 
       } /* for */
    } /* else */

  if(!found)
    {
      res_LOOKUP4.status = NFS4ERR_NOENT;
      return res_LOOKUP4.status;
    }

  /* A matching entry was found */
  if(iter->junction_export == NULL)
    {
      /* The entry is not a junction, we stay within the pseudo fs */
      if(!nfs4_PseudoToFhandle(&(data->currentFH), iter))
        {
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }
    }
  else
    {
      /* The entry is a junction */
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,      
                   "A junction in pseudo fs is traversed: name = %s, id = %d",
                   iter->name, iter->junction_export->id);
      data->pexport = iter->junction_export;
      strncpy(data->MntPath, iter->fullname, NFS_MAXPATHLEN);

      /* Build credentials */
      if(nfs4_MakeCred(data) != 0)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to get FSAL credentials for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          res_LOOKUP4.status = NFS4ERR_WRONGSEC;
          return res_LOOKUP4.status;
        }

      /* Build fsal data for creation of the first entry */
      if( !pseudo_is_slash )
        {
          strncpy( pathfsal, data->pexport->fullpath, MAXPATHLEN ) ;
	}
      else
       {
	 pathfsal[0] = '/';
         strncat(&pathfsal[1], name, MAXPATHLEN - 2);
       }

      /* Lookup the FSAL to build the fsal handle */
      exp_hdl = data->pexport->export_hdl;
      fsal_status = exp_hdl->ops->lookup_path(exp_hdl, data->req_ctx,
                                              pathfsal, &fsal_handle);
      if(FSAL_IS_ERROR(fsal_status))
        {
	  LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to lookup for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: fsal_status = ( %d, %d )",
                   fsal_status.major, fsal_status.minor);
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }

      if(data->mounted_on_FH.nfs_fh4_len == 0)
        {
          if((error = nfs4_AllocateFH(&(data->mounted_on_FH))) != NFS4_OK)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to allocate the 'mounted on' file handle");
              res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
              return res_LOOKUP4.status;
            }
        }

      if(data->currentFH.nfs_fh4_len == 0)
        {
          if((error = nfs4_AllocateFH(&(data->currentFH))) != NFS4_OK)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to allocate the first file handle");
              res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
              return res_LOOKUP4.status;
            }
        }

      /* Build the nfs4 handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, fsal_handle, data))
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to build the first file handle");
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }

      /* The new fh is to be the "mounted on Filehandle" */
      memcpy(data->mounted_on_FH.nfs_fh4_val, data->currentFH.nfs_fh4_val,
             sizeof(file_handle_v4_t));
      data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

      /* Add the entry to the cache as a root (BUGAZOMEU: make it a junction entry when junction is available) */
/** @TODO make_root calls new_entry which will free the object.
 * this may happen a lot as we traverse pseudos.  Might we have a lookahead
 * or think of a better way to handle this once the pseudo has been cached?
 * leave the handle_to_key here for a bit till we sort this out.
 * maybe the fsal lookup above should be a cache_inode_lookup??
 */
/*       fsal_handle->ops->handle_to_key(fsal_handle, &fsdata.fh_desc); */

      if((pentry = cache_inode_make_root(fsal_handle,
                                         &cache_status)) == NULL)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Allocate root entry in cache inode failed, for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }

      /* Keep the pentry within the compound data */
      if (data->current_entry) {
          cache_inode_put(data->current_entry);
      }
      data->current_entry = pentry;
      data->current_filetype = pentry->type;

    }                           /* else */


  res_LOOKUP4.status = NFS4_OK;
  return NFS4_OK;
}                               /* nfs4_op_lookup_pseudo */

/**
 * nfs4_op_lookupp_pseudo: looks up into the pseudo fs for the parent directory
 * 
 * looks up into the pseudo fs for the parent directory of the current file handle. 
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */

/* Shorter notation to avoid typos */
#define arg_LOOKUPP4 op->nfs_argop4_u.oplookupp
#define res_LOOKUPP4 resp->nfs_resop4_u.oplookupp

int nfs4_op_lookupp_pseudo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp)
{
  pseudofs_entry_t psfsentry;

  resp->resop = NFS4_OP_LOOKUPP;

  /* Get the pseudo fs entry related to the file handle */
  if(!nfs4_FhandleToPseudo(&(data->currentFH), data->pseudofs, &psfsentry))
    {
      res_LOOKUPP4.status = NFS4ERR_BADHANDLE;
      return res_LOOKUPP4.status;
    }

  /* lookupp on the root on the pseudofs should return NFS4ERR_NOENT (RFC3530, page 166) */
  if(!memcmp(&psfsentry, data->pseudofs->reverse_tab[0], sizeof(psfsentry)))
    {
      res_LOOKUPP4.status = NFS4ERR_NOENT;
      return res_LOOKUPP4.status;
    }

  /* A matching entry was found */
  if(!nfs4_PseudoToFhandle(&(data->currentFH), psfsentry.parent))
    {
      res_LOOKUPP4.status = NFS4ERR_SERVERFAULT;
      return res_LOOKUPP4.status;
    }

  /* Copy this to the mounted on FH (if no junction is traversed */
  memcpy((char *)(data->mounted_on_FH.nfs_fh4_val),
         (char *)(data->currentFH.nfs_fh4_val), data->currentFH.nfs_fh4_len);
  data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

  /* Keep the vnode pointer within the data compound */
  if (data->current_entry) {
      cache_inode_put(data->current_entry);
  }
  data->current_entry = NULL;
/* pseudo file system is always a directory and we need to keep
 * nfs4_sanity_check_FH  happy.
 */
  data->current_filetype = DIRECTORY;
  res_LOOKUPP4.status = NFS4_OK;
  return NFS4_OK;
}                               /* nfs4_op_lookupp_pseudo */

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

/* shorter notation to avoid typo */
#define arg_READDIR4 op->nfs_argop4_u.opreaddir
#define res_READDIR4 resp->nfs_resop4_u.opreaddir

static const struct bitmap4 RdAttrErrorBitmap = {
	.bitmap4_len = 1,
	.map = {[0] = (1<<FATTR4_RDATTR_ERROR),
		[1] = 0,
		[2] = 0}
};
static attrlist4 RdAttrErrorVals = { 0, NULL };      /* Nothing to be seen here */

int nfs4_op_readdir_pseudo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp)
{
  unsigned long dircount = 0;
  unsigned long maxcount = 0;
  unsigned long estimated_num_entries = 0;
  unsigned long i = 0;
  nfs_cookie4 cookie;
  verifier4 cookie_verifier;
  unsigned long space_used = 0;
  pseudofs_entry_t psfsentry;
  pseudofs_entry_t *iter = NULL;
  entry4 *entry_nfs_array = NULL;
  exportlist_t *save_pexport;
  nfs_fh4 entryFH;
  cache_inode_fsal_data_t fsdata;
  struct fsal_export *exp_hdl;
  struct fsal_obj_handle *fsal_handle;
  fsal_status_t fsal_status;
  int error = 0;
  size_t namelen = 0;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  cache_entry_t *pentry = NULL;

  resp->resop = NFS4_OP_READDIR;
  res_READDIR4.status = NFS4_OK;

  entryFH.nfs_fh4_len = 0;

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO, "Entering NFS4_OP_READDIR_PSEUDO");

  /* get the caracteristic value for readdir operation */
  dircount = arg_READDIR4.dircount;
  maxcount = arg_READDIR4.maxcount;
  cookie = arg_READDIR4.cookie;
  space_used = sizeof(entry4);

  /* dircount is considered meaningless by many nfsv4 client (like the CITI one). we use maxcount instead */
  estimated_num_entries = maxcount / sizeof(entry4);

  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "PSEUDOFS READDIR: dircount=%lu, maxcount=%lu, cookie=%"PRIu64", sizeof(entry4)=%lu num_entries=%lu",
               dircount, maxcount, (uint64_t)cookie, space_used, estimated_num_entries);

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if(maxcount < sizeof(entry4) || estimated_num_entries == 0)
    {
      res_READDIR4.status = NFS4ERR_TOOSMALL;
      return res_READDIR4.status;
    }

  /* Now resolve the file handle to pseudo fs */
  if(!nfs4_FhandleToPseudo(&(data->currentFH), data->pseudofs, &psfsentry))
    {
      res_READDIR4.status = NFS4ERR_BADHANDLE;
      return res_READDIR4.status;
    }
  LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
               "PSEUDOFS READDIR in #%s#", psfsentry.name);

  /* If this a junction filehandle ? */
  if(psfsentry.junction_export != NULL)
    {
      /* This is a junction */
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDOFS READDIR : DIR #%s# id=%u is a junction",
                   psfsentry.name, psfsentry.junction_export->id);

      /* Step up the compound data */
      data->pexport = psfsentry.junction_export;
      strncpy(data->MntPath, psfsentry.fullname, NFS_MAXPATHLEN);

      /* Build the credentials */
      if(nfs4_MakeCred(data) != 0)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to get FSAL credentials for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          res_READDIR4.status = NFS4ERR_WRONGSEC;
          return res_READDIR4.status;
        }

      /* Lookup the FSAL to build the fsal handle */
      exp_hdl = data->pexport->export_hdl;
      fsal_status = exp_hdl->ops->lookup_path(exp_hdl, data->req_ctx,
					      data->pexport->fullpath,
					      &fsal_handle);
      if(FSAL_IS_ERROR(fsal_status))
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to lookup for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: fsal_status = ( %d, %d )",
                   fsal_status.major, fsal_status.minor);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      if(data->mounted_on_FH.nfs_fh4_len == 0)
        {
          if((error = nfs4_AllocateFH(&(data->mounted_on_FH))) != NFS4_OK)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to allocate the 'mounted on' file handle");
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              return res_READDIR4.status;
            }
        }

      if(data->currentFH.nfs_fh4_len == 0)
        {
          if((error = nfs4_AllocateFH(&(data->currentFH))) != NFS4_OK)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                       "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to allocate the first file handle");
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              return res_READDIR4.status;
            }
        }

      /* Build the nfs4 handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, fsal_handle, data))
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to build the first file handle");
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      /* The new fh is to be the "mounted on Filehandle" */
      memcpy(data->mounted_on_FH.nfs_fh4_val, data->currentFH.nfs_fh4_val,
             sizeof(file_handle_v4_t));
      data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

      /* Add the entry to the cache as a root (BUGAZOMEU: make it a junction entry when junction is available) */
      fsal_handle->ops->handle_to_key(fsal_handle, &fsdata.fh_desc);

      if((pentry = cache_inode_make_root(fsal_handle,
                                         &cache_status)) == NULL)
        {
          LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Allocate root entry in cache inode failed, for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      /* Keep the pentry within the compound data */
      if (data->current_entry) {
          cache_inode_put(data->current_entry);
      }
      data->current_entry = pentry;
      data->current_filetype = pentry->type;

      /* redo the call on the other side of the junction */
      return nfs4_op_readdir(op, data, resp);
    }

  /* Allocation of the entries array */
  if((entry_nfs_array =
      gsh_calloc(estimated_num_entries, sizeof(entry4))) == NULL)
    {
      LogError(COMPONENT_NFS_V4_PSEUDO, ERR_SYS, ERR_MALLOC, errno);
      res_READDIR4.status = NFS4ERR_SERVERFAULT;
      return res_READDIR4.status;
    }

  /* Cookie verifier has the value of the Server Boot Time for pseudo fs */
  memset(cookie_verifier, 0, NFS4_VERIFIER_SIZE);
#ifdef _WITH_COOKIE_VERIFIER
  /* BUGAZOMEU: management of the cookie verifier */
  if(NFS_SpecificConfig.UseCookieVerf == 1)
    {
      memcpy(cookie_verifier, &ServerBootTime, sizeof(ServerBootTime));
      if(cookie != 0)
        {
          if(memcmp(cookie_verifier, arg_READDIR4.cookieverf, NFS4_VERIFIER_SIZE) != 0)
            {
              res_READDIR4.status = NFS4ERR_BAD_COOKIE;
              gsh_free(entry_nfs_array);
              return res_READDIR4.status;
            }
        }
    }
#endif
  /* Cookie delivered by the server and used by the client SHOULD not ne 0, 1 or 2 (cf RFC3530, page192)
   * because theses value are reserved for special use.
   *      0 - cookie for first READDIR
   *      1 - reserved for . on client handside
   *      2 - reserved for .. on client handside
   * Entries '.' and '..' are not returned also
   * For these reason, there will be an offset of 3 between NFS4 cookie and HPSS cookie */

  /* make sure to start at the right position given by the cookie */
  iter = psfsentry.sons;
  if(cookie != 0)
    {
      for(; iter != NULL; iter = iter->next)
        if((iter->pseudo_id + 3) == cookie)
          break;
    }

  /* Here, where are sure that iter is set to the position indicated eventually by the cookie */
  i = 0;
  for(; iter != NULL; iter = iter->next)
    {
      LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS: Found entry %s", iter->name);

      namelen = strlen(iter->name);
      entry_nfs_array[i].name.utf8string_len = namelen;
      if ((entry_nfs_array[i].name.utf8string_val = gsh_malloc(namelen + 1)) == NULL) 
        {
            LogError(COMPONENT_NFS_V4_PSEUDO, ERR_SYS, ERR_MALLOC, errno);
            res_READDIR4.status = NFS4ERR_SERVERFAULT;
            return res_READDIR4.status;
        }
      strncpy(entry_nfs_array[i].name.utf8string_val, iter->name, namelen);
      entry_nfs_array[i].name.utf8string_val[namelen] = '\0';

      entry_nfs_array[i].cookie = iter->pseudo_id + 3;

      /* This used to be in an if with a bogus check for FATTR4_FILEHANDLE. Such
       * a common case, elected to set up FH for call to xxxx_ToFattr
       * unconditionally.
       */ 
      if(entryFH.nfs_fh4_len == 0)
        {
          if(nfs4_AllocateFH(&entryFH) != NFS4_OK)
            {
              return res_READDIR4.status;
            }
        }
      /* Do the case where we stay within the pseudo file system. */
      if(iter->junction_export == NULL) {

          if(!nfs4_PseudoToFhandle(&entryFH, iter))
            {
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              gsh_free(entry_nfs_array);
              return res_READDIR4.status;
            }

          if(nfs4_PseudoToFattr(iter,
                            &(entry_nfs_array[i].attrs),
                            data, &entryFH, &(arg_READDIR4.attr_request)) != 0)
            {
              /* Should never occured, but the is no reason for leaving the section without any information */
              entry_nfs_array[i].attrs.attrmask = RdAttrErrorBitmap;
              entry_nfs_array[i].attrs.attr_vals = RdAttrErrorVals;
            }
      } else {
      /* This is a junction. Code used to not recognize this which resulted
       * in readdir giving different attributes ( including FH, FSid, etc... )
       * to clients from a lookup. AIX refused to list the directory because of
       * this. Now we go to the junction to get the attributes.
       */
          LogFullDebug(COMPONENT_NFS_V4_PSEUDO,
                 "PSEUDOFS READDIR : Offspring DIR #%s# id=%u is a junction full path %s ", 
                  iter->name, iter->junction_export->id, iter->junction_export->fullpath); 
          /* Save the compound data context */
          save_pexport = data->pexport;
          data->pexport = iter->junction_export;
          /* Build the credentials */
          /* XXX Is this really necessary for doing a lookup and 
           * getting attributes?
           * The logic is borrowed from the process invoked above in this code
           * when the target directory is a junction.
           */ 
          if(nfs4_MakeCred(data) != 0)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to get FSAL credentials for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
              res_READDIR4.status = NFS4ERR_WRONGSEC;
              return res_READDIR4.status;
            }
          /* Do the look up. */
	  exp_hdl = data->pexport->export_hdl;
	  fsal_status = exp_hdl->ops->lookup_path(exp_hdl, data->req_ctx,
						  iter->junction_export->fullpath,
						  &fsal_handle);
	  if(FSAL_IS_ERROR(fsal_status))
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to lookup for %s , id=%d",
                   data->pexport->fullpath, data->pexport->id);
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: fsal_status = ( %d, %d )",
                   fsal_status.major, fsal_status.minor);
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              return res_READDIR4.status;
            }
          /* Build the nfs4 handle. Again, we do this unconditionally. */
          if(!nfs4_FSALToFhandle(&entryFH, fsal_handle, data))
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Failed to build the first file handle");
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              return res_READDIR4.status;
            }
          /* Add the entry to the cache as a root. There has to be a better way. */
          fsal_handle->ops->handle_to_key(fsal_handle, &fsdata.fh_desc);
          if((pentry = cache_inode_make_root(fsal_handle,
                                             &cache_status)) == NULL)
            {
              LogMajor(COMPONENT_NFS_V4_PSEUDO,
                   "PSEUDO FS JUNCTION TRAVERSAL: /!\\ | Allocate root entry in cache inode failed, for %s, id=%d",
                   data->pexport->fullpath, data->pexport->id);
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              return res_READDIR4.status;
            }
          if(cache_entry_To_Fattr(pentry,
                                  &(entry_nfs_array[i].attrs),
                                  data, &entryFH, &(arg_READDIR4.attr_request)) != 0)
            {
              /* Return the fattr4_rdattr_error , cf RFC3530, page 192 */
              entry_nfs_array[i].attrs.attrmask = RdAttrErrorBitmap;
              entry_nfs_array[i].attrs.attr_vals = RdAttrErrorVals;
            }
           data->pexport = save_pexport;
      }
      /* Chain the entry together */
      entry_nfs_array[i].nextentry = NULL;
      if(i != 0)
        entry_nfs_array[i - 1].nextentry = &(entry_nfs_array[i]);

      /* Increment the counter */
      i += 1;

      /* Did we reach the maximum number of entries */
      if(i == estimated_num_entries)
        break;
    }

  /* Resize entry_nfs_array */
  /* @todo : Is this reallocation actually needed ? */
#ifdef BUGAZOMEU
  if(i < estimated_num_entries)
    if((entry_nfs_array = gsh_realloc(entry_nfs_array, i *
                                      sizeof(entry4))) == NULL)
      {
        LogError(COMPONENT_NFS_V4_PSEUDO, ERR_SYS, ERR_MALLOC, errno);
        res_READDIR4.status = NFS4ERR_SERVERFAULT;
        gsh_free(entry_nfs_array);
        return res_READDIR4.status;
      }
#endif
  /* Build the reply */
  memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
         NFS4_VERIFIER_SIZE);
  if(i == 0)
    res_READDIR4.READDIR4res_u.resok4.reply.entries = NULL;
  else
    res_READDIR4.READDIR4res_u.resok4.reply.entries = entry_nfs_array;

  /* did we reach the end ? */
  if(iter == NULL)
    {
      /* Yes, we did */
      res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE;
    }
  else
    {
      /* No, there are some more entries */
      res_READDIR4.READDIR4res_u.resok4.reply.eof = FALSE;
    }

  /* Exit properly */
  res_READDIR4.status = NFS4_OK;

  return NFS4_OK;
}                               /* nfs4_op_readdir_pseudo */
