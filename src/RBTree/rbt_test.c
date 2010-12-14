/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "rbt_node.h"
#include "rbt_tree.h"

#define LENBUF 256
#define RBT_NUM 16

void print_node(struct rbt_head *head)
{
  struct rbt_node *it;

  printf("header 0x%lx : root 0x%lx lm 0x%lx rm 0x%lx num = %d\n",
         head, head->root, head->leftmost, head->rightmost, head->rbt_num_node);

  RBT_LOOP(head, it)
  {
    printf("node 0x%lx : flags 0%o p 0x%lx a 0x%lx "
           "l 0x%lx r 0x%lx val = %d\n",
           it, it->rbt_flags, it->parent, it->anchor, it->left, it->next, it->rbt_value);

    RBT_INCREMENT(it);
  }

  return;
}

main(int argc, char **argv)
{
  struct rbt_head head;
  struct rbt_head *node_head;
  struct rbt_node *node_free;
  struct rbt_node *pn, *qn, *rn;
  int rc;
  int ok;
  int val;
  int i;
  char c;
  int debug;
  char *p;
  char buf[LENBUF];

  debug = 0;
  ok = 1;

  node_free = 0;

  node_head = &head;
  RBT_HEAD_INIT(node_head);

  while(ok)
    {
      RBT_VERIFY(node_head, pn, rc);
      if(rc)
        {
          printf("verify retourne %d, noeud 0x%lx\n", rc, pn);
          print_node(node_head);
        }

      fputs("> ", stdout);
      p = fgets(buf, LENBUF, stdin);
      if(p == 0)
        {
          printf("fin des commandes\n");
          ok = 0;
          continue;
        }
      p = strchr(buf, '\n');
      if(p)
        *p = '\0';
      rc = sscanf(buf, "%c %d", &c, &val);
      if(rc != 2)
        {
          printf("scanf retourne %d\n", rc);
          continue;
        }

      switch (c)
        {
        case 'a':
          if(node_free)
            {
              qn = node_free;
              node_free = node_free->next;
            }
          else
            {
              qn = (struct rbt_node *)malloc(RBT_NUM * sizeof(struct rbt_node));
              if(qn)
                {
                  i = RBT_NUM - 2;
                  node_free = qn + 1;
                  for(pn = node_free; i; pn++, i--)
                    {
                      pn->next = pn + 1;
                    }
                  pn->next = (struct rbt_node *)NULL;
                }
              else
                {
                  fprintf(stderr, "erreur d'allocation d'un node\n");
                  ok = 0;
                  continue;
                }
            }

          RBT_VALUE(qn) = val;
          RBT_FIND(node_head, pn, val);
          RBT_INSERT(node_head, qn, pn);
          continue;

        case 'd':
          debug = val;
          continue;

        case 'f':
          RBT_FIND(node_head, pn, val);
          if((pn == 0) || (RBT_VALUE(pn) != val))
            {
              printf("node %d pas trouve\n", val);
              continue;
            }
          continue;

        case 'l':
          RBT_FIND_LEFT(node_head, pn, val);
          if(pn == 0)
            {
              printf("node %d pas trouve\n", val);
              continue;
            }
          if(RBT_VALUE(pn) != val)
            {
              printf("mauvais node 0x%lx (%d) pour la valeur %d\n",
                     pn, RBT_VALUE(pn), val);
              print_node(node_head);
              continue;
            }
          qn = pn;
          RBT_DECREMENT(qn);
          if(qn && (RBT_VALUE(qn) == val))
            {
              printf("mauvais node 0x%lx pour la valeur %d\n", qn, val);
              print_node(node_head);
              continue;
            }
          continue;

        case 'p':
          print_node(node_head);
          continue;

        case 'q':
          ok = 0;
          continue;

        case 'r':
          RBT_FIND(node_head, pn, val);
          if((pn == 0) || (RBT_VALUE(pn) != val))
            {
              printf("node %d pas trouve\n", val);
              continue;
            }
          RBT_UNLINK(node_head, pn);
          pn->next = node_free;
          node_free = pn;
          continue;
        }
    }
}
