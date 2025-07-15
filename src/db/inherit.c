/* $Header: /u/cvsroot/muse/src/db/inherit.c,v 1.6 1993/03/21 00:49:57 nils Exp $ */

#include <stdio.h>
#include "externs.h"

#include "config.h"
#include "db.h"

void put_atrdefs (f, defs)
     FILE *f;
     ATRDEF *defs;
{
  extern void putstring P((FILE *, char *));
  for (;defs;defs=defs->next) {
    fputc('/',f);
    putref(f, defs->a.flags);
    putref(f, defs->a.obj);
    putstring(f, defs->a.name);
  }
  fputs("\\\n",f);
}

ATRDEF *get_atrdefs (f,olddefs)
     FILE *f;
     ATRDEF *olddefs;
{
  extern char *getstring_noalloc P((FILE *));
  char k;
  ATRDEF *ourdefs=NULL, *endptr=NULL;
  
  for (;;) {
    k = fgetc(f);
    switch (k) {
    case '\\':
      if (fgetc(f)!='\n')
	log_error ("No atrdef newline.");
      return ourdefs;
      break;
    case '/':
      if (!endptr)
	if (olddefs) {
	  endptr = ourdefs = olddefs;
	  olddefs = olddefs->next;
	} else
	  endptr = ourdefs = malloc( sizeof(ATRDEF));
      else
	if (olddefs) {
	  endptr->next = olddefs;
	  olddefs = olddefs->next;
	  endptr = endptr->next;
	} else {
	  endptr->next = malloc( sizeof(ATRDEF));
	  endptr = endptr->next;
	}
      endptr->a.flags = getref(f);
      endptr->a.obj = getref(f);
      endptr->next = NULL;
      endptr->a.name = NULL;
      SET (endptr->a.name, getstring_noalloc(f));
      break;
    default:
      log_error ("Illegal character in get_atrdefs");
      return 0;
    }
  }
}

static void remove_attribute (obj, atr)
     dbref obj;
     ATTR *atr;
{
  int i;
  atr_add (obj, atr, "");
  for (i=0; db[obj].children && db[obj].children[i] != NOTHING; i++)
    remove_attribute (db[obj].children[i], atr);
}

void do_undefattr (player, arg1)
     dbref player;
     char *arg1;
{
  dbref obj;
  ATTR *atr;
  ATRDEF *d, *prev=NULL;
  
  if(!parse_attrib(player, arg1, &obj, &atr, POW_MODIFY)) {
    notify(player, "No match.");
    return;
  }
  for (d= db[obj].atrdefs; d; prev=d, d=d->next)
    if (&(d->a) == atr) {
      if (prev)
	prev->next = d->next;
      else
	db[obj].atrdefs = d->next;
      remove_attribute (obj, atr);
      if (0==--d->a.refcount) {	/* this should always happen, but... */
	free(d->a.name);
	free(d);
      }
      notify(player, "Deleted.");
      return;
    }
  notify(player, "No match.");
}

void do_defattr (player, arg1, arg2)
     dbref player;
     char *arg1,*arg2;
{
  ATTR *atr;
  char *flag_parse;
  int atr_flags = 0;
  dbref thing;
  char *attribute;
  int i;

  if (!(attribute=strchr(arg1,'/'))) {
    notify(player,"No match.");
    return;
  }
  *(attribute++) = '\0';
  
  thing = match_controlled(player, arg1, POW_MODIFY);
  if (thing == NOTHING)
    return;

  if (!ok_attribute_name (attribute)) {
    notify(player,"Illegal attribute name.");
    return;
  }
  
  while ((flag_parse = parse_up(&arg2,' '))) {
    if (flag_parse == NULL);	/* empty if, so we can do else: */
#define IFIS(foo,baz) else if(!string_compare(flag_parse,foo)) atr_flags|=baz
    IFIS("wizard",AF_WIZARD);
    IFIS("osee",AF_OSEE);
    IFIS("dark",AF_DARK);
    IFIS("inherit",AF_INHERIT);
    IFIS("unsaved",AF_UNIMP);
    IFIS("date",AF_DATE);
    IFIS("lock",AF_LOCK);
    IFIS("function",AF_FUNC);
    IFIS("dbref",AF_DBREF);
    IFIS("haven",AF_HAVEN);
#undef IFIS
  else
    notify(player,tprintf("Unknown attribute option: %s",flag_parse));
  }
  
  atr = atr_str (thing, thing, attribute);
  if (atr && atr->obj == thing) {
    atr->flags = atr_flags;
    notify(player, "Options set.");
    return;
  }

  {
    ATRDEF *k;
    
    for (k=db[thing].atrdefs,i=0; k; k=k->next, i++);
    if (i>90 && !power(player,POW_SECURITY)) {
      notify(player,"Sorry, you can't have that many attribute defs on an object.");
      return;
    }
    if (atr) {
      notify(player, "Sorry, attribute shadows a builtin attribute or one on a parent.");
      return;
    }
    k=malloc( sizeof(ATRDEF));
    k->a.name = NULL;
    SET (k->a.name, attribute);
    k->a.flags = atr_flags;
    k->a.obj = thing;
    k->a.refcount = 1;
    k->next = db[thing].atrdefs;
    db[thing].atrdefs = k;
  }
  notify(player,"Attribute defined.");
}


