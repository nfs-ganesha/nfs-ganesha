/*
 * \brief Manage a namespace for path<->inode association
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "namespace.h"
#include "stuff_alloc.h"
#include "RW_Lock.h"
#include "HashTable.h"

#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

#ifndef P
#define P(_m_)    pthread_mutex_lock(&(_m_))
#endif

#ifndef V
#define V(_m_)    pthread_mutex_unlock(&(_m_))
#endif

/*-----------------------------------------------------
 *              Type definitions
 *-----------------------------------------------------*/

/* We provide 2 associations:
 * Lookup: (parent,name) -> fsnode
 * Path:   inode->fsnode with list of (parent,name)
 */

typedef struct __inode__
{
  ino_t inum;
  dev_t dev;
  unsigned int generation;
} inode_t;

/* - key for lookup hash table
 * - it is also used to keep a list of parent/name of an entry
 */
typedef struct __lookup_peer__
{
  inode_t parent;
  char name[FSAL_MAX_NAME_LEN];

  /* used for pool chaining into parent list and pool allocation */
  struct __lookup_peer__ *p_next;

} lookup_peer_t;

/* - data for lookup hash table is a fsnode ;
 * - key for path hash table is an inode_t ;
 * - data for path hash table is a fsnode.
 */

typedef struct __fsnode__
{
  inode_t inode;
  unsigned int n_lookup;       /**< number of times the entry is involved
                                    in a lookup operation as a child:
                                    i.e. hardlink count */
  unsigned int n_children;       /**< number of times the entry is involved
                                      in a lookup operation as a parent:
                                      i.e. child count */
  lookup_peer_t *parent_list;

  /* used for pool allocation */
  struct __fsnode__ *p_next;

} fsnode_t;

/*-----------------------------------------------------
 *                 Memory management
 *-----------------------------------------------------*/

/* pool of preallocated nodes */
/* TODO: externalize pool size parameter */

#define POOL_CHUNK_SIZE   1024
static struct prealloc_pool node_pool;
static pthread_mutex_t node_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

static fsnode_t *node_alloc()
{
  fsnode_t *p_new;

  P(node_pool_mutex);
  GetFromPool(p_new, &node_pool, fsnode_t);
  V(node_pool_mutex);

  memset(p_new, 0, sizeof(fsnode_t));

  return p_new;
}

static void node_free(fsnode_t * p_node)
{
  memset(p_node, 0, sizeof(fsnode_t));

  P(node_pool_mutex);
  ReleaseToPool(p_node, &node_pool);
  V(node_pool_mutex);
}

/* pool of preallocated lookup peers */

static struct prealloc_pool peer_pool;
static pthread_mutex_t peer_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

static lookup_peer_t *peer_alloc()
{
  lookup_peer_t *p_new;

  P(peer_pool_mutex);
  GetFromPool(p_new, &peer_pool, lookup_peer_t);
  V(peer_pool_mutex);

  memset(p_new, 0, sizeof(lookup_peer_t));

  return p_new;
}

static void peer_free(lookup_peer_t * p_peer)
{
  memset(p_peer, 0, sizeof(lookup_peer_t));

  P(peer_pool_mutex);
  ReleaseToPool(p_peer, &peer_pool);
  V(peer_pool_mutex);
}

/*----------------------------------------------------
 *                  hash tables
 *----------------------------------------------------*/

/* functions for hashing/comparing lookup peers */

static unsigned long hash_peer_idx(hash_parameter_t * p_conf, hash_buffer_t * p_key);
static unsigned long hash_peer_rbt(hash_parameter_t * p_conf, hash_buffer_t * p_key);
static int cmp_peers(hash_buffer_t * p_key1, hash_buffer_t * p_key2);

/* functions for hashing/comparing inode numbers */

static unsigned long hash_ino_idx(hash_parameter_t * p_conf, hash_buffer_t * p_key);
static unsigned long hash_ino_rbt(hash_parameter_t * p_conf, hash_buffer_t * p_key);
static int cmp_inodes(hash_buffer_t * p_key1, hash_buffer_t * p_key2);

