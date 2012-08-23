/*
 * Copyright (C) Paul Sheer, 2012
 * Author: Paul Sheer paulsheer@gmail.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */


#include <pthread.h>


#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>



#if defined(__STRICT_ANSI__) && defined(__GNUC__)
int fileno();
int fseeko();
int fsync();
int ftruncate();
int pclose();
int snprintf();
#endif



#define LOCK_ON(x)              pthread_mutex_lock(&(x)->pthread_mutex)
#define LOCK_OFF(x)             pthread_mutex_unlock(&(x)->pthread_mutex)
#define LOCK_INIT(x)            pthread_mutex_init(&(x)->pthread_mutex, NULL)
#define LOCK_FREE(x)            pthread_mutex_destroy(&(x)->pthread_mutex)
#define LOCK_IS_LOCKED(x)       (pthread_mutex_trylock(&(x)->pthread_mutex) ? 1 : pthread_mutex_unlock(&(x)->pthread_mutex))


#define REDBLACK_NOCOLOR        (0)




#define xfclose(x)      do { if ((x)) { fclose(x); (x) = NULL; } } while (0)

#include "redblack.h"
#include "fastdb.h"

#define LOCK_DEF		pthread_mutex_t pthread_mutex
#define FASTDB_MAGIC            0x289d971c
#define FASTDB_ITEM_MAGIC       0x8e8cafa3



struct fastdb_header_ {
    char magic[7];              /* "fastdb\0" */
    char version;
    unsigned char record_size[4];
};

#define FASTDB_VERSION          2

union fastdb_header {
    unsigned char header[1024];
    struct fastdb_header_ h;
};

enum fastdb_phase {
    FASTDB_PHASE_FREE,
    FASTDB_PHASE_USED
};

enum fastdb_flavor {
    FASTDB_FLAVOR_WRITTEN,
    FASTDB_FLAVOR_WRITEPENDING
};


struct fastdb_item;
struct fastdb;

static void fastdb_extend_database (struct fastdb *m, const user_data_t * data, char phase, char flavor);
static void fastdb_insert_free_list (struct fastdb *m);
static void fastdb_relink (struct fastdb *m, struct fastdb_item *p, const user_data_t * data, char phase, char flavor);


struct fastdb_item {
    struct redblack_node redblack_node_phaseoffset;
    struct redblack_node redblack_node_flavoroffset;
    unsigned int magic;
    enum fastdb_phase phase;
    enum fastdb_flavor flavor;
    long long offset;
/* alloc length of data is ((struct fastdb *) x)->record_size */
    user_data_t *data;
/* variable length */
    struct redblack_node redblack_node_userindices[1];
};

struct fastdb_user_index {
    struct redblack_tree redblack_root;
    redblack_cmp_cb_t cmp;
    void *hook;
};

struct fastdb {
    LOCK_DEF;
    unsigned int magic;
    struct redblack_tree redblack_root_phaseoffset;
    struct redblack_tree redblack_root_flavoroffset;
    struct fastdb_user_index *userindices;
    int n_indices;
/* current last byte of file + 1: */
    long long eof_offset;
/* first byte past header: */
    long long initial_offset;
/* total records alloced - both free and used: */
    long long record_count;
/* total records pending for write to disk: */
    long long write_pending_count;
    int record_size;
    int item_size;
    int not_allowed_to_add_indices;
    FILE *file;
};


long long fastdb_count (struct fastdb *m)
{
    assert (m->magic == FASTDB_MAGIC);
    return m->record_count;
}

long long fastdb_write_pending_count (struct fastdb *m)
{
    assert (m->magic == FASTDB_MAGIC);
    return m->write_pending_count;
}

/* caller controls locking */
void fastdb_lock (struct fastdb *m)
{
    assert (m->magic == FASTDB_MAGIC);
    LOCK_ON (m);
}

/* caller controls locking */
void fastdb_unlock (struct fastdb *m)
{
    assert (m->magic == FASTDB_MAGIC);
    LOCK_OFF (m);
}

static void put_size (unsigned char *p, unsigned int v)
{
    *p++ = (v >> 24) & 0xFF;
    *p++ = (v >> 16) & 0xFF;
    *p++ = (v >> 8) & 0xFF;
    *p++ = (v >> 0) & 0xFF;
}

static unsigned int get_size (const unsigned char *p)
{
    int r = 0;
    r <<= 8;
    r |= *p++;
    r <<= 8;
    r |= *p++;
    r <<= 8;
    r |= *p++;
    r <<= 8;
    r |= *p++;
    return r;
}