static int is_a_internal (thing, parent, dep)
     dbref thing;
     dbref parent;
     int dep;
{
  int j;
  if (thing == parent) return 1;
  if (dep < 0) return 1;
  if (!db[thing].parents) return 0;
  for (j=0; db[thing].parents[j]!=NOTHING; j++)
    if (is_a_internal (db[thing].parents[j], parent, dep-1))
      return 1;
  return 0;
}

int is_a (thing, parent)
     dbref thing;
     dbref parent;
{
  if (thing == NOTHING)
    return 1;
  return is_a_internal (thing, parent, 20); /* 20 is the max depth */
}
#if 0
static void reparent P((dbref, dbref, dbref *)), unparent P((dbref));
#endif

void do_delparent (player, arg1, arg2)
     dbref player;
     char *arg1,*arg2;
{
  dbref thing;
  dbref parent;
  int i;
  int can_doit = 0;

  thing = match_controlled(player, arg1 , POW_MODIFY);
  if (thing == NOTHING)
    return;
  mark_hearing(thing);
  parent = match_thing (player, arg2);
  if (parent == NOTHING) return;
  if (!(db[parent].flags&BEARING) && !controls(player,parent,POW_MODIFY)) {
    notify (player,tprintf("Sorry, you can't unparent from that."));
    can_doit = 1;
  }
      
  
  for (i=0; db[thing].parents && db[thing].parents[i]!=NOTHING;
       i++)
    if (db[thing].parents[i] == parent)
      can_doit |= 2;
  
  if (!(can_doit&2))
    notify (player, "Sorry, it doesn't have that as its parent.");
  if (can_doit != 2)
    return;

  REMOVE_FIRST_L (db[thing].parents, parent);
  REMOVE_FIRST_L (db[parent].children, thing);
  notify(player, tprintf("%s is no longer a parent of %s.",
			 unparse_object_a (player, parent),
			 unparse_object_a (player, thing)));
  check_hearing();
}

void do_addparent (player, arg1, arg2)
     dbref player;
     char *arg1,*arg2;
{
  dbref thing;
  dbref parent;
  int i;
  int can_doit = 0;

  thing = match_controlled(player, arg1 , POW_MODIFY);
  if (thing == NOTHING)
    return;
  mark_hearing(thing);
  parent = match_thing (player, arg2);
  if (parent == NOTHING)
    return;
  if (is_a(parent, thing)) {
    notify (player,tprintf("But %s is a descendant of %s!",
			   unparse_object_a (player, parent),
			   unparse_object_a (player, thing)));
    can_doit |= 4;
  }
  if (!(db[parent].flags&BEARING) && !controls(player,parent,POW_MODIFY)) {
    notify (player,tprintf("Sorry, you can't parent to that."));
    can_doit |= 1;
  }
      
  
  for (i=0; db[thing].parents && db[thing].parents[i]!=NOTHING;
       i++)
    if (db[thing].parents[i] == parent)
	can_doit |= 2;
  
  if (can_doit&2)
    notify (player, "Sorry, it already has that as its parent.");
  if (can_doit != 0)
    return;

  PUSH_L (db[thing].parents, parent);
  PUSH_L (db[parent].children, thing);
  notify(player, tprintf("%s is now a parent of %s.",
			 unparse_object_a (player, parent),
			 unparse_object_a (player, thing)));
  check_hearing();
}