/* display functions */

static int print_lookup_peer(hash_buffer_t * p_val, char *outbuff);
static int print_inode(hash_buffer_t * p_val, char *outbuff);
static int print_fsnode(hash_buffer_t * p_val, char *outbuff);

/* configuration for lookup hashtable
 * TODO: externize those parameters
 */

static hash_parameter_t lookup_hash_config = {
  .index_size = 877,
  .alphabet_length = 26,
  .nb_node_prealloc = POOL_CHUNK_SIZE,
  .hash_func_key = hash_peer_idx,
  .hash_func_rbt = hash_peer_rbt,
  .compare_key = cmp_peers,
  .key_to_str = print_lookup_peer,
  .val_to_str = print_fsnode,
};

/* configuration for inode->fsnode hashtable
 * TODO: externize those parameters
 */
static hash_parameter_t nodes_hash_config = {
  .index_size = 877,
  .alphabet_length = 10,
  .nb_node_prealloc = POOL_CHUNK_SIZE,
  .hash_func_key = hash_ino_idx,
  .hash_func_rbt = hash_ino_rbt,
  .compare_key = cmp_inodes,
  .key_to_str = print_inode,
  .val_to_str = print_fsnode,
};

/* namespace structures */

static rw_lock_t ns_lock = { 0 };

static hash_table_t *lookup_hash = NULL;
static hash_table_t *nodes_hash = NULL;

/*----------------------------------------------------
 *         hash tables functions implementation
 *----------------------------------------------------*/

/* functions for hashing/comparing lookup peers */

static unsigned long hash_peer_idx(hash_parameter_t * p_conf, hash_buffer_t * p_key)
{
  unsigned int i;
  unsigned long hash;
  lookup_peer_t *p_peer = (lookup_peer_t *) p_key->pdata;
  char *name;

  hash = 1;

  for(name = p_peer->name; *name != '\0'; name++)
    hash =
        ((hash * p_conf->alphabet_length) + (unsigned long)(*name)) % p_conf->index_size;

  hash = (hash + (unsigned long)p_peer->parent.inum) % p_conf->index_size;
  hash = (hash ^ (unsigned long)p_peer->parent.dev) % p_conf->index_size;

  return hash;
}

static unsigned long hash_peer_rbt(hash_parameter_t * p_conf, hash_buffer_t * p_key)
{
  unsigned int i;
  unsigned long hash;
  lookup_peer_t *p_peer = (lookup_peer_t *) p_key->pdata;
  char *name;

  hash = 1;

  for(name = p_peer->name; *name != '\0'; name++)
    hash = ((hash << 5) - hash + (unsigned long)(*name));

  return (hash ^ (unsigned long)p_peer->parent.inum ^ (unsigned long)p_peer->parent.dev);
}

static int cmp_peers(hash_buffer_t * p_key1, hash_buffer_t * p_key2)
{
  lookup_peer_t *p_peer1 = (lookup_peer_t *) p_key1->pdata;
  lookup_peer_t *p_peer2 = (lookup_peer_t *) p_key2->pdata;

  /* compare parent inode, then name */

  if((p_peer1->parent.inum > p_peer2->parent.inum)
     || (p_peer1->parent.dev > p_peer2->parent.dev))
    return 1;
  else if((p_peer1->parent.inum < p_peer2->parent.inum)
          || (p_peer1->parent.dev < p_peer2->parent.dev))
    return -1;
  else                          /* same parent */
    return strncmp(p_peer1->name, p_peer2->name, FSAL_MAX_NAME_LEN);

}

/* functions for hashing/comparing inode numbers */

static unsigned long hash_ino_idx(hash_parameter_t * p_conf, hash_buffer_t * p_key)
{
  unsigned long hash;
  inode_t *p_ino = (inode_t *) p_key->pdata;

  hash =
      (p_conf->alphabet_length + ((unsigned long)p_ino->inum ^ (unsigned int)p_ino->dev));
  return (3 * hash + 1999) % p_conf->index_size;

}

