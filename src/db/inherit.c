/* inherit.c */
/* $Header: /u/cvsroot/muse/src/db/inherit.c,v 1.6 1993/03/21 00:49:57 nils Exp $ */

#include <stdio.h>
#include "externs.h"
#include "config.h"
#include "db.h"

/* ===================================================================
 * Constants
 * =================================================================== */

#define MAX_INHERIT_DEPTH 20  /* Maximum parent chain depth */
#define MAX_ATTRDEFS 90       /* Maximum attribute definitions per object */

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static int is_a_internal(dbref thing, dbref parent, int dep);
static void remove_attribute(dbref obj, ATTR *atr);

/* ===================================================================
 * Attribute Definition I/O Functions
 * =================================================================== */

/**
 * Write attribute definitions to a file
 * @param f File pointer to write to
 * @param defs Attribute definition list to write
 */
void put_atrdefs(FILE *f, ATRDEF *defs)
{
  ATRDEF *current;
  
  if (!f)
  {
    log_error("put_atrdefs: NULL file pointer");
    return;
  }
  
  for (current = defs; current; current = current->next)
  {
    fputc('/', f);
    putref(f, current->a.flags);
    putref(f, current->a.obj);
    putstring(f, current->a.name);
  }
  fputs("\\\n", f);
}

/**
 * Read attribute definitions from a file
 * @param f File pointer to read from
 * @param olddefs Existing definitions to reuse if available
 * @return New attribute definition list or NULL on error
 */
ATRDEF *get_atrdefs(FILE *f, ATRDEF *olddefs)
{
  char k;
  ATRDEF *ourdefs = NULL;
  ATRDEF *endptr = NULL;
  
  if (!f)
  {
    log_error("get_atrdefs: NULL file pointer");
    return NULL;
  }
  
  for (;;)
  {
    k = fgetc(f);
    switch (k)
    {
    case '\\':
      if (fgetc(f) != '\n')
        log_error("No atrdef newline.");
      return ourdefs;
      
    case '/':
      if (!endptr)
      {
        if (olddefs)
        {
          endptr = ourdefs = olddefs;
          olddefs = olddefs->next;
        }
        else
        {
          SAFE_MALLOC(endptr, ATRDEF, 1);
          ourdefs = endptr;
        }
      }
      else
      {
        if (olddefs)
        {
          endptr->next = olddefs;
          olddefs = olddefs->next;
          endptr = endptr->next;
        }
        else
        {
          SAFE_MALLOC(endptr->next, ATRDEF, 1);
          endptr = endptr->next;
        }
      }
      
      /* Read attribute definition data */
      endptr->a.flags = getref(f);
      endptr->a.obj = getref(f);
      endptr->next = NULL;
      endptr->a.name = NULL;
      SET(endptr->a.name, getstring_noalloc(f));
      break;
      
    default:
      log_error("Illegal character in get_atrdefs");
      return ourdefs;  /* Return what we have so far */
    }
  }
}

/* ===================================================================
 * Attribute Management Functions
 * =================================================================== */

/**
 * Recursively remove an attribute from an object and its children
 * @param obj Object to remove attribute from
 * @param atr Attribute to remove
 */
static void remove_attribute(dbref obj, ATTR *atr)
{
  int i;
  
  if (!GoodObject(obj) || !atr)
    return;
  
  atr_add(obj, atr, "");
  
  for (i = 0; db[obj].children && db[obj].children[i] != NOTHING; i++)
  {
    if (GoodObject(db[obj].children[i]))
      remove_attribute(db[obj].children[i], atr);
  }
}

/**
 * Undefine a user-defined attribute
 * @param player Player issuing the command
 * @param arg1 Object/attribute specification
 */
void do_undefattr(dbref player, char *arg1)
{
  dbref obj;
  ATTR *atr;
  ATRDEF *d;
  ATRDEF *prev = NULL;
  
  if (!arg1 || !*arg1)
  {
    notify(player, "Undefine what attribute?");
    return;
  }
  
  if (!parse_attrib(player, arg1, &obj, &atr, POW_MODIFY))
  {
    notify(player, "No match.");
    return;
  }
  
  if (!GoodObject(obj))
  {
    notify(player, "Invalid object.");
    return;
  }
  
  /* Find the attribute definition */
  for (d = db[obj].atrdefs; d; prev = d, d = d->next)
  {
    if (&(d->a) == atr)
    {
      /* Remove from list */
      if (prev)
        prev->next = d->next;
      else
        db[obj].atrdefs = d->next;
      
      /* Clean up the attribute from object and children */
      remove_attribute(obj, atr);
      
      /* Free memory if no longer referenced */
      if (0 == --d->a.refcount)
      {
        if (d->a.name)
          SMART_FREE(d->a.name);
        SMART_FREE(d);
      }
 
      notify(player, "Deleted.");
      return;
    }
  }

  notify(player, "No match.");
}

