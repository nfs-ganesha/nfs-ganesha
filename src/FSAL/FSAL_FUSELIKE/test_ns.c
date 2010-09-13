#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "namespace.h"
#include "BuddyMalloc.h"

typedef struct ns_testset__
{
  ino_t parent_inode;
  ino_t entry_inode;
  char *name;
} ns_testset_t;

#define ROOT_INODE  ((ino_t)1)
#define DEV ((dev_t)1)

/* Test set */
ns_testset_t testset[] = {
  {ROOT_INODE, ROOT_INODE + 1, "dir1"},
  {ROOT_INODE, ROOT_INODE + 2, "dir2"},
  {ROOT_INODE, ROOT_INODE + 3, "dir3"},
  {ROOT_INODE, ROOT_INODE + 4, "dir4"},
  {ROOT_INODE + 4, ROOT_INODE + 5, "file1"},
  {ROOT_INODE + 4, ROOT_INODE + 6, "file2"},
  {ROOT_INODE + 4, ROOT_INODE + 7, "subdir1"},
  {ROOT_INODE + 4, ROOT_INODE + 8, "subdir2"},
  {ROOT_INODE + 8, ROOT_INODE + 9, "file1"},
  {ROOT_INODE + 8, ROOT_INODE + 10, "file2"},
  {ROOT_INODE + 8, ROOT_INODE + 10, "file2.harlink"},   /* same inode = hardlink */
  {ROOT_INODE, ROOT_INODE + 11, "dir5"},
  {ROOT_INODE + 11, ROOT_INODE + 10, "file2.hardlink"}, /* same inode = another hardlink */

  {0, 0, NULL}                  /* end of list */

};