static unsigned long hash_ino_rbt(hash_parameter_t * p_conf, hash_buffer_t * p_key)
{
  inode_t *p_ino = (inode_t *) p_key->pdata;

  return (unsigned long)(p_ino->inum ^ (3 * (p_ino->dev + 1)));
}

static int cmp_inodes(hash_buffer_t * p_key1, hash_buffer_t * p_key2)
{
  inode_t *p_ino1 = (inode_t *) p_key1->pdata;
  inode_t *p_ino2 = (inode_t *) p_key2->pdata;

  if((p_ino1->inum > p_ino2->inum) || (p_ino1->dev > p_ino2->dev))
    return 1;
  else if((p_ino1->inum < p_ino2->inum) || (p_ino1->dev < p_ino2->dev))
    return -1;
  else
    return 0;
}

/* display functions */

static int print_lookup_peer(hash_buffer_t * p_val, char *outbuff)
{
  lookup_peer_t *p_peer = (lookup_peer_t *) p_val->pdata;
  return sprintf(outbuff, "parent:%lX.%lu, name:%s",
                 (unsigned long int)p_peer->parent.dev,
                 (unsigned long int)p_peer->parent.inum, p_peer->name);
}

static int print_inode(hash_buffer_t * p_val, char *outbuff)
{
  inode_t *p_ino = (inode_t *) p_val->pdata;
  return sprintf(outbuff, "device:%lX inode:%lu (gen:%u)",
                 (unsigned long int)p_ino->dev, (unsigned long int)p_ino->inum,
                 p_ino->generation);
}

static int print_fsnode(hash_buffer_t * p_val, char *outbuff)
{
  fsnode_t *p_node = (fsnode_t *) p_val->pdata;

  if(p_node->parent_list)
    return sprintf(outbuff,
                   "device:%lX inode:%lu (gen:%u), linkcount:%u, children:%u, first_parent:%lX.%lu, name=%s",
                   (unsigned long int)p_node->inode.dev,
                   (unsigned long int)p_node->inode.inum, p_node->inode.generation,
                   p_node->n_lookup, p_node->n_children,
                   (unsigned long int)p_node->parent_list->parent.dev,
                   (unsigned long int)p_node->parent_list->parent.inum,
                   p_node->parent_list->name);
  else
    return sprintf(outbuff,
                   "device:%lX inode:%lu (gen:%u), linkcount:%u, children:%u (no parent)",
                   (unsigned long int)p_node->inode.dev,
                   (unsigned long int)p_node->inode.inum, p_node->inode.generation,
                   p_node->n_lookup, p_node->n_children);

}

/*----------------------------------------------------
 *              hash tables helpers
 *----------------------------------------------------*/
lookup_peer_t *h_insert_new_lookup(ino_t parent_inode,
                                   dev_t parent_dev,
                                   unsigned int parent_gen,
                                   char *name, fsnode_t * p_entry, int overwrite)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  lookup_peer_t *p_lpeer;
  int flag;

  /* alloc new peer */
  p_lpeer = peer_alloc();
  if(!p_lpeer)
    return NULL;

  p_lpeer->parent.inum = parent_inode;
  p_lpeer->parent.dev = parent_dev;
  p_lpeer->parent.generation = parent_gen;
  strncpy(p_lpeer->name, name, FSAL_MAX_NAME_LEN);

  buffkey.pdata = (caddr_t) p_lpeer;
  buffkey.len = sizeof(*p_lpeer);

  buffval.pdata = (caddr_t) p_entry;
  buffval.len = sizeof(*p_entry);

  if(overwrite)
    flag = HASHTABLE_SET_HOW_SET_OVERWRITE;
  else
    flag = HASHTABLE_SET_HOW_SET_NO_OVERWRITE;

  if(HashTable_Test_And_Set(lookup_hash, &buffkey, &buffval, flag) != HASHTABLE_SUCCESS)
    {
      peer_free(p_lpeer);
      return NULL;
    }

  /* Add lookup to node and increase n_lookup count */
  p_entry->n_lookup++;
  p_lpeer->p_next = p_entry->parent_list;
  p_entry->parent_list = p_lpeer;

  return p_lpeer;

}                               /* h_insert_new_lookup */