/**
 * Define a new user-defined attribute
 * @param player Player issuing the command
 * @param arg1 Object/attribute specification
 * @param arg2 Attribute flags
 */
void do_defattr(dbref player, char *arg1, char *arg2)
{
  ATTR *atr;
  char *flag_parse;
  int atr_flags = 0;
  dbref thing;
  char *attribute;
  int i;
  ATRDEF *k;

  if (!arg1 || !*arg1)
  {
    notify(player, "Define what attribute?");
    return;
  }

  /* Split object/attribute */
  attribute = strchr(arg1, '/');
  if (!attribute)
  {
    notify(player, "No match.");
    return;
  }
  *attribute = '\0';
  attribute++;
  
  /* Find the object */
  thing = match_controlled(player, arg1, POW_MODIFY);
  if (thing == NOTHING)
    return;

  /* Validate attribute name */
  if (!ok_attribute_name(attribute))
  {
    notify(player, "Illegal attribute name.");
    return;
  }
  
  /* Parse flags */
  while ((flag_parse = parse_up(&arg2, ' ')))
  {
    if (flag_parse == NULL)
      ; /* empty if statement */
#define IFIS(foo, baz) else if (!string_compare(flag_parse, foo)) atr_flags |= baz
    IFIS("wizard", AF_WIZARD);
    IFIS("osee", AF_OSEE);
    IFIS("dark", AF_DARK);
    IFIS("inherit", AF_INHERIT);
    IFIS("unsaved", AF_UNIMP);
    IFIS("date", AF_DATE);
    IFIS("lock", AF_LOCK);
    IFIS("function", AF_FUNC);
    IFIS("dbref", AF_DBREF);
    IFIS("haven", AF_HAVEN);
#undef IFIS
    else
      notify(player, tprintf("Unknown attribute option: %s", flag_parse));
  }
  
  /* Check if attribute already exists */
  atr = atr_str(thing, thing, attribute);
  if (atr && atr->obj == thing)
  {
    /* Attribute already defined on this object - update flags */
    atr->flags = atr_flags;
    notify(player, "Options set.");
    return;
  }

  /* Count existing attribute definitions */
  for (k = db[thing].atrdefs, i = 0; k; k = k->next, i++)
    ;
    
  if (i > MAX_ATTRDEFS && !power(player, POW_SECURITY))
  {
    notify(player, "Sorry, you can't have that many attribute defs on an object.");
    return;
  }
  
  /* Check for shadowing */
  if (atr)
  {
    notify(player, "Sorry, attribute shadows a builtin attribute or one on a parent.");
    return;
  }
  
  /* Create new attribute definition */
  SAFE_MALLOC(k, ATRDEF, 1);
  k->a.name = NULL;
  SET(k->a.name, attribute);
  k->a.flags = atr_flags;
  k->a.obj = thing;
  k->a.refcount = 1;
  k->next = db[thing].atrdefs;
  db[thing].atrdefs = k;
  
  notify(player, "Attribute defined.");
}

/* ===================================================================
 * Parent/Child Relationship Functions
 * =================================================================== */

/**
 * Internal function to check parent relationship with depth limit
 * @param thing Object to check
 * @param parent Potential parent
 * @param dep Maximum depth to search
 * @return 1 if thing is descended from parent, 0 otherwise
 */
static int is_a_internal(dbref thing, dbref parent, int dep)
{
  int j;
  
  if (thing == parent)
    return 1;
    
  if (dep < 0)
    return 0;  /* Too deep - prevent infinite loops */
    
  if (!db[thing].parents)
    return 0;
  
  for (j = 0; db[thing].parents[j] != NOTHING; j++)
  {
    if (GoodObject(db[thing].parents[j]))
    {
      if (is_a_internal(db[thing].parents[j], parent, dep - 1))
        return 1;
    }
  }
  
  return 0;
}

