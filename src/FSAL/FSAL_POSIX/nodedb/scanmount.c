#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>



#define min(a,b)  ((a) < (b) ? (a) : (b))


#if defined(__STRICT_ANSI__) && defined(__GNUC__)
FILE *popen();
int pclose();
#endif



const char *mount_shell_script = "sh -c \
'#!/bin/sh\n\
\n\
PATH=/sbin:/usr/sbin:$PATH\n\
export PATH\n\
\n\
case `uname` in\n\
  SunOS) :\n\
    mount | awk \"{print \\$1}\" ;;\n\
  Linux) :\n\
    mount | awk \"{print \\$3}\" ;;\n\
  HP-UX) :\n\
    mount | awk \"{print \\$1}\" ;;\n\
  FreeBSD) :\n\
    mount | awk \"{print \\$3}\" ;;\n\
esac\n\
\n\
'\n\
";


/* we are not interested in speed here */
unsigned long long pauls_hash_64bit_version (unsigned char *p)
{
    unsigned long long h = 0x54741a07a07400f6ULL;
    while (*p) {
        unsigned long long v;
        h += *p;
        v = h % 151660541ULL;
        h += ((v + 9) * (v + 2) * 401) >> 1;
        h ^= (h << 21) ^ (h >> 42);
        p++;
    }
    return h;
}



struct mount_item {
    struct mount_item *next;
    char *path;
    unsigned long long fsid;
    int len;
};

struct mount_list {
    struct mount_item *first;
    int count;
};

static struct mount_list mount_list = { NULL, 0 };

/* add in order of length descending */
static void add_mount (struct mount_list *list_, const char *path)
{
    int l;
    struct mount_item *n, *i;
    l = strlen (path);
    n = (struct mount_item *) malloc (sizeof (*n));
    n->path = (char *) malloc (l + 1);
    n->len = l;
    strcpy (n->path, path);
    n->fsid = pauls_hash_64bit_version ((unsigned char *) n->path);
    for (i = (struct mount_item *) list_; i; i = i->next) {
        if (!i->next || i->next->len <= n->len) {
            if (i->next && !strcmp(i->next->path, n->path))
                break;  /* skip duplicates */
            n->next = i->next;
            i->next = n;
            list_->count++;
            break;
        }
    }
}

static void free_mount_list (struct mount_list *l)
{
    struct mount_item *i, *n;
    for (i = l->first; i; i = n) {
        n = i->next;
        free (i->path);
        free (i);
    }
    l->first = NULL;
    l->count = 0;
}

void read_mounts (void)
{
    struct mount_list new_list = { NULL, 0 };
    char line[1024];
    FILE *p;
    p = popen (mount_shell_script, "r");
    if (!p) {
        perror ("popen");
        return;
    }
    while (fgets (line, sizeof (line), p)) {
        int l;
        l = strlen (line);
        while (l > 0 && line[l - 1] <= ' ') {
            l--;
            line[l] = '\0';
        }
        if (line[0] != '/')
            continue;
        add_mount (&new_list, line);
    }
    add_mount (&new_list, "/");
    {
        struct mount_list old_list;
        old_list = mount_list;
        mount_list = new_list;
        free_mount_list (&old_list);
    }
    pclose (p);
}

int get_mount_count (void)
{
    return mount_list.count;
}

/* lookup, longer paths first */
unsigned long long get_fsid (const char *path)
{
    struct mount_item *i;
    int l;
    assert (path);
    assert (*path == '/');
    l = strlen (path);
    for (i = mount_list.first; i; i = i->next) {
        if (l >= i->len)
            if (!strncmp (path, i->path, i->len) && (i->len == l || path[i->len] == '/'))
                return i->fsid;
        if (i->len == 1)
            return i->fsid;
    }
    assert (!"not possible");
    return 0LL;
}

#if 0
void dump_mounts (void)
{
    struct mount_item *i;
    for (i = mount_list.first; i; i = i->next) {
        printf ("\n %16llx [%s]\n", i->fsid, i->path);
        printf (" %16llx [%s]\n", get_fsid (i->path), i->path);
    }
    printf ("lookup -- %16llx [%s]\n", get_fsid ("/usr/local/bin/something"), "/usr/local/bin/something");
    printf ("lookup -- %16llx [%s]\n", get_fsid ("/mnt/win7/somefile"), "/mnt/win7/somefile");
}


int main(int argc, char **argv)
{
    read_mounts();
    dump_mounts();
    return 0;
}
#endif