/* get the node pointed by a lookup peer (if it exists) */
fsnode_t *h_get_lookup(ino_t parent_inode, dev_t parent_dev, char *name, int *p_rc)
{
  lookup_peer_t lpeer;
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int rc;

  /* test if lookup peer is referenced in lookup hashtable */
  lpeer.parent.inum = parent_inode;
  lpeer.parent.dev = parent_dev;

  strncpy(lpeer.name, name, FSAL_MAX_NAME_LEN);

  buffkey.pdata = (caddr_t) & lpeer;
  buffkey.len = sizeof(lpeer);

  rc = HashTable_Get(lookup_hash, &buffkey, &buffval);

  if(p_rc)
    *p_rc = rc;

  if(rc == HASHTABLE_SUCCESS)
    return (fsnode_t *) (buffval.pdata);
  else
    return NULL;
}                               /* h_get_lookup */

/* remove a lookup entry and return the pointed node */
fsnode_t *h_del_lookup(ino_t parent_inode, dev_t parent_dev, unsigned int parent_gen,
                       char *name, int *p_rc)
{
  lookup_peer_t lpeer;
  lookup_peer_t *p_lpeer;
  lookup_peer_t *p_lpeer_last;
  hash_buffer_t buffkey_in, buffkey_out;
  hash_buffer_t buffdata;
  fsnode_t *p_node;
  int rc;

  /* test if lookup peer is referenced in lookup hashtable */
  lpeer.parent.inum = parent_inode;
  lpeer.parent.dev = parent_dev;
  /* note: generation not needed for Hashtable_Get/Del */

  strncpy(lpeer.name, name, FSAL_MAX_NAME_LEN);

  buffkey_in.pdata = (caddr_t) & lpeer;
  buffkey_in.len = sizeof(lpeer);

  rc = HashTable_Del(lookup_hash, &buffkey_in, &buffkey_out, &buffdata);

  if(p_rc)
    *p_rc = rc;

  if(rc == HASHTABLE_SUCCESS)
    {
      /* Remove lookup from node and decrease n_lookup count */
      p_node = (fsnode_t *) buffdata.pdata;

      assert(p_node->n_lookup > 0);
      p_node->n_lookup--;

      /* find lpeer in node */
      p_lpeer_last = NULL;

      for(p_lpeer = p_node->parent_list; p_lpeer != NULL; p_lpeer = p_lpeer->p_next)
        {
          if(p_lpeer == (lookup_peer_t *) buffkey_out.pdata)
            {
              /* check parent inode and entry name */
              if((p_lpeer->parent.inum != parent_inode)
                 || (p_lpeer->parent.dev != parent_dev)
                 || (p_lpeer->parent.generation != parent_gen)
                 || strncmp(p_lpeer->name, name, FSAL_MAX_NAME_LEN))
                {
                  LogCrit(COMPONENT_FSAL,
                          "NAMESPACE MANAGER: An incompatible direntry was found. In node: %lu.%lu (gen:%u) ,%s  Deleted:%lu.%lu (gen:%u),%s",
                          p_lpeer->parent.inum, p_lpeer->parent.dev,
                          p_lpeer->parent.generation, p_lpeer->name, parent_dev,
                          parent_inode, parent_gen, name);
                  /* remove it anyway... */
                }

              /* remove it from list */

              if(p_lpeer_last)
                p_lpeer_last->p_next = p_lpeer->p_next;
              else
                p_node->parent_list = p_lpeer->p_next;

              /* free it */

              peer_free(p_lpeer);

              /* finished */
              break;

            }
          /* if lpeer found */
          p_lpeer_last = p_lpeer;
        }

      /* return the pointer node */
      return p_node;
    }
  else
    return NULL;

}                               /* h_del_lookup */