/**
 * Check if an object is descended from another
 * @param thing Object to check
 * @param parent Potential ancestor
 * @return 1 if thing is descended from parent, 0 otherwise
 */
int is_a(dbref thing, dbref parent)
{
  if (thing == NOTHING)
    return 1;
    
  if (!GoodObject(thing) || !GoodObject(parent))
    return 0;
  
  return is_a_internal(thing, parent, MAX_INHERIT_DEPTH);
}

/**
 * Remove a parent from an object
 * @param player Player issuing the command
 * @param arg1 Child object name
 * @param arg2 Parent object name
 */
void do_delparent(dbref player, char *arg1, char *arg2)
{
  dbref thing;
  dbref parent;
  int i;
  int can_doit = 0;

  if (!arg1 || !*arg1)
  {
    notify(player, "Remove parent from what?");
    return;
  }
  
  if (!arg2 || !*arg2)
  {
    notify(player, "Remove what parent?");
    return;
  }

  thing = match_controlled(player, arg1, POW_MODIFY);
  if (thing == NOTHING)
    return;
    
  mark_hearing(thing);
  
  parent = match_thing(player, arg2);
  if (parent == NOTHING)
  {
    check_hearing();
    return;
  }
  
  /* Check permissions */
  if (!(db[parent].flags & BEARING) && !controls(player, parent, POW_MODIFY))
  {
    notify(player, "Sorry, you can't unparent from that.");
    can_doit = 1;
  }
  
  /* Check if actually a parent */
  for (i = 0; db[thing].parents && db[thing].parents[i] != NOTHING; i++)
  {
    if (db[thing].parents[i] == parent)
      can_doit |= 2;
  }
  
  if (!(can_doit & 2))
    notify(player, "Sorry, it doesn't have that as its parent.");
    
  if (can_doit != 2)
  {
    check_hearing();
    return;
  }

  /* Remove from both parent and child lists */
  REMOVE_FIRST_L(db[thing].parents, parent);
  REMOVE_FIRST_L(db[parent].children, thing);
  
  notify(player, tprintf("%s is no longer a parent of %s.",
                         unparse_object_a(player, parent),
                         unparse_object_a(player, thing)));
  check_hearing();
}

/**
 * Add a parent to an object
 * @param player Player issuing the command
 * @param arg1 Child object name
 * @param arg2 Parent object name
 */
void do_addparent(dbref player, char *arg1, char *arg2)
{
  dbref thing;
  dbref parent;
  int i;
  int can_doit = 0;

  if (!arg1 || !*arg1)
  {
    notify(player, "Add parent to what?");
    return;
  }
  
  if (!arg2 || !*arg2)
  {
    notify(player, "Add what parent?");
    return;
  }

  thing = match_controlled(player, arg1, POW_MODIFY);
  if (thing == NOTHING)
    return;
    
  mark_hearing(thing);
  
  parent = match_thing(player, arg2);
  if (parent == NOTHING)
  {
    check_hearing();
    return;
  }
  
  /* Check for circular inheritance */
  if (is_a(parent, thing))
  {
    notify(player, tprintf("But %s is a descendant of %s!",
                          unparse_object_a(player, parent),
                          unparse_object_a(player, thing)));
    can_doit |= 4;
  }
  
  /* Check permissions */
  if (!(db[parent].flags & BEARING) && !controls(player, parent, POW_MODIFY))
  {
    notify(player, "Sorry, you can't parent to that.");
    can_doit |= 1;
  }
  
  /* Check if already a parent */
  for (i = 0; db[thing].parents && db[thing].parents[i] != NOTHING; i++)
  {
    if (db[thing].parents[i] == parent)
      can_doit |= 2;
  }
  
  if (can_doit & 2)
    notify(player, "Sorry, it already has that as its parent.");
    
  if (can_doit != 0)
  {
    check_hearing();
    return;
  }

  /* Add to both parent and child lists */
  PUSH_L(db[thing].parents, parent);
  PUSH_L(db[parent].children, thing);
  
  notify(player, tprintf("%s is now a parent of %s.",
                         unparse_object_a(player, parent),
                         unparse_object_a(player, thing)));
  check_hearing();
}
