/*
 * \brief Manage a namespace for path<->inode association
 */

#ifndef _GANEFUSE_NAMESPACE_H
#define _GANEFUSE_NAMESPACE_H

#include "fsal.h"

/* Initialize namespace root
 * p_root_gen in/out:
 *      in: give the ctime of the entry the first time
 *          it was seen in the filesystem.
 *      out: the effective generation number that was given.
 */
int NamespaceInit(ino_t root_inode, dev_t root_dev, unsigned int *p_root_gen);

/* Add a child entry
 * p_new_gen in/out:
 *      in: give the ctime of the entry the first time
 *          it was seen in the filesystem.
 *      out: the effective generation number that was given.
 * \return ENOENT if directoy inode is unknown
 * \return ESTALE if directoy gen number is not correct
 *         
 */
int NamespaceAdd(ino_t parent_ino, dev_t parent_dev, unsigned int gen,
                 char *name, ino_t entry_ino, dev_t entry_dev, unsigned int *p_new_gen);

/* Remove a child entry
 * \return ENOENT if directoy inode is unknown
 * \return ESTALE if directoy gen number is not correct
 */
int NamespaceRemove(ino_t parent_ino, dev_t parent_dev, unsigned int gen, char *name);

/* Move an entry in the namespace
 * \return ENOENT if directoy inode is unknown
 * \return ESTALE if directoy gen number is not correct
 */
int NamespaceRename(ino_t parent_entry_src, dev_t src_dev, unsigned int srcgen,
                    char *name_src, ino_t parent_entry_tgt, dev_t tgt_dev,
                    unsigned int tgtgen, char *name_tgt);

/**
 * Get a possible full path for an entry.
 *
 * \param entry The inode for the entry to be "reverse lookuped"
 * \param path this buffer must be at least of size FSAL_MAX_PATH_LEN
 *
 * \return ENOENT if inode is unknown
 * \return ESTALE if gen number is not correct
 */
int NamespacePath(ino_t entry, dev_t dev, unsigned int gen, char *path);

/**
 * retrieves the current generation number for a inode
 */
int NamespaceGetGen(ino_t inode, dev_t dev, unsigned int *p_gen);

#endif