fsnode_t *h_insert_new_node(ino_t inode, dev_t device, unsigned int gen, int overwrite)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  fsnode_t *p_node;
  int flag;

  p_node = node_alloc();
  if(!p_node)
    return NULL;

  p_node->inode.inum = inode;
  p_node->inode.dev = device;

  p_node->inode.generation = gen;

  /* no children, no hardlink for the moment */
  p_node->n_lookup = 0;
  p_node->n_children = 0;
  p_node->parent_list = NULL;

  /* insert it to hash table */

  buffkey.pdata = (caddr_t) & p_node->inode;
  buffkey.len = sizeof(p_node->inode);

  buffval.pdata = (caddr_t) p_node;
  buffval.len = sizeof(*p_node);

  if(overwrite)
    flag = HASHTABLE_SET_HOW_SET_OVERWRITE;
  else
    flag = HASHTABLE_SET_HOW_SET_NO_OVERWRITE;

  if(HashTable_Test_And_Set(nodes_hash, &buffkey, &buffval, flag) != HASHTABLE_SUCCESS)
    {
      node_free(p_node);
      return NULL;
    }

  return p_node;

}                               /* h_insert_new_node */

/* get the node corresponding to an inode number */
fsnode_t *h_get_node(ino_t inode, dev_t device, int *p_rc)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  inode_t key;
  int rc;

  /* test if inode is referenced in nodes hashtable */

  key.inum = inode;
  key.dev = device;
  /* note: generation not needed for Hashtable_Get */

  buffkey.pdata = (caddr_t) & key;
  buffkey.len = sizeof(key);

  rc = HashTable_Get(nodes_hash, &buffkey, &buffval);

  if(p_rc)
    *p_rc = rc;

  if(rc == HASHTABLE_SUCCESS)
    return (fsnode_t *) (buffval.pdata);
  else
    return NULL;
}                               /* h_get_node */

/* remove the node corresponding to an inode number */
int h_del_node(ino_t inode, dev_t device)
{
  hash_buffer_t buffkey;
  inode_t key;
  int rc;

  /* test if inode is referenced in nodes hashtable */
  key.inum = inode;
  key.dev = device;

  /* note: generation not needed for Hashtable_Get/Del */

  buffkey.pdata = (caddr_t) & key;
  buffkey.len = sizeof(key);

  return HashTable_Del(nodes_hash, &buffkey, NULL, NULL);

}                               /* h_del_node */

/*----------------------------------------------------
 *              Exported functions
 *----------------------------------------------------*/

/* Initialize namespace and create root with the given inode number */
int NamespaceInit(ino_t root_inode, dev_t root_dev, unsigned int *p_root_gen)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  fsnode_t *root;

  /* Initialize pools.
   */

  MakePool(&peer_pool, POOL_CHUNK_SIZE, lookup_peer_t, NULL, NULL);

  MakePool(&node_pool, POOL_CHUNK_SIZE, fsnode_t, NULL, NULL);

  /* initialize namespace lock */
  if(rw_lock_init(&ns_lock))
    return ENOMEM;

  /* init the lookup hash table */
  lookup_hash = HashTable_Init(lookup_hash_config);
  nodes_hash = HashTable_Init(nodes_hash_config);

  if(!lookup_hash || !nodes_hash)
    return ENOMEM;

  /* allocate the root entry */
  root = h_insert_new_node(root_inode, root_dev, *p_root_gen, TRUE);

  if(!root)
    return ENOMEM;

  LogFullDebug(COMPONENT_FSAL, "namespace: Root=%lX.%ld (gen:%u)", root_dev, root_inode,
         root->inode.generation);

  *p_root_gen = root->inode.generation;

  /* never remove it */
  root->n_lookup = 1;

  return 0;
}

