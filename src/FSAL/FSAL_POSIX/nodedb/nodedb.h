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


struct nodedb;
struct stat;

#define FILE_DATA_EQUAL(a, b) \
        ((a)->handle.fsid  == (b)->handle.fsid && \
         (a)->handle.devid == (b)->handle.devid && \
         (a)->handle.inode == (b)->handle.inode && \
         (a)->extra.type == (b)->extra.type)

#define FILE_DATA_EQUAL_(a, b) \
        ((a)->handle.devid == (b)->handle.devid && \
         (a)->handle.inode == (b)->handle.inode && \
         (a)->extra.type == (b)->extra.type)

struct extra {
    int nlinks;
    int type;
    unsigned long long ctime;
};

struct handle_data {
    unsigned long long fsid;
    unsigned long long devid;
    unsigned long long inode;
};

struct file_data {
    struct handle_data handle;
    struct extra extra;
    struct file_data *p;        /* pointer to self for testing */
    unsigned int handleid;
};

#if 0
struct dirent_data {
    char *name;
    struct file_data fdata;
};
#endif

struct nodedb *nodedb_new (void);

char **nodedb_strsplit (const char *s, char c, int max_split);
char *dir_entry_name_cat (const char *name1, const char *name2);
void nodedb_stat_to_file_data (unsigned long long fsid, const struct stat *st, struct file_data *file_data);
int nodedb_stat_to_file_type (const struct stat *st);
void nodedb_lock (struct nodedb *);
void nodedb_unlock (struct nodedb *);
void _nodedb_print (struct nodedb *db);
void nodedb_sync (struct nodedb *db);