int main(int argc, char **argv)
{
  int rc, i;
  ns_testset_t *p_test;
  char path[FSAL_MAX_PATH_LEN];
  unsigned int gen = 0;
  unsigned int dev = DEV;

  /* Init logging */

  SetNamePgm("test_ns");
  SetDefaultLogging("TEST");
  SetNameFunction("main");
  InitLogging();

#ifndef _NO_BUDDY_SYSTEM
  BuddyInit(NULL);
#endif

  /* namespace init */
  rc = NamespaceInit(ROOT_INODE, DEV, &gen);
  if(rc)
    {
      LogTest("NamespaceInit rc=%d\n", rc);
      exit(1);
    }

  for(i = 0; i < 2; i++)
    {

      /* creation des entrees */
      for(p_test = testset; p_test->name != NULL; p_test++)
        {
          rc = NamespaceAdd(p_test->parent_inode, DEV, gen, p_test->name,
                            p_test->entry_inode, DEV, &gen);
          LogTest("NamespaceAdd(%lu,%s->%lu) = %d\n", p_test->parent_inode, p_test->name,
                 p_test->entry_inode, rc);
          if(rc)
            exit(1);            /* This is an error */
        }

      /* tentative de recreation */
      for(p_test = testset; p_test->name != NULL; p_test++)
        {
          rc = NamespaceAdd(p_test->parent_inode, DEV, gen, p_test->name,
                            p_test->entry_inode, DEV, &gen);
          LogTest("Redundant NamespaceAdd(%lu,%s->%lu) = %d\n", p_test->parent_inode,
                 p_test->name, p_test->entry_inode, rc);
          if(rc)
            exit(1);            /* This is an error */
        }

      /* recolte du chemin complet de root */

      rc = NamespacePath(ROOT_INODE, DEV, gen, path);
      if(rc)
        {
          LogTest("NamespacePath(%lu) rc=%d\n", ROOT_INODE, rc);
          exit(1);
        }
      else
        LogTest("NamespacePath(%lu) => \"%s\"\n", ROOT_INODE, path);

      /* recolte du chemin complet des entrees */

      for(p_test = testset; p_test->name != NULL; p_test++)
        {
          rc = NamespacePath(p_test->entry_inode, DEV, gen, path);
          if(rc)
            {
              LogTest("NamespacePath(%lu) rc=%d\n", p_test->entry_inode, rc);
              exit(1);
            }
          else
            LogTest("NamespacePath(%lu) => \"%s\"\n", p_test->entry_inode, path);
        }

      /* on efface les entrees en ordre inverse */
      for(p_test--; p_test >= testset; p_test--)
        {
          rc = NamespaceRemove(p_test->parent_inode, DEV, gen, p_test->name);
          LogTest("NamespaceRemove(%lu,%s) = %d\n", p_test->parent_inode,
                 p_test->name, rc);
        }

      /* on essaye d'obtenir leur nom */
      for(p_test = testset; p_test->name != NULL; p_test++)
        {
          rc = NamespacePath(p_test->entry_inode, DEV, gen, path);
          if(rc == 0)
            {
              LogTest("NamespacePath(%lu) => \"%s\"\n", p_test->entry_inode, path);
              exit(1);
            }
          else if(rc != ENOENT)
            {
              LogTest("NamespacePath(%lu) rc=%d\n", p_test->entry_inode, rc);
              exit(1);
            }
          else
            LogTest("NamespacePath(%lu) rc=%d (ENOENT)\n", p_test->entry_inode, rc);
        }
    }

  /* now create/remove a hardlink to a file N times */

  rc = NamespaceAdd(ROOT_INODE, DEV, gen, "dir", ROOT_INODE + 1, DEV, &gen);
  if(rc)
    {
      LogTest("NamespaceAdd error %d line %d\n", rc, __LINE__ - 1);
      exit(1);
    }

  rc = NamespaceAdd(ROOT_INODE + 1, DEV, gen, "subdir", ROOT_INODE + 2, DEV, &gen);
  if(rc)
    {
      LogTest("NamespaceAdd error %d line %d\n", rc, __LINE__ - 1);
      exit(1);
    }

  rc = NamespaceAdd(ROOT_INODE + 2, DEV, gen, "entry", ROOT_INODE + 3, DEV, &gen);
  if(rc)
    {
      LogTest("NamespaceAdd error %d line %d\n", rc, __LINE__ - 1);
      exit(1);
    }

  /* create hardlinks and lookup */
  for(i = 0; i < 3; i++)
    {
      char name[FSAL_MAX_NAME_LEN];

      sprintf(name, "entry.hl%d", i);

      rc = NamespaceAdd(ROOT_INODE + 2, DEV, gen, name, ROOT_INODE + 3, DEV, &gen);
      LogTest("NamespaceAdd(%lu,%s->%lu) = %d\n", ROOT_INODE + 2, name, ROOT_INODE + 3,
             rc);
      if(rc)
        exit(1);

      rc = NamespacePath(ROOT_INODE + 3, DEV, gen, path);
      if(rc)
        {
          LogTest("NamespacePath(%lu) rc=%d\n", ROOT_INODE + 3, rc);
          exit(1);
        }
      else
        LogTest("NamespacePath(%lu) => \"%s\"\n", ROOT_INODE + 3, path);

    }

  /* delete hardlinks and lookup */
  for(i = 0; i < 3; i++)
    {
      char name[FSAL_MAX_NAME_LEN];

      sprintf(name, "entry.hl%d", i);

      rc = NamespaceRemove(ROOT_INODE + 2, DEV, gen, name);
      LogTest("NamespaceRemove(%lu,%s) = %d\n", ROOT_INODE + 2, name, rc);
      if(rc)
        exit(1);

      rc = NamespacePath(ROOT_INODE + 3, DEV, gen, path);
      if(rc)
        {
          LogTest("NamespacePath(%lu) rc=%d\n", ROOT_INODE + 3, rc);
          exit(1);
        }
      else
        LogTest("NamespacePath(%lu) => \"%s\"\n", ROOT_INODE + 3, path);

    }

  return 0;

}