/* Add a child entry (no lock) */
static int NamespaceAdd_nl(ino_t parent_ino, dev_t parent_dev, unsigned int parent_gen,
                           char *name,
                           ino_t entry_ino, dev_t entry_dev, unsigned int *p_new_gen)
{
  fsnode_t *p_parent = NULL;
  fsnode_t *p_node = NULL;
  fsnode_t *p_node_exist = NULL;
  lookup_peer_t lpeer;
  lookup_peer_t *p_lpeer;
  int rc;

  LogFullDebug(COMPONENT_FSAL, "namespace: Adding (%lX.%ld,%s)=%lX.%ld",
         parent_dev, parent_ino, name, entry_dev, entry_ino);

  /* does parent inode exist in namespace ? */

  p_parent = h_get_node(parent_ino, parent_dev, &rc);

  if(!p_parent)
    return ENOENT;
  else if(p_parent->inode.generation != parent_gen)
    return ESTALE;

  /* does another entry exist with this inode ? (hardlink) */

  p_node = h_get_node(entry_ino, entry_dev, &rc);

  switch (rc)
    {
    case HASHTABLE_SUCCESS:
      {
        /* this node exists */

        /* test if it is already referenced in lookup hashtable */

        p_node_exist = h_get_lookup(parent_ino, parent_dev, name, &rc);

        if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
          {

            /* create and add new peer to hash table */
            p_lpeer =
                h_insert_new_lookup(parent_ino, parent_dev, parent_gen, name, p_node,
                                    FALSE);
            if(!p_lpeer)
              return EFAULT;

            /* increment parent's refcount */
            p_parent->n_children++;

          }
        else if(rc == HASHTABLE_SUCCESS)
          {

            if((p_node_exist->inode.inum == entry_ino)
               && (p_node_exist->inode.dev == entry_dev))
              {
                /* entry already exist: nothing to do, return last generation */
                *p_new_gen = p_node_exist->inode.generation;
                return 0;
              }
            else
              {
                /* an incompatible entry was found ! */

                /* TODO: REMOVE IT silently because the filesystem changed behind us */

                LogCrit(COMPONENT_FSAL,
                        "NAMESPACE MANAGER: An incompatible direntry was found. Existing: %lX.%lu,%s->%lX.%lu  New:%lX.%lu,%s->%lX.%lu",
                        parent_dev, parent_ino, name, p_node_exist->inode.dev,
                        p_node_exist->inode.inum, parent_dev, parent_ino, name, entry_dev, entry_ino);
                return EEXIST;
              }
          }
        else                    /* other error */
          {
            return EFAULT;
          }

        break;
      }

    case HASHTABLE_ERROR_NO_SUCH_KEY:
      {

        /* allocate new node entry */
        p_node = h_insert_new_node(entry_ino, entry_dev, *p_new_gen, FALSE);

        if(!p_node)
          return ENOMEM;

        /* now, create the associated lookup peer */

        p_lpeer =
            h_insert_new_lookup(parent_ino, parent_dev, parent_gen, name, p_node, FALSE);

        if(!p_lpeer)
          return ENOMEM;

        /* increase parent's refcount */
        p_parent->n_children++;

        break;
      }

    default:
      return EFAULT;            /* should not occur ! */
    }

  LogFullDebug(COMPONENT_FSAL, "namespace: Entry %lX.%ld (gen:%u)  has now link count = %u",
         p_node->inode.dev, p_node->inode.inum, p_node->inode.generation,
         p_node->n_lookup);

  *p_new_gen = p_node->inode.generation;

  /* added successfuly */
  return 0;
}                               /* NamespaceAdd_nl */