static int fastdb_fsync (FILE * f)
{
    fflush (f);
#ifdef _WIN32
    return !FlushFileBuffers ((HANDLE) _get_osfhandle (_fileno (f)));
#else
    return fsync (fileno (f));
#endif
}

static int fastdb_fseek (FILE * f, long long l)
{
#ifdef BALANCE_WIN32
    return _fseeki64 (f, l, SEEK_SET);
#else
    return fseeko (f, l, SEEK_SET);
#endif
}

/* return the pointer to start and truncate */
static int fastdb_ftruncate (FILE * f, long long l)
{
#ifdef _WIN32
    LONG upper, lower;
    HANDLE h;
    if (fastdb_fseek (f, 0LL))
        return 1;
    h = (HANDLE) _get_osfhandle (_fileno (o->f));
    upper = ((l >> 32) & 0xFFFFFFFFULL);
    lower = ((l >> 0) & 0xFFFFFFFFULL);
    if (SetFilePointer (h, lower, &upper, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return 1;
    if (!SetEndOfFile (h))
        return 1;
    upper = lower = 0;
    if (SetFilePointer (h, lower, &upper, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return 1;
    return 0;
#else
    fastdb_fseek (f, 0LL);
    if (ftruncate (fileno (f), l))
        return 1;
    return 0;
#endif
}

static int fastdb_load_ (struct fastdb *fastdb, const char *filename, char *err_msg)
{
    union fastdb_header h;
    FILE *f;

    f = fopen (filename, "rb");
    if (!f || fread (&h, sizeof (h), 1, f) != 1) {      /* empty file */
        xfclose (f);
        if (!(f = fopen (filename, "w+b")))
            goto error;
        memset (&h, '\0', sizeof (h));
        strncpy (h.h.magic, "fastdb", sizeof (h.h.magic));
        h.h.version = 2;
        put_size (h.h.record_size, fastdb->record_size);
        if (fwrite (&h, sizeof (h), 1, f) != 1) {
            snprintf (err_msg, ERROR_MSG_SIZE, "%s: %s", filename, strerror (errno));
            goto error;
        }
        if (fastdb_fsync (f)) {
            snprintf (err_msg, ERROR_MSG_SIZE, "%s: %s", filename, strerror (errno));
            goto error;
        }
        xfclose (f);
    } else {
        if (h.h.magic[0] == 'f' && h.h.magic[1] == 'a' && h.h.magic[2] == 's' && h.h.magic[3] == 't'
            && h.h.magic[4] == 'd' && h.h.magic[5] == 'b') {
            /* file magic check */
        } else {
            snprintf (err_msg, ERROR_MSG_SIZE, "%s: not a fastdb database", filename);
            goto error;
        }
        if (h.h.version != 2) {
            snprintf (err_msg, ERROR_MSG_SIZE, "%s: invalid version", filename);
            goto error;
        } else {
            if (get_size (h.h.record_size) != fastdb->record_size) {
                snprintf (err_msg, ERROR_MSG_SIZE, "%s: invalid user data size", filename);
                goto error;
            }
        }
        fastdb_lock (fastdb);
        {
            user_data_t *s, *blank_record;
            s = (user_data_t *) malloc (fastdb->record_size);
            blank_record = (user_data_t *) malloc (fastdb->record_size);
            memset (blank_record, '\0', fastdb->record_size);
            while (!feof (f) && fread (s, fastdb->record_size, 1, f) == 1) {
                if (!memcmp (s, blank_record, fastdb->record_size))
                    fastdb_insert_free_list (fastdb);
                else
                    fastdb_extend_database (fastdb, s, FASTDB_PHASE_USED, FASTDB_FLAVOR_WRITTEN);
            }
            free (blank_record);
            free (s);
        }
        fastdb_unlock (fastdb);
        if (!feof (f) && ferror (f)) {
            snprintf (err_msg, ERROR_MSG_SIZE, "%s: error loading database", filename);
            goto error;
        }
        xfclose (f);
    }
    if (!(fastdb->file = fopen (filename, "r+b")))
        goto error;
    return 0;

  error:
    xfclose (f);
    return 1;
}

static int redblack_node_phaseoffset (const void *a_, const void *b_, void *dummy)
{
    struct fastdb_item *a, *b;
    a = (struct fastdb_item *) a_;
    b = (struct fastdb_item *) b_;
    if (a->phase < b->phase)
        return -1;
    if (a->phase > b->phase)
        return 1;
    if (a->offset < b->offset)
        return -1;
    if (a->offset > b->offset)
        return 1;
    return 0;
}

static int redblack_node_flavoroffset (const void *a_, const void *b_, void *dummy)
{
    struct fastdb_item *a, *b;
    a = (struct fastdb_item *) a_;
    b = (struct fastdb_item *) b_;
    if (a->flavor < b->flavor)
        return -1;
    if (a->flavor > b->flavor)
        return 1;
    if (a->offset < b->offset)
        return -1;
    if (a->offset > b->offset)
        return 1;
    return 0;
}


struct fastdb *fastdb_setup (char *err_msg, int record_size)
{
    struct fastdb_item *v = NULL;
    struct fastdb *r;
    r = (struct fastdb *) malloc (sizeof (struct fastdb));
    memset (r, '\0', sizeof (*r));
    r->magic = FASTDB_MAGIC;
    r->initial_offset = r->eof_offset = sizeof (union fastdb_header);
    r->record_size = record_size;
    r->item_size = sizeof (struct fastdb_item) - sizeof (v->redblack_node_userindices);

    redblack_tree_new (&r->redblack_root_phaseoffset, offsetof (struct fastdb_item, redblack_node_phaseoffset), 1,
                       redblack_node_phaseoffset, NULL);
    redblack_tree_new (&r->redblack_root_flavoroffset, offsetof (struct fastdb_item, redblack_node_flavoroffset), 1,
                       redblack_node_flavoroffset, NULL);

    LOCK_INIT (r);
    return r;
}

static int user_index_cmp (const void *a_, const void *b_, void *user_data)
{
    struct fastdb_user_index *d;
    struct fastdb_item *a, *b;
    d = (struct fastdb_user_index *) user_data;

    a = (struct fastdb_item *) a_;
    b = (struct fastdb_item *) b_;

    return (*d->cmp) (a->data, b->data, d->hook);
}

/* beware: assert() will fail with allow_duplicates == 0 and duplicate insert */
int fastdb_add_index (struct fastdb *m, int allow_duplicates, redblack_cmp_cb_t cmp, void *hook)
{
    struct fastdb_user_index *n;
    struct fastdb_item *v = NULL;
    int item_offset;

    if (m->not_allowed_to_add_indices)
        return -1;

    item_offset = m->item_size;
    m->item_size += sizeof (v->redblack_node_userindices[m->n_indices - 1]);

    m->n_indices++;
    m->userindices =
        (struct fastdb_user_index *) realloc (m->userindices, sizeof (struct fastdb_user_index) * m->n_indices);
    n = &m->userindices[m->n_indices - 1];
    memset (n, '\0', sizeof (*n));
    n->cmp = cmp;
    n->hook = hook;
    redblack_tree_new (&n->redblack_root, item_offset, allow_duplicates, user_index_cmp, (void *) n);
    return m->n_indices - 1;
}

int fastdb_load (struct fastdb *m, const char *filename, char *err_msg)
{
    m->not_allowed_to_add_indices = 1;
    return fastdb_load_ (m, filename, err_msg);
}

static void fastdb_unlink (struct fastdb *m, struct fastdb_item *removed)
{
    int i;

    assert (removed->magic == FASTDB_ITEM_MAGIC);

/* flavor fully determines accounting write_pending_count */
    if (removed->redblack_node_flavoroffset.color)
        if (removed->flavor == FASTDB_FLAVOR_WRITEPENDING)
            m->write_pending_count--;

    redblack_tree_delete (&m->redblack_root_phaseoffset, removed);
    memset (&removed->redblack_node_phaseoffset, '\0', sizeof (removed->redblack_node_phaseoffset));
    redblack_tree_delete (&m->redblack_root_flavoroffset, removed);
    memset (&removed->redblack_node_flavoroffset, '\0', sizeof (removed->redblack_node_flavoroffset));

    for (i = 0; i < m->n_indices; i++) {
        if (removed->redblack_node_userindices[i].color) {
            redblack_tree_delete (&m->userindices[i].redblack_root, removed);
            memset (&removed->redblack_node_userindices[i], '\0', sizeof (removed->redblack_node_userindices[i]));
        }
    }
}

static void fastdb_item_free (struct fastdb *m, struct fastdb_item *removed)
{
    assert (removed->magic == FASTDB_ITEM_MAGIC);
    fastdb_unlink (m, removed);
    assert (removed->data);
    free (removed->data);
    free (removed);
    m->record_count--;
    assert (m->record_count >= 0);
}

void fastdb_free (struct fastdb *m)
{
    struct fastdb_item *n;
    int i;

    assert (m->magic == FASTDB_MAGIC);
    assert (!LOCK_IS_LOCKED (m));

/* records always linked in phaseoffset list, but only non-free records
are linked in key and time lists: */
    while ((n = redblack_tree_first (&m->redblack_root_phaseoffset))) {
        fastdb_item_free (m, n);
    }

    assert (!redblack_tree_count (&m->redblack_root_phaseoffset));
    assert (!redblack_tree_count (&m->redblack_root_flavoroffset));

    for (i = 0; i < m->n_indices; i++) {
        assert (!redblack_tree_count (&m->userindices[i].redblack_root));
    }

    assert (!m->record_count);

    free (m->userindices);

    xfclose (m->file);

    LOCK_FREE (m);

    memset (m, '\0', sizeof (*m));
    free (m);
}

static void fastdb_link (struct fastdb *m, struct fastdb_item *new_node)
{
    int i;

    assert (new_node->redblack_node_phaseoffset.color == REDBLACK_NOCOLOR);
    assert (new_node->redblack_node_flavoroffset.color == REDBLACK_NOCOLOR);

    for (i = 0; i < m->n_indices; i++) {
        assert (new_node->redblack_node_userindices[i].color == REDBLACK_NOCOLOR);
    }

    assert (m->magic == FASTDB_MAGIC);
    assert (LOCK_IS_LOCKED (m));

    redblack_tree_add (&m->redblack_root_phaseoffset, new_node);
    redblack_tree_add (&m->redblack_root_flavoroffset, new_node);

    for (i = 0; i < m->n_indices; i++) {
        int c;
        c = redblack_tree_add (&m->userindices[i].redblack_root, new_node);
        assert (c);
    }

    assert (m->write_pending_count >= 0);
    if (new_node->flavor == FASTDB_FLAVOR_WRITEPENDING)
        m->write_pending_count++;
}

static void fastdb_extend_database (struct fastdb *m, const user_data_t * data, char phase, char flavor)
{
    struct fastdb_item *new_node;

    assert (m->magic == FASTDB_MAGIC);

    assert (LOCK_IS_LOCKED (m));

/* item_size can't be changed after malloc */
    m->not_allowed_to_add_indices = 1;

    new_node = (struct fastdb_item *) malloc (m->item_size);
    memset (new_node, '\0', m->item_size);
    new_node->magic = FASTDB_ITEM_MAGIC;
    new_node->data = (user_data_t *) malloc (m->record_size);
    memcpy (new_node->data, data, m->record_size);
    new_node->phase = phase;
    new_node->flavor = flavor;
    new_node->offset = m->eof_offset;
    m->eof_offset += m->record_size;

    fastdb_link (m, new_node);
    assert (m->record_count >= 0);
    m->record_count++;
}

static void fastdb_link2 (struct fastdb *m, struct fastdb_item *new_node)
{
    int i;

    assert (new_node->redblack_node_phaseoffset.color == REDBLACK_NOCOLOR);
    assert (new_node->redblack_node_flavoroffset.color == REDBLACK_NOCOLOR);

    for (i = 0; i < m->n_indices; i++) {
        assert (new_node->redblack_node_userindices[i].color == REDBLACK_NOCOLOR);
    }

    assert (m->magic == FASTDB_MAGIC);
    assert (LOCK_IS_LOCKED (m));

    redblack_tree_add (&m->redblack_root_phaseoffset, new_node);
    redblack_tree_add (&m->redblack_root_flavoroffset, new_node);

    assert (m->write_pending_count >= 0);
    if (new_node->flavor == FASTDB_FLAVOR_WRITEPENDING)
        m->write_pending_count++;
}

static void fastdb_insert_free_list (struct fastdb *m)
{
    struct fastdb_item *new_node;

    assert (m->magic == FASTDB_MAGIC);

    assert (LOCK_IS_LOCKED (m));

    m->not_allowed_to_add_indices = 1;

    new_node = (struct fastdb_item *) malloc (m->item_size);
    memset (new_node, '\0', m->item_size);
    new_node->magic = FASTDB_ITEM_MAGIC;
    new_node->data = (user_data_t *) malloc (m->record_size);
    memset (new_node->data, '\0', m->record_size);
    new_node->phase = FASTDB_PHASE_FREE;
    new_node->flavor = FASTDB_FLAVOR_WRITTEN;
    new_node->offset = m->eof_offset;
    m->eof_offset += m->record_size;

    fastdb_link2 (m, new_node);
    assert (m->record_count >= 0);
    m->record_count++;
}

void fastdb_traverse (struct fastdb *m, int idx_num, void (*cb) (user_data_t *, void *, void *), void *user_data1,
                      void *user_data2)
{
    struct fastdb_item *n;
    user_data_t *d;

    d = (user_data_t *) malloc (m->record_size);

    assert (m->magic == FASTDB_MAGIC);

    LOCK_ON (m);

    n = redblack_tree_first (&m->userindices[idx_num].redblack_root);
    memcpy (d, n->data, m->record_size);

    while (n) {
        (*cb) (d, user_data1, user_data2);
        if ((n = redblack_tree_next (&m->userindices[idx_num].redblack_root)))
            memcpy (d, n->data, m->record_size);
    }

    LOCK_OFF (m);

    free (d);
}


static struct fastdb_item *fastdb_lookup_ (struct fastdb *m, int idx_number, user_data_t * data)
{
    struct fastdb_user_index *idx;
    struct fastdb_item v;
    idx = &m->userindices[idx_number];
    memset (&v, '\0', sizeof (v));
    v.data = data;
    assert (LOCK_IS_LOCKED (m));
    return redblack_tree_find (&idx->redblack_root, &v);
}

/* return zero on error */
static int _fastdb_lookup (struct fastdb *m, int idx_number, user_data_t * data)
{
    struct fastdb_item *r;
    r = fastdb_lookup_ (m, idx_number, data);
    if (!r)
        return 0;
    memcpy (data, r->data, m->record_size);
    return 1;
}

/* return zero on error */
int fastdb_lookup (struct fastdb *m, int idx_number, user_data_t * data)
{
    int r;
    LOCK_ON (m);
    r = _fastdb_lookup (m, idx_number, data);
    LOCK_OFF (m);
    return r;
}

int fastdb_lookup_lock (struct fastdb *m, int idx_number, user_data_t * data, enum cmp_op op, enum cmp_lean lean)
{
    struct fastdb_user_index *idx;
    struct fastdb_item v, *r;
    assert (m->magic == FASTDB_MAGIC);
    LOCK_ON (m);
    idx = &m->userindices[idx_number];
    memset (&v, '\0', sizeof (v));
    v.data = data;
    assert (LOCK_IS_LOCKED (m));
    r = redblack_tree_find_op (&idx->redblack_root, &v, op, lean);
    if (r) {
        memcpy (data, r->data, m->record_size);
        return 0;
    }
    LOCK_OFF (m);
    return 1;
}

int fastdb_next (struct fastdb *m, int idx_num, user_data_t * data)
{
    struct fastdb_item *r;
    if (!(r = redblack_tree_next (&m->userindices[idx_num].redblack_root)))
        return 1;
    memcpy (data, r->data, m->record_size);
    return 0;
}

/* return zero on error */
static int fastdb_update_ (struct fastdb *m, int idx_number, user_data_t * data)
{
    struct fastdb_item *r;
    r = fastdb_lookup_ (m, idx_number, data);
    if (!r)
        return 0;
    fastdb_relink (m, r, data, FASTDB_PHASE_USED, FASTDB_FLAVOR_WRITEPENDING);
    return 1;
}

int fastdb_update (struct fastdb *m, int idx_number, user_data_t * data)
{
    int r;
    LOCK_ON (m);
    r = fastdb_update_ (m, idx_number, data);
    LOCK_OFF (m);
    return r;
}

/* return zero on error */
static int fastdb_delete_ (struct fastdb *m, int idx_number, user_data_t * data)
{
    struct fastdb_item *p;
    p = fastdb_lookup_ (m, idx_number, data);
    if (!p)
        return 0;

    fastdb_unlink (m, p);
    p->phase = FASTDB_PHASE_FREE;
    p->flavor = FASTDB_FLAVOR_WRITEPENDING;
    memset (p->data, '\0', m->record_size);
    fastdb_link2 (m, p);

    return 1;
}

int fastdb_delete (struct fastdb *m, int idx_number, user_data_t * data)
{
    int r;
    LOCK_ON (m);
    r = fastdb_delete_ (m, idx_number, data);
    LOCK_OFF (m);
    return r;
}

long long fastdb_eof_offset (struct fastdb *m)
{
    return m->eof_offset;
}

static struct fastdb_item *fastdb_lookup_by_phaseoffset (struct fastdb *m, char phase, long long offset, enum cmp_op op,
                                                         enum cmp_lean lean)
{
    struct fastdb_item v;

    memset (&v, '\0', sizeof (v));

    v.phase = phase;
    v.offset = offset;

    assert (m->magic == FASTDB_MAGIC);
    assert (LOCK_IS_LOCKED (m));

    return redblack_tree_find_op (&m->redblack_root_phaseoffset, &v, op, lean);
}

static struct fastdb_item *fastdb_lookup_by_flavoroffset (struct fastdb *m, char flavor, long long offset,
                                                          enum cmp_op op, enum cmp_lean lean)
{
    struct fastdb_item v;

    memset (&v, '\0', sizeof (v));

    v.flavor = flavor;
    v.offset = offset;

    assert (m->magic == FASTDB_MAGIC);
    assert (LOCK_IS_LOCKED (m));

    return redblack_tree_find_op (&m->redblack_root_flavoroffset, &v, op, lean);
}

static long long fastdb_truncate_database (struct fastdb *m)
{
    assert (m->magic == FASTDB_MAGIC);
    assert (LOCK_IS_LOCKED (m));

    for (;;) {
        struct fastdb_item *found;
        found =
            fastdb_lookup_by_phaseoffset (m, FASTDB_PHASE_FREE, 0x4000000000000000, CMP_OP_LT,
                                          CMP_LEAN_RIGHT /* doesn't matter */ );
        if (!(found && found->phase == FASTDB_PHASE_FREE))
            return m->eof_offset;
        assert (found->offset + m->record_size <= m->eof_offset);
        if (found->offset + m->record_size < m->eof_offset)
            return m->eof_offset;
        m->eof_offset -= m->record_size;
        fastdb_item_free (m, found);
        if (m->eof_offset == m->initial_offset || !m->record_count || !m->redblack_root_phaseoffset.count
            || !m->redblack_root_flavoroffset.count) {
            int i;
            assert (!m->redblack_root_phaseoffset.count);
            assert (!m->redblack_root_flavoroffset.count);
            for (i = 0; i < m->n_indices; i++) {
                assert (!redblack_tree_count (&m->userindices[i].redblack_root));
            }
            assert (!m->record_count);
        }
    }
}

static void fastdb_relink (struct fastdb *m, struct fastdb_item *p, const user_data_t * data, char phase, char flavor)
{
    fastdb_unlink (m, p);

/* sanity checks: */
    assert (p->offset >= m->initial_offset);
    assert (p->offset <= m->eof_offset - m->record_size);
    assert (!((p->offset - m->initial_offset) % m->record_size));
    memcpy (p->data, data, m->record_size);

    p->phase = phase;
    p->flavor = flavor;

    fastdb_link (m, p);
}

#if 0
static void dump_by_flavoroffset (struct fastdb *m)
{
    struct fastdb_item *i;

    i = redblack_tree_first (&m->redblack_root_flavoroffset);
    while (i) {
        printf ("flavor=%d offset=%lld   --  phase=%d  \n", (int) i->flavor, i->offset, (int) i->phase);
        i = redblack_tree_next (&m->redblack_root_flavoroffset);
    }
}
#endif

/* returns 1 on error */
static int fastdb_update_from_pending_and_return_record (struct fastdb *m, user_data_t * data, long long *offset)
{
    struct fastdb_item *p;

    assert (m->magic == FASTDB_MAGIC);
    assert (LOCK_IS_LOCKED (m));

    p = fastdb_lookup_by_flavoroffset (m, FASTDB_FLAVOR_WRITEPENDING, 0LL, CMP_OP_GE, CMP_LEAN_LEFT);
    if (p && p->flavor == FASTDB_FLAVOR_WRITEPENDING) {
        assert (p->magic == FASTDB_ITEM_MAGIC);
        fastdb_unlink (m, p);
        *offset = p->offset;
        memcpy (data, p->data, m->record_size);
        assert (*offset >= m->initial_offset);
        assert (*offset <= m->eof_offset - m->record_size);
        assert (!((*offset - m->initial_offset) % m->record_size));
        p->flavor = FASTDB_FLAVOR_WRITTEN;
        switch (p->phase) {
        case FASTDB_PHASE_FREE:
            memset (p->data, '\0', m->record_size);
            fastdb_link2 (m, p);
            break;
        case FASTDB_PHASE_USED:
            fastdb_link (m, p);
            break;
        }
        return 0;
    }
    return 1;
}

static void fastdb_insert_ (struct fastdb *m, const user_data_t * data)
{
    struct fastdb_item *p;

    assert (m->magic == FASTDB_MAGIC);
    assert (LOCK_IS_LOCKED (m));

    p = fastdb_lookup_by_phaseoffset (m, FASTDB_PHASE_FREE, 0LL, CMP_OP_GE, CMP_LEAN_LEFT);
    if (p && p->phase == FASTDB_PHASE_FREE) {
/* an unused node node is available somewhere in the middle of the file: */
        fastdb_relink (m, p, data, FASTDB_PHASE_USED, FASTDB_FLAVOR_WRITEPENDING);
        return;
    }
    fastdb_extend_database (m, data, FASTDB_PHASE_USED, FASTDB_FLAVOR_WRITEPENDING);
}

void fastdb_insert (struct fastdb *m, const user_data_t * data)
{
    LOCK_ON (m);
    fastdb_insert_ (m, data);
    LOCK_OFF (m);
}

/* returns non-zero if already in the database */
void fastdb_insert_or_replace (struct fastdb *m, int idx_number, const user_data_t * data)
{
    user_data_t *ldata;
    struct fastdb_item *r;
    LOCK_ON (m);
    ldata = (user_data_t *) malloc (m->record_size);
    memcpy (ldata, data, m->record_size);
    r = fastdb_lookup_ (m, idx_number, ldata);
    if (r) {
        if (memcmp (data, ldata, m->record_size)) {
            fastdb_relink (m, r, data, FASTDB_PHASE_USED, FASTDB_FLAVOR_WRITEPENDING);
        }
    } else {
        fastdb_insert_ (m, data);
    }
    free (ldata);
    LOCK_OFF (m);
}

/* This is only ever called from one thread. Re-entering this function is a bug: */
int fastdb_flush (struct fastdb *fastdb, int flush_records, long long *count, long long *done_truncate, char *err_msg)
{
    int i, n;
    unsigned char *data;
    long long *offset;
    *count = 0LL;
    *done_truncate = 0;
    data = (unsigned char *) malloc (flush_records * fastdb->record_size);
    offset = (long long *) malloc (flush_records * sizeof (long long));
    fastdb_lock (fastdb);
    {
        long long eof_offset, orig_offset;
        orig_offset = fastdb_eof_offset (fastdb);
        eof_offset = fastdb_truncate_database (fastdb);
        if (eof_offset != orig_offset) {
            *done_truncate = orig_offset - eof_offset;
            fastdb_ftruncate (fastdb->file, eof_offset);
        }
        for (n = 0; n < flush_records; n++)
            if (fastdb_update_from_pending_and_return_record (fastdb, (user_data_t *) & data[n * fastdb->record_size], &offset[n]))
                break;
    }
    fastdb_unlock (fastdb);
    for (i = 0; i < n; i++) {
        (*count)++;
        if (fastdb_fseek (fastdb->file, offset[i])) {
            free (offset);
            free (data);
            return 1;
        }
        if (fwrite (data + (i * fastdb->record_size), fastdb->record_size, 1, fastdb->file) != 1) {
            free (offset);
            free (data);
            return 1;
        }
    }
    if (n) {
        if (fastdb_fsync (fastdb->file)) {
            free (offset);
            free (data);
            return 1;
        }
    }
    free (offset);
    free (data);
    return 0;
}


#ifdef UNITTEST

struct my_data {
    char key[16];
    char data[16];
};

static int idx_cmp (const void *a_, const void *b_, void *dummy)
{
    struct my_data *a, *b;
    int *test;
    test = (int *) dummy;
    a = (struct my_data *) a_;
    b = (struct my_data *) b_;
    assert (*test == 12345);
    return memcmp (a->key, b->key, sizeof (b->key));
}

int main ()
{
    struct fastdb *fdb;
    char err_msg[256];
    int test = 12345;
    int idx;
    int v;

    printf ("sizeof(long) = %d\n", (int) sizeof (long));

    unlink ("test.fdb");
    fdb = fastdb_setup (err_msg, 32);
    idx = fastdb_add_index (fdb, 1, idx_cmp, &test);
    if (fastdb_load (fdb, "test.fdb", err_msg)) {
        perror (err_msg);
        exit (1);
    }
    fastdb_free (fdb);


    fdb = fastdb_setup (err_msg, 32);
    idx = fastdb_add_index (fdb, 1, idx_cmp, &test);
    if (fastdb_load (fdb, "test.fdb", err_msg)) {
        perror (err_msg);
        fastdb_free (fdb);
        exit (1);
    }

    {
        struct my_data r;
        long long count = -1;
        long long done_truncate = -1;

        strncpy (r.key, "key001", 16);
        strncpy (r.data, "data001", 16);

        fastdb_insert (fdb, &r);
        assert (fdb->record_count == 1);
        assert (fdb->write_pending_count == 1);

        if (fastdb_flush (fdb, 99, &count, &done_truncate, err_msg)) {
            perror (err_msg);
            fastdb_free (fdb);
            exit (1);
        }




        strncpy (r.key, "key002", 16);
        strncpy (r.data, "data002", 16);

        fastdb_insert (fdb, &r);
        assert (fdb->record_count == 2);
        assert (fdb->write_pending_count == 1);

        strncpy (r.key, "key003", 16);
        strncpy (r.data, "data003", 16);

        fastdb_insert (fdb, &r);
        assert (fdb->record_count == 3);
        assert (fdb->write_pending_count == 2);

        if (fastdb_flush (fdb, 99, &count, &done_truncate, err_msg)) {
            perror (err_msg);
            fastdb_free (fdb);
            exit (1);
        }




        {
            int k;
            for (k = 4; k <= 9; k++) {
                strncpy (r.key, "key00", 16);
                r.key[5] = '0' + k;
                strncpy (r.data, "data00", 16);
                r.data[6] = '0' + k;

                fastdb_insert (fdb, &r);
                assert (fdb->record_count == k);
                assert (fdb->write_pending_count == 1);

                if (fastdb_flush (fdb, 99, &count, &done_truncate, err_msg)) {
                    perror (err_msg);
                    fastdb_free (fdb);
                    exit (1);
                }
            }
        }


        {
            int k;
            int c = 0;
            for (k = 2; k <= 8; k += 2) {
                strncpy (r.key, "key00", 16);
                r.key[5] = '0' + k;
                strncpy (r.data, "data00", 16);
                r.data[6] = '0' + k;

                v = fastdb_delete (fdb, idx, &r);
                assert (v == 1);
                c++;
                assert (fdb->record_count == 9);
                assert (fdb->write_pending_count == k / 2);
            }

            if (fastdb_flush (fdb, 99, &count, &done_truncate, err_msg)) {
                perror (err_msg);
                fastdb_free (fdb);
                exit (1);
            }
            assert (c == (int) count);
        }

        {
            int k;
            FILE *f;
            f = fopen ("test.fdb", "r");
            fseek (f, sizeof (union fastdb_header), 0);
            for (k = 1; k <= 9; k++) {
                struct my_data cmp, z;
                int c;
                memset (&z, '\0', sizeof (z));
                strncpy (r.key, "key00", 16);
                r.key[5] = '0' + k;
                strncpy (r.data, "data00", 16);
                r.data[6] = '0' + k;
                c = fread (&cmp, sizeof (cmp), 1, f);
                assert (c == 1);
                if (!(k % 2)) {
                    assert (!memcmp (&cmp, &z, sizeof (cmp)));
                } else {
                    assert (!memcmp (&cmp, &r, sizeof (cmp)));
                }
            }
            fclose (f);
        }


        {
            int k;
            int c = 0;
            for (k = 1; k <= 7; k += 2) {
                strncpy (r.key, "key00", 16);
                r.key[5] = '0' + k;
                strncpy (r.data, "data00", 16);
                r.data[6] = '0' + k;

                v = fastdb_delete (fdb, idx, &r);
                assert (v == 1);
                c++;
                assert (fdb->record_count == 9);
                assert (fdb->write_pending_count == (k + 1) / 2);
            }

            if (fastdb_flush (fdb, 99, &count, &done_truncate, err_msg)) {
                perror (err_msg);
                fastdb_free (fdb);
                exit (1);
            }
            assert (c == (int) count);
        }

        {
            int k;
            FILE *f;
            f = fopen ("test.fdb", "r");
            fseek (f, sizeof (union fastdb_header), 0);
            for (k = 1; k <= 9; k++) {
                struct my_data cmp, z;
                int c;
                memset (&z, '\0', sizeof (z));
                strncpy (r.key, "key00", 16);
                r.key[5] = '0' + k;
                strncpy (r.data, "data00", 16);
                r.data[6] = '0' + k;
                c = fread (&cmp, sizeof (cmp), 1, f);
                assert (c == 1);
                if (k != 9) {
                    assert (!memcmp (&cmp, &z, sizeof (cmp)));
                } else {
                    assert (!memcmp (&cmp, &r, sizeof (cmp)));
                }
            }
            fclose (f);
        }

        {
            strncpy (r.key, "key00", 16);
            r.key[5] = '0' + 3;
            strncpy (r.data, "data00", 16);
            r.data[6] = '0' + 3;
            v = fastdb_delete (fdb, idx, &r);
            assert (!v);

            strncpy (r.key, "key00", 16);
            r.key[5] = '0' + 9;
            strncpy (r.data, "data00", 16);
            r.data[6] = '0' + 9;
            v = fastdb_delete (fdb, idx, &r);
            assert (v);

            if (fastdb_flush (fdb, 99, &count, &done_truncate, err_msg)) {
                perror (err_msg);
                fastdb_free (fdb);
                exit (1);
            }
        }

        {
            struct stat st;
            fstat (fileno (fdb->file), &st);
            assert (st.st_size == sizeof (union fastdb_header));
        }

    }

    fastdb_free (fdb);

    return 0;
}
#endif



