
/* $Id: hash.c,v 1.3 1993/07/25 19:06:50 nils Exp $ */
/* generic hash table functions */

#include "externs.h"
#include "hash.h"

static struct hashtab *hashtabs = NULL;

void free_hash()
{
  struct hashtab *i;
  struct hashtab *next;

  for (i = hashtabs; i; i = next)
  {
    int j;

    next = i->next;
    for (j = 0; j < i->nbuckets; j++)
    {
/*      int k;
   for (k=0; j->buckets[j][k].name; k++)
   free(i->buckets[j][k].name); */
      free(i->buckets[j]);
    }
    free(i->buckets);
    free(i->name);
    free(i);
  }
}

void do_showhash(player, arg1)
dbref player;
char *arg1;
{
  struct hashtab *i;

  if (!*arg1)
  {
    for (i = hashtabs; i; i = i->next)
      notify(player, i->name);
    notify(player, "Done.");
  }
  else
  {
    char buf[1024];

    for (i = hashtabs; i; i = i->next)
      if (!string_compare(arg1, i->name))
      {
	int j;

	notify(player, tprintf("%s (%d buckets):", i->name, i->nbuckets));
	for (j = 0; j < i->nbuckets; j++)
	{
	  int k;

	  notify(player, tprintf(" bucket %d:", j));
	  for (k = 0; i->buckets[j][k].name; k++)
	  {
	    sprintf(buf, "   %s: %s", i->buckets[j][k].name, (*i->display) (i->buckets[j][k].value));
	    notify(player, buf);
	  }
	}
	return;
      }
    notify(player, "Couldn't find that.");
    return;
  }
}

struct hashtab *make_hashtab(nbuck, ents, entsize, name, displayfunc)
int nbuck;
void *ents;
int entsize;
char *name;
char *(*displayfunc) P((void *));
{
  struct hashtab *op;
  int i;

  op = malloc(sizeof(struct hashtab));

  op->nbuckets = nbuck;
  op->buckets = malloc(sizeof(hashbuck) * nbuck);
  op->name = malloc(strlen(name) + 1);
  strcpy(op->name, name);
  op->display = displayfunc;
  op->next = hashtabs;
  hashtabs = op;

  for (i = 0; i < nbuck; i++)
  {
    int numinbuck = 1;
    struct hashdeclent *thisent;

    for (thisent = ents; thisent->name; thisent = (struct hashdeclent *)(((char *)thisent) + entsize))
    {
      int val = hash_name(thisent->name);

      if ((val % nbuck) == i)
	numinbuck++;
    }
    op->buckets[i] = malloc(sizeof(struct hashent) * numinbuck);

    op->buckets[i][--numinbuck].name = NULL;	/* end of list marker */
    for (thisent = ents; thisent->name; thisent = (struct hashdeclent *)(((char *)thisent) + entsize))
    {
      int val = hash_name(thisent->name);

      if ((val % nbuck) == i)
      {
	op->buckets[i][--numinbuck].name = thisent->name;
	op->buckets[i][numinbuck].hashnum = val;
	op->buckets[i][numinbuck].value = thisent;
      }
    }
  }
  return op;
}

void *lookup_hash(tab, hashvalue, name)
struct hashtab *tab;
int hashvalue;
char *name;
{
  hashbuck z = tab->buckets[hashvalue % tab->nbuckets];
  int i;

  for (i = 0; z[i].name; i++)
    if (z[i].hashnum == hashvalue)
      if (!string_compare(z[i].name, name))
	return z[i].value;
  return NULL;
}

int hash_name(name)
char *name;
{
  unsigned short op = 0;
  int i;
  int j;
  int shift = 0;

  for (i = 0; name[i]; i++)
  {
    j = to_lower(name[i]);	/* case insensitive */
    op ^= j << (shift);
    op ^= j >> (16 - shift);
    shift = (shift + 11) % 16;
  }
  return op;
}