/* Remove a child entry (no lock) */
static int NamespaceRemove_nl(ino_t parent_ino, dev_t parent_dev, unsigned int parent_gen,
                              char *name)
{
  fsnode_t *p_parent = NULL;
  fsnode_t *p_node = NULL;
  int rc;

  LogFullDebug(COMPONENT_FSAL, "namespace: removing %lX.%ld/%s", parent_dev, parent_ino, name);

  /* get parent node */
  p_parent = h_get_node(parent_ino, parent_dev, &rc);

  if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
    return ENOENT;
  else if(!p_parent)
    return EFAULT;
  else if(p_parent->inode.generation != parent_gen)
    return ESTALE;

  /* remove the lookup entry in the hash and get pointed node (if exists) */
  p_node = h_del_lookup(parent_ino, parent_dev, parent_gen, name, &rc);

  if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
    {
      /* consider its OK */
      return 0;
    }
  else if(!p_node)
    {
      return EFAULT;
    }

  assert(p_parent->n_children > 0);

  /* decrement parents' lookup count */
  p_parent->n_children--;

  LogFullDebug(COMPONENT_FSAL, "namespace: Entry %lX.%ld has now link count = %u",
         p_node->inode.dev, p_node->inode.inum, p_node->n_lookup);

  /* node not in namespace tree anymore */
  if(p_node->n_lookup == 0)
    {
      assert(p_node->n_children == 0);

      /* remove from hash table */
      rc = h_del_node(p_node->inode.inum, p_node->inode.dev);

      if(rc != HASHTABLE_SUCCESS)
        {
          return EFAULT;
        }

      /* free the node */
      node_free(p_node);
    }

  /* remove succeeded ! */
  return 0;
}                               /* NamespaceRemove_nl */

/* Add a child entry */
int NamespaceAdd(ino_t parent_ino, dev_t parent_dev, unsigned int gen,
                 char *name, ino_t entry_ino, dev_t entry_dev, unsigned int *p_new_gen)
{
  int rc;

  /* lock the namespace for modification */
  P_w(&ns_lock);
  rc = NamespaceAdd_nl(parent_ino, parent_dev, gen, name,
                       entry_ino, entry_dev, p_new_gen);
  V_w(&ns_lock);

  return rc;
}

/* Remove a child entry */
int NamespaceRemove(ino_t parent_ino, dev_t parent_dev, unsigned int gen, char *name)
{
  int rc;

  /* lock the namespace for modification */
  P_w(&ns_lock);
  rc = NamespaceRemove_nl(parent_ino, parent_dev, gen, name);
  V_w(&ns_lock);

  return rc;
}

/* Move an entry in the namespace */
int NamespaceRename(ino_t parent_entry_src, dev_t src_dev, unsigned int srcgen,
                    char *name_src, ino_t parent_entry_tgt, dev_t tgt_dev,
                    unsigned int tgtgen, char *name_tgt)
{

  int rc = 0;
  fsnode_t *p_node;
  unsigned int new_gen;

  /* lock the namespace for modification */
  P_w(&ns_lock);

  /* get source node info */
  p_node = h_get_lookup(parent_entry_src, src_dev, name_src, &rc);

  if(!p_node || rc != 0)
    {
      V_w(&ns_lock);
      return rc;
    }

  /* check that source != target (if so, do nothing) */
  if((parent_entry_src == parent_entry_tgt)
     && (src_dev == tgt_dev) && !strcmp(name_src, name_tgt))
    {
      V_w(&ns_lock);
      return 0;
    }

  /* try to add new path (with the same gen number) */
  new_gen = p_node->inode.generation;

  rc = NamespaceAdd_nl(parent_entry_tgt, tgt_dev, tgtgen, name_tgt, p_node->inode.inum,
                       p_node->inode.dev, &new_gen);

  if(rc == EEXIST)
    {
      /* remove previous entry */
      rc = NamespaceRemove_nl(parent_entry_tgt, tgt_dev, tgtgen, name_tgt);
      if(rc)
        {
          V_w(&ns_lock);
          return rc;
        }

      /* add the new one */
      new_gen = p_node->inode.generation;

      rc = NamespaceAdd_nl(parent_entry_tgt, tgt_dev, tgtgen, name_tgt,
                           p_node->inode.inum, p_node->inode.dev, &new_gen);
    }

  if(rc)
    {
      V_w(&ns_lock);
      return rc;
    }

  /* remove old path */
  rc = NamespaceRemove_nl(parent_entry_src, src_dev, srcgen, name_src);

  V_w(&ns_lock);

  return rc;

}

int NamespaceGetGen(ino_t inode, dev_t dev, unsigned int *p_gen)
{
  fsnode_t *p_node;
  int rc;

  /* get entry from hash */
  p_node = h_get_node(inode, dev, &rc);

  LogFullDebug(COMPONENT_FSAL, "NamespaceGetGen(%lX,%ld): p_node = %p, rc = %d", dev, inode, p_node, rc);

  if(!p_node)
    return ENOENT;
  else if(rc != 0)
    return rc;

  *p_gen = p_node->inode.generation;

  return 0;
}

/* Get a possible full path for an entry */
int NamespacePath(ino_t entry, dev_t dev, unsigned int gen, char *path)
{
  fsnode_t *p_node;
  int rc;
  char tmp_path[FSAL_MAX_PATH_LEN];

  inode_t curr_inode;

  /* initialize paths */
  path[0] = '\0';
  tmp_path[0] = '\0';

  /* lock the namespace read-only */
  P_r(&ns_lock);

  curr_inode.inum = entry;
  curr_inode.dev = dev;
  curr_inode.generation = gen;

  do
    {
      /* get entry from hash */
      p_node = h_get_node(curr_inode.inum, curr_inode.dev, &rc);

      if(!p_node)
        {
          V_r(&ns_lock);
          if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
            {
              LogFullDebug(COMPONENT_FSAL, "namespace: %lX.%ld not found", (unsigned long)dev,
                     (unsigned long)entry);
              return ENOENT;
            }
          else
            return EFAULT;
        }
      else if(p_node->inode.generation != curr_inode.generation)
        {
          V_r(&ns_lock);
          return ESTALE;
        }

      if(!p_node->parent_list)
        {
          /* this is the root entry, just add '/' at the begining and return */

          LogFullDebug(COMPONENT_FSAL, "namespace: root entry reached");

          snprintf(path, FSAL_MAX_PATH_LEN, "/%s", tmp_path);
          break;
        }
      else if(path[0] == '\0')
        {
          /* nothing in path for the moment, just copy entry name in it */
          strncpy(path, p_node->parent_list->name, FSAL_MAX_NAME_LEN);
          curr_inode = p_node->parent_list->parent;
        }
      else
        {
          /* this is a parent dir, path is now <dirname>/<subpath> */
          snprintf(path, FSAL_MAX_PATH_LEN, "%s/%s", p_node->parent_list->name, tmp_path);

          LogFullDebug(COMPONENT_FSAL, "lookup peer found: (%lX.%ld,%s)",
                 p_node->parent_list->parent.dev,
                 p_node->parent_list->parent.inum, p_node->parent_list->name);

          /* loop detection */
          if((curr_inode.inum == p_node->parent_list->parent.inum)
             && (curr_inode.dev == p_node->parent_list->parent.dev))
            {
              LogCrit(COMPONENT_FSAL,
                      "NAMESPACE MANAGER: loop detected in namespace: %lX.%ld/%s = %lX.%ld",
                      p_node->parent_list->parent.dev, p_node->parent_list->parent.inum,
                      p_node->parent_list->name, curr_inode.dev, curr_inode.inum);
              V_r(&ns_lock);
              return ELOOP;
            }

          curr_inode = p_node->parent_list->parent;
        }

      /* backup path to tmp_path for next loop */
      strcpy(tmp_path, path);

    }
  while(1);

  LogFullDebug(COMPONENT_FSAL, "inode=%lX.%ld (gen %u), path='%s'", dev, entry, gen, path);

  /* reverse lookup succeeded */
  V_r(&ns_lock);
  return 0;

}
