/* db.c */
/* $Id: db.c,v 1.34 1994/02/18 22:40:43 nils Exp $ */


#include "credits.h"
/* #include "config.c" */
#include "motd.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define __DO_DB_C__
#include "db.h"
#include "config.h"
#include "externs.h"

/* there's something wrong with this code, memwatch, or both.
 * if you try to use memwatch with db.c, demuse won't get past 
 * loading object #2 for some reason.  Very weird problem which
 * I spent about 3 hours on and just said to hell with it. -wm
 */

#include "interface.h"
#include "hash.h"
#undef __DO_DB_C__


dbref combat_list = NOTHING;
struct object *db = 0;
dbref db_top = 0;

#ifdef TEST_MALLOC
int malloc_count = 0;

#endif /* TEST_MALLOC */

#undef MEMWATCH

dbref db_size = 100;

extern char ccom[];
extern long epoch;
static ALIST *AL_MAKE P((ATTR *, ALIST *, char *));
static dbref *getlist P((FILE *));
static void putlist P((FILE *, dbref *));

struct builtinattr
{
  ATTR definition;
  int number;
};

struct builtinattr attr[] =
{
#define DOATTR(var, name, flags, num) {{name, flags, NOTHING, 1}, num},
#include "attrib.h"
#undef DOATTR
  {
    {0, 0, 0, 0}, 0}

};

#define NUM_BUILTIN_ATTRS ((sizeof(attr)/sizeof(struct builtinattr))-1)
#define MAX_ATTRNUM 2048	/* less than this, but this will do. */

static ATTR *builtin_atr(num)
int num;
{
  static int initted = 0;
  static ATTR *numtable[MAX_ATTRNUM];

  if (!initted)
  {
    /* we need to init. */
    int i;

    initted = 1;

    for (i = 0; i < MAX_ATTRNUM; i++)
    {
      int a = 0;

      while ((a < NUM_BUILTIN_ATTRS) && (attr[a].number != i))
	a++;
      if (a < NUM_BUILTIN_ATTRS)
	numtable[i] = &(attr[a].definition);
      else
	numtable[i] = NULL;
    }
  }
  if (num >= 0 && num < MAX_ATTRNUM)
    return numtable[num];
  else
    return NULL;
}
static char *attr_disp(atr)
void *atr;
{
  return tprintf("#%d, flags #%d", ((struct builtinattr *)atr)->number, ((struct builtinattr *)atr)->definition.flags);
}

ATTR *builtin_atr_str(str)
char *str;
{
  static struct hashtab *attrhash = NULL;

  if (!attrhash)
    attrhash = make_hashtab(207, attr, sizeof(struct builtinattr), "attr", attr_disp);

  return &((struct builtinattr *)lookup_hash(attrhash, hash_name(str), str))->definition;
/*  for (a=0; a<NUM_BUILTIN_ATTRS; a++)
   if (!string_compare (str, attr[a].definition.name))
   return &(attr[a].definition);
   return NULL; */
}

static ATTR *atr_defined_on_str(obj, str)
dbref obj;
char *str;
{
  ATRDEF *k;

  if (obj < 0 || obj >= db_top)
    return NULL;		/* sanity check */

  for (k = db[obj].atrdefs; k; k = k->next)
    if (!string_compare(k->a.name, str))
      return &(k->a);
  return NULL;
}

static ATTR *atr_find_def_str(obj, str)
dbref obj;
char *str;
{
  ATTR *k;
  int i;

  if ((k = atr_defined_on_str(obj, str)))
    return k;
  for (i = 0; db[obj].parents && db[obj].parents[i] != NOTHING; i++)
    if ((k = atr_find_def_str(db[obj].parents[i], str)))
      return k;
  return NULL;
}

ATTR *atr_str(player, obj, s)
dbref player;
dbref obj;
char *s;
{
  ATTR *a;
  char *t;

  if ((t = strchr(s, '.')))
  {				/* definately a user defined attribute. */
    dbref onobj;
    static char mybuf[1000];

    if (t == s)			/* first thing too! make it global. */
      return builtin_atr_str(s + 1);
    strcpy(mybuf, s);
    mybuf[t - s] = '\0';
    init_match(player, mybuf, NOTYPE);
    match_everything();
    onobj = match_result();
    if (onobj == AMBIGUOUS)
      onobj = NOTHING;
    if (onobj != NOTHING)
    {
      a = atr_defined_on_str(onobj, t + 1);
/*      if (is_a (player, a->obj))
   return a; */
      if (a)
	return a;
    }
  }
  if (player != NOTHING)
  {
    a = atr_find_def_str(player, s);
    if (a && is_a(obj, a->obj))
      return a;
  }

  a = builtin_atr_str(s);
  if (a)
    return a;
  a = atr_find_def_str(obj, s);
  return a;			/* if everything else fails, use the one on
				   the obj. */
}

/* routines to handle object attribute lists */

/* single attribute cache */
static dbref atr_obj = -1;
static ATTR *atr_atr = NULL;
static char *atr_p;

/* clear an attribute in the list */
void atr_clr(thing, atr)
dbref thing;
ATTR *atr;
{
  ALIST *ptr = db[thing].list;

  atr_obj = -1;
  while (ptr)
  {
    if (AL_TYPE(ptr) == atr)
    {
      if (AL_TYPE(ptr))
	unref_atr(AL_TYPE(ptr));
      AL_DISPOSE(ptr);
      return;
    }
    ptr = AL_NEXT(ptr);
  }
}

ALIST *AL_MAKE();

/* add attribute of type atr to list */
void atr_add(thing, atr, s)
dbref thing;
ATTR *atr;
char *s;
{
  ALIST *ptr;
  char *d;

  if (!(atr->flags & AF_NOMEM))
    db[thing].i_flags |= I_UPDATEBYTES;

  if (!s)
    s = "";
  if (atr == NULL)
    return;
  for (ptr = db[thing].list; ptr && (AL_TYPE(ptr) != atr); ptr = AL_NEXT(ptr))
    ;
  if (!*s)
  {
    if (ptr)
    {
      unref_atr(AL_TYPE(ptr));
      AL_DISPOSE(ptr);
    }
    else
      return;
  }
  if (!ptr || (strlen(s) > strlen(d = (char *)AL_STR(ptr))))
  {
    /* allocate room for the attribute */
    if (ptr)
    {
      AL_DISPOSE(ptr);
      db[thing].list = AL_MAKE(atr, db[thing].list, s);
    }
    else
    {
      ref_atr(AL_TYPE((db[thing].list = AL_MAKE(atr, db[thing].list, s))));
    }
  }
  else
    strcpy(d, s);
  atr_obj = -1;
}

/* used internally by defines */
static char *atr_get_internal(thing, atr)
dbref thing;
ATTR *atr;
{
  int i;
  ALIST *ptr;
  char *k;

  for (ptr = db[thing].list; ptr; ptr = AL_NEXT(ptr))
    if (ptr && (AL_TYPE(ptr) == atr))
      return ((char *)(AL_STR(ptr)));
  for (i = 0; db[thing].parents && db[thing].parents[i] != NOTHING; i++)
    if ((k = atr_get_internal(db[thing].parents[i], atr)))
      if (*k)
	return k;
  return "";
}

char *atr_get(thing, atr)
dbref thing;
ATTR *atr;
{
  ALIST *ptr;

  if (thing < 0 || thing >= db_top || !atr)
    return "";			/* sanity check */

  if ((thing == atr_obj) && (atr == atr_atr))
    return (atr_p);
  atr_obj = thing;
  atr_atr = atr;
  if (atr->flags & AF_BUILTIN)
  {
/*
    static char buf[1024];
*/
    char buf[1024];

    if (atr == A_LOCATION)
      sprintf(buf, "#%ld", db[thing].location);
    else if (atr == A_OWNER)
      sprintf(buf, "#%ld", db[thing].owner);
    else if (atr == A_LINK)
      sprintf(buf, "#%ld", db[thing].link);
    else if (atr == A_PARENTS || atr == A_CHILDREN)
    {
      dbref *list;
      int i;

      *buf = '\0';
      list = (atr == A_PARENTS) ? db[thing].parents : db[thing].children;
      for (i = 0; list && list[i] != NOTHING; i++)
	if (*buf)
	  sprintf(buf + strlen(buf), " #%ld", list[i]);
	else
	  sprintf(buf, "#%ld", list[i]);
    }
    else if (atr == A_CONTENTS)
    {
      dbref it;

      *buf = '\0';
      for (it = db[thing].contents; it != NOTHING; it = db[it].next)
	if (*buf)
	  strcpy(buf, tprintf("%s #%ld", buf, it));
	else
	  sprintf(buf, "#%ld", it);
    }
    else if (atr == A_EXITS)
    {
      dbref it;

      *buf = '\0';
      for (it = db[thing].exits; it != NOTHING; it = db[it].next)
	if (*buf)
	  strcpy(buf, tprintf("%s #%ld", buf, it));
	else
	  sprintf(buf, "#%ld", it);
    }
    else if (atr == A_NAME)
      strcpy(buf, db[thing].name);
    else if (atr == A_CNAME)
      strcpy(buf, db[thing].cname);
    else if (atr == A_FLAGS)
      strcpy(buf, unparse_flags(thing));
    else if (atr == A_ZONE)
      sprintf(buf, "#%ld", db[thing].zone);
    else if (atr == A_NEXT)
      sprintf(buf, "#%ld", db[thing].next);
    else if (atr == A_MODIFIED)
      sprintf(buf, "%ld", db[thing].mod_time);
    else if (atr == A_CREATED)
      sprintf(buf, "%ld", db[thing].create_time);
    else if (atr == A_LONGFLAGS)
      sprintf(buf, "%s", flag_description(thing));

/*  old age attr stuff
    else if (atr == A_AGE)
    {
      struct descriptor_data *d;
      int found = 0;

      *buf = '\0';
      if (Typeof(thing) == TYPE_PLAYER)
      {
	for (d = descriptor_list; d && !found; d = d->next)
	{
	  if (d->player == thing && d->state == CONNECTED)
	  {
	    sprintf(buf, "%ld", atol(atr_get(thing, A_PREVTIME)) + (now - d->connected_at));
	    found = 1;
	  }
	}

	if (!found)
	  strcpy(buf, atr_get(thing, A_PREVTIME));
      }
    }
*/

    else
      sprintf(buf, "???");
    return atr_p = tprintf("%s",buf);
  }
  for (ptr = db[thing].list; ptr; ptr = AL_NEXT(ptr))
    if (ptr && AL_TYPE(ptr) == atr)
      return (atr_p = (char *)(AL_STR(ptr)));
  if (atr->flags & AF_INHERIT)
    return (atr_p = atr_get_internal(thing, atr));
  else
    return (atr_p = "");
}

void atr_free(thing)
dbref thing;
{
  ALIST *ptr, *next;

  if (thing < 0 || thing >= db_top)
    return;			/* sanity check */

  for (ptr = db[thing].list; ptr; ptr = next)
  {
    next = AL_NEXT(ptr);
    SAFE_FREE(ptr);
  }
  db[thing].list = NULL;
  atr_obj = -1;
}

static ALIST *AL_MAKE(type, next, string)
ATTR *type;
ALIST *next;
char *string;
{
  ALIST *ptr;

//  ptr = (ALIST *) malloc(sizeof(ALIST) + strlen(string) + 1);
  ptr = (ALIST *)safe_malloc(sizeof(ALIST) + strlen(string) + 1, __FILE__, __LINE__);
  AL_TYPE(ptr) = type;
  ptr->next = next;
  strcpy(AL_STR(ptr), string);
  return (ptr);
}

/* garbage collect an attribute list */
void atr_collect(thing)
dbref thing;
{
  ALIST *ptr, *next;

  if (thing < 0 || thing >= db_top)
    return;			/* sanity check */

  ptr = db[thing].list;
  db[thing].list = NULL;
  while (ptr)
  {
    if (AL_TYPE(ptr))
      db[thing].list = AL_MAKE(AL_TYPE(ptr), db[thing].list, AL_STR(ptr));
    next = AL_NEXT(ptr);
    SAFE_FREE(ptr);
    ptr = next;
  }
  atr_obj = -1;
}

void atr_cpy_noninh(dest, source)
dbref dest;
dbref source;
{
  ALIST *ptr;

  ptr = db[source].list;
  db[dest].list = NULL;
  while (ptr)
  {
    if (AL_TYPE(ptr) && !(AL_TYPE(ptr)->flags & AF_INHERIT))
    {
      db[dest].list = AL_MAKE(AL_TYPE(ptr), db[dest].list, AL_STR(ptr));
      ref_atr(AL_TYPE(ptr));
    }
    ptr = AL_NEXT(ptr);
  }
}

int db_init = 0;

static void db_grow(newtop)
dbref newtop;
{
  struct object *newdb;

  if (newtop > db_top)
  {
//    if (!db)
//    {
//      /* make the initial db */
//      db_size = (db_init) ? db_init : 100;
//      if ((db = 5 + (struct object *)
//	   malloc((db_size + 5) * sizeof(struct object))) == 0)
//      {
//	abort();
//      }
//      memchr(db - 5, 0, sizeof(struct object) * (db_size + 5));
//    }

    if (!db)
    {
      /* make the initial db */
      db_size = (db_init) ? db_init : 100;
      struct object *temp;
      SAFE_MALLOC(temp, struct object, db_size + 5);
      db = temp + 5;
      memchr(db - 5, 0, sizeof(struct object) * (db_size + 5));
    }

    /* maybe grow it */
    if (newtop > db_size)
    {
      /* make sure it's big enough */
      while (newtop > db_size)
	db_size *= 2;
      newdb = realloc(db - 5, (5 + db_size) * sizeof(struct object));

      if (!newdb)
	abort();
      newdb += 5;
      memchr(newdb + db_top, 0, sizeof(struct object) * (db_size - db_top));

      db = newdb;
    }
    db_top = newtop;
  }
}

dbref new_object()
{
  dbref newobj;
  struct object *o;

  if ((newobj = free_get()) == NOTHING)
  {
    newobj = db_top;
    db_grow(db_top + 1);
  }

  /* clear it out */
  o = db + newobj;
  o->name = NULL;
  o->cname = NULL;
  o->list = NULL;
  o->location = NOTHING;
  o->contents = NOTHING;
  o->exits = NOTHING;
  o->fighting = NOTHING;
  o->parents = NULL;
  o->children = NULL;
  o->link = NOTHING;
  o->next = NOTHING;
  o->next_fighting = NOTHING;
  o->owner = NOTHING;
  /* o->penn = 0; */
  /* flags you must initialize yourself */
  o->mod_time = 0;
  o->create_time = now;
  o->zone = NOTHING;
#ifdef USE_UNIV
  o->universe = db[0].universe;
#endif
  o->i_flags = I_UPDATEBYTES;
  o->size = 0;
  return newobj;
}

#define DB_MSGLEN 1040

void putref(f, ref)
FILE *f;
dbref ref;
{
  fprintf(f, "%ld\n", ref);
}

///*
//   void putstring(f,s)
//   FILE *f;
//   char *s;
//   {
//   if (s) fputs(s, f);
//   fputc('\n', f);
//   }
// */
//
///* static char *atr_format P((ATTR *));
//   static char *atr_format (k)
//   ATTR *k;
//   {
//   static char opbuf[100];
//   ATRDEF *def;
//   int j;
//
//   if (k->obj == NOTHING) {
//   sprintf(opbuf,"%d",*(((int *)k)-1)); * get the atr number. kludgy. *
//   return opbuf;
//   }
//   for (j=0,def=db[k->obj].atrdefs; def; def=def->next, j++)
//   if (&(def->a) == k) {
//   sprintf(opbuf,"%d.%d",k->obj, j);
//   return opbuf;
//   }
//   sprintf(opbuf,"%d.%d",k->obj,0);
//   return opbuf;
//   }
//   static ATTR *atr_unformat (o, str)
//   dbref o;
//   char *str;
//   {
//   dbref obj;
//   int num;
//   int i;
//   ATRDEF *atr;
//
//   if (!strchr(str,'.'))
//   return builtin_atr(atol(str));
//   *strchr(str,'.') = '\0';
//   obj = atol(str);
//   num = atol(str+strlen(str)+1);
//   if (obj>=o) {
//   db_grow (obj+1);
//   if (!db[obj].atrdefs) {
//   db[obj].atrdefs=malloc(sizeof(ATRDEF));
//   db[obj].atrdefs->a.name=NULL;
//   db[obj].atrdefs->next=NULL;
//   }
//   for (atr = db[obj].atrdefs, i=0; atr->next && i<num; atr=atr->next, i++)
//   ;
//   while (i < num) {
//   atr->next = malloc( sizeof(ATRDEF));
//   atr->next->a.name = NULL;
//   atr->next->next = NULL;
//   atr = atr->next;
//   i++;
//   }
//   } else
//   for (atr=db[obj].atrdefs, i=0; i<num; atr=atr->next, i++)
//   ;
//   return &(atr->a);
//   } */

static int db_write_object(f, i)
FILE *f;
dbref i;
{
  struct object *o;
  ALIST *list;
  int x;

  o = db + i;
  putstring(f, o->name);
  putstring(f, o->cname);
  putref(f, o->location);
  putref(f, o->zone);
  putref(f, o->contents);
  putref(f, o->exits);
  putref(f, o->fighting);
  putref(f, o->link);
  putref(f, o->next);
  putref(f, o->owner);
  /* putref(f, Pennies(i)); */
  putref(f, o->flags);
  putref(f, o->mod_time);
  putref(f, o->create_time);
  if (Typeof(i) == TYPE_PLAYER)
    put_powers(f, i);
  /* write the attribute list */
  for (list = o->list; list; list = AL_NEXT(list))
  {
    if (AL_TYPE(list))
    {
      ATTR *x;

      x = AL_TYPE(list);
      if (x && !(x->flags & AF_UNIMP))
      {
	if (x->obj == NOTHING)
	{			/* builtin attribute. */
	  fputc('>', f);
	  putref(f, ((struct builtinattr *)x)->number);		/* kludgy.
								   fix this.
								   xxxx */
	  putref(f, NOTHING);
	}
	else
	{
	  ATRDEF *m;
	  int j;

	  fputc('>', f);

	  for (m = db[AL_TYPE(list)->obj].atrdefs, j = 0;
	       m; m = m->next, j++)
	    if ((&(m->a)) == AL_TYPE(list))
	    {
	      putref(f, j);
	      break;
	    }
	  if (!m)
	  {
	    putref(f, 0);
	    putref(f, NOTHING);
	  }
	  else
	    putref(f, AL_TYPE(list)->obj);
	}
	putstring(f, (AL_STR(list)));
      }
    }
  }
  fprintf(f, "<\n");
  putlist(f, o->parents);
  putlist(f, o->children);
  put_atrdefs(f, o->atrdefs);
#ifdef USE_UNIV
  fprintf(f, ">%ld\n", o->universe);
  if (((o->flags & TYPE_MASK) == TYPE_UNIVERSE))
    for (x = 0; x < NUM_UA; x++)
    {
      switch (univ_config[x].type)
      {
      case UF_BOOL:
      case UF_INT:
	fprintf(f, "/%d:%d\n", x, o->ua_int[x]);
	break;
      case UF_FLOAT:
	fprintf(f, "/%d:%f\n", x, o->ua_float[x]);
	break;
      case UF_STRING:
	fprintf(f, "/%d:%s\n", x, o->ua_string[x]);
	break;
      default:
	break;
      }
    }
  fprintf(f, "\\\n");
#endif
  return 0;
}

dbref db_write(f)
FILE *f;
{
  dbref i;

  write_loginstats(epoch);

  fprintf(f, "@%d\n", DB_VERSION);
  fprintf(f, "~%ld\n", db_top);
  for (i = 0; i < db_top; i++)
  {
    fprintf(f, "&%ld\n", i);
    db_write_object(f, i);
  }
  fputs("***END OF DUMP***\n", f);
  write_mail(f);
  fflush(f);
  return (db_top);
}

dbref parse_dbref(s)
char *s;
{
  char *p;
  long x;

  x = atol(s);
  if (x > 0)
    return x;
  else if (x == 0)
    /* check for 0 */
    for (p = s; *p; p++)
    {
      if (*p == '0')
	return 0;
      if (!isspace(*p))
	break;
    }

  /* else x < 0 or s != 0 */
  return NOTHING;
}

dbref getref(f)
FILE *f;
{
  static char buf[DB_MSGLEN];

  fgets(buf, DB_MSGLEN, f);
  if (buf[strlen(buf) - 1] == '\n')
    buf[strlen(buf) - 1] = '\0';
  return (atol(buf));
}

char *getstring_noalloc(f)
FILE *f;
{
  static char buf[DB_MSGLEN];

  atr_fgets(buf, DB_MSGLEN, f);
  if (buf[strlen(buf) - 1] == '\n')
    buf[strlen(buf) - 1] = '\0';

  return buf;
}

/*#define getstring(x) alloc_string(getstring_noalloc(x)) */
#define getstring(x,p) {p=NULL;SET(p,getstring_noalloc(x));}

#define getstring_compress(x,p) getstring(x,p)

/* just return the string, for now, we need to convert attrs later */
static void getboolexp(i, f)
dbref i;
FILE *f;
{
  char buffer[DB_MSGLEN];
  char *p = buffer;
  int c;

  while ((c = getc(f)) != '\n')
  {
    if (c == ':')
    {				/* snarf up the attribute */
      *p++ = c;
      while ((c = getc(f)) != '\n')
	*p++ = c;
    }
    else
      *p++ = c;
  }
  *p = '\0';
  atr_add(i, A_LOCK, buffer);
}

char *b;

static void get_num(s, i)
char **s;
int *i;
{
  *i = 0;
  while (**s && isdigit(**s))
  {
    *i = (*i * 10) + **s - '0';
    (*s)++;
  }
  return;
}

#define RIGHT_DELIMITER(x) ((x == AND_TOKEN) || (x == OR_TOKEN) || (x == ':') || (x == '.') || (x == ')') || (x == '=') || (!x))

static void grab_dbref(p)
char *p;
{
  int num, n;
  dbref thing;
  ATRDEF *atr;
  ATTR *attr;

  get_num(&b, &num);
  switch (*b)
  {
  case '.':
    b++;
    sprintf(p, "#%d", num);
    p += strlen(p);
    *p++ = '.';
    thing = (dbref) num;
    get_num(&b, &num);
    for (atr = db[thing].atrdefs, n = 0; n < num; atr = atr->next, n++) ;
    strcpy(p, atr->a.name);
    p += strlen(p);
    *p++ = *b++;
    while (!RIGHT_DELIMITER(*b))
      *p++ = *b++;
    *p = '\0';
    break;
  case ':':
    b++;
    attr = builtin_atr(num);
    strcpy(p, attr->name);
    p += strlen(p);
    *p++ = ':';
    while (!RIGHT_DELIMITER(*b))
      *p++ = *b++;
    *p = '\0';
    break;
  default:
    sprintf(p, "#%d", num);
    break;
  }
  return;
}

static int convert_sub(p, outer)
char *p;
int outer;
{
  int inner;

  if (!*b)
  {
    *p = '\0';
    return 0;
  }
  switch (*b)
  {
  case '(':
    b++;
    inner = convert_sub(p, outer);
    if (*b == ')')
      b++;
    else
    {
      p += strlen(p);
      goto part2;
    }
    return inner;
  case NOT_TOKEN:
    *p++ = *b++;
    if ((inner = convert_sub(p + 1, outer)) > 0)
    {
      *p = '(';
      p += strlen(p);
      *p++ = ')';
      *p = '\0';
    }
    else
    {
      p++;
      while (*p)
	*(p - 1) = *p++;
      *--p = '\0';
    }
    return inner;
  default:
    /* a dbref of some sort */
    grab_dbref(p);
    p += strlen(p);
  }
part2:
  switch (*b)
  {
  case AND_TOKEN:
    *p++ = *b++;
    if ((inner = convert_sub(p + 1, 1)) == 2)
    {
      *p = '(';
      p += strlen(p);
      *p++ = ')';
      *p = '\0';
    }
    else
    {
      p++;
      while (*p)
	*(p - 1) = *p++;
      *--p = '\0';
    }
    return 1;
  case OR_TOKEN:
    *p++ = *b++;
    inner = convert_sub(p, 2);
    p += strlen(p);
    return 2;
  default:
    return 0;
  }
}

static int is_zone(i)
dbref i;
{
  dbref j;

  for (j = 0; j < db_top; j++)
    if (db[j].zone == i)
      return 1;
  return 0;
}

static void convert_boolexp()
{
  dbref i;
  char buffer[BUFFER_LEN], *p;

  for (i = 0; i < db_top; i++)
  {
    p = buffer;
    b = atr_get(i, A_LOCK);
    convert_sub(p, 0);
    if ((db[i].flags & ENTER_OK) && (!is_zone(i)))
    {
      atr_add(i, A_ELOCK, buffer);
      sprintf(buffer, "#%ld", db[i].owner);
      atr_add(i, A_LOCK, buffer);
    }
    else
      atr_add(i, A_LOCK, buffer);
  }
  return;
}

//static void db_free()
//{
//  dbref i;
//  struct object *o;
//
//  if (db)
//  {
//    for (i = 0; i < db_top; i++)
//    {
//      o = &db[i];
//      SET(o->name, NULL);
//      atr_free(i);
//      /* Everything starts off very old */
//    }
//    free(db - 5);
//    db = 0;
//    db_init = db_top = 0;
//  }
//}

static void db_free()
{
  dbref i;
  struct object *o;
  if (db)
  {
    for (i = 0; i < db_top; i++)
    {
      o = &db[i];
      SET(o->name, NULL);
      atr_free(i);
      /* Everything starts off very old */
    }
    struct object *temp = db - 5;  /* Get the original allocated pointer */
    SAFE_FREE(temp);
    db = 0;
    db_init = db_top = 0;
  }
}

/* read attribute list */
static int get_list(f, obj, vers)
FILE *f;
dbref obj;
int vers;
{
  ATRDEF *atr;
  int atrnum;
  dbref atrobj;
  dbref old_db_top;
  char *s;
  int c;
  int i;

  while (1)
    switch (c = fgetc(f))
    {
    case '>':			/* read # then string */
      atrnum = getref(f);
      if (vers <= 7)
      {
	if (builtin_atr(atrnum) && !(builtin_atr(atrnum)->flags & AF_UNIMP))
	  atr_add(obj, builtin_atr(atrnum), s = getstring_noalloc(f));
	else
	  getstring_noalloc(f);
      }
      else
      {
	atrobj = getref(f);
	if (atrobj == NOTHING)
	{
	  if (builtin_atr(atrnum) && !(builtin_atr(atrnum)->flags & AF_UNIMP))
	    atr_add(obj, builtin_atr(atrnum), s = getstring_noalloc(f));
	  else
	    getstring_noalloc(f);
	}
	else if (atrobj >= obj)
	{			/* ergh, haven't read it in yet. */
	  old_db_top = db_top;
	  db_grow(atrobj + 1);
	  db_top = old_db_top;
	  if (!db[atrobj].atrdefs)
	  {
//	    db[atrobj].atrdefs = malloc(sizeof(ATRDEF));
	    SAFE_MALLOC(db[atrobj].atrdefs, ATRDEF, 1);
	    db[atrobj].atrdefs->a.name = NULL;
	    db[atrobj].atrdefs->next = NULL;
	  }
	  for (atr = db[atrobj].atrdefs, i = 0;
                      atr->next && (i < atrnum);
	       atr = atr->next, i++)
	    ;			/* check to see if it's already there. */
	  while (i < atrnum)
	  {
//	    atr->next = malloc(sizeof(ATRDEF));
	    SAFE_MALLOC(atr->next, ATRDEF, 1);
	    atr->next->a.name = NULL;
	    atr->next->next = NULL;
	    atr = atr->next;
	    i++;
	  }
	  atr_add(obj, &(atr->a), s = getstring_noalloc(f));
	}
	else
	{
	  for (atr = db[atrobj].atrdefs, i = 0;
	       i < atrnum; atr = atr->next, i++) ;
	  atr_add(obj, &(atr->a), s = getstring_noalloc(f));
	}
      }
      break;
    case '<':			/* end of list */
      if ('\n' != fgetc(f))
      {
	log_error(tprintf("No line feed on object %ld", obj));
	return (0);
      }
      return (1);
    default:
      log_error(tprintf("Bad character %c on object %ld", c, obj));
      return (0);
    }
}

static object_flag_type upgrade_flags(version, player, flags)
int version;
dbref player;
object_flag_type flags;
{
  long type;
  int iskey, member, chown_ok, link_ok, iswizard;

  /* only modify version 1 */
  if (version > 1)
    return flags;

  /* record info from old bits */
  iskey = (flags & 0x8);	/* if was THING_KEY */
  link_ok = (flags & 0x20);	/* if was LINK_OK */
  chown_ok = (flags & 0x40000);	/* if was CHOWN_OK */
  member = (flags & 0x2000);	/* if player was a member */
  iswizard = (flags & 0x10);	/* old wizard bit flag */
  type = flags & 0x3;		/* yank out old object type */

  /* clear out old bits */
  flags &= ~TYPE_MASK;		/* set up for 4-bit encoding */
  flags &= ~THING_KEY;		/* clear out thing key bit */
  flags &= ~INHERIT_POWERS;	/* clear out new spec pwrs bit */
  flags &= ~CHOWN_OK;
  flags &= ~LINK_OK;

  /* put these bits in their new positions */
  flags |= (iskey) ? THING_KEY : 0;
  flags |= (link_ok) ? LINK_OK : 0;
  flags |= (chown_ok) ? CHOWN_OK : 0;
#define TYPE_GUEST 0x8
#define TYPE_TRIALPL 0x9
#define TYPE_MEMBER 0xA
#define TYPE_ADMIN 0xE
#define TYPE_DIRECTOR 0xF
  /* encode type under 4-bit scheme */
  /* nonplayers */
  if (type != 3)
  {
    flags |= type;
    if (iswizard)
      flags |= INHERIT_POWERS;
  }
  /* root */
  else if (player == 1)
    flags |= TYPE_DIRECTOR;
  /* wizards */
  else if (iswizard)
    flags |= TYPE_ADMIN;
  /* members */
  else if (member)
    flags |= TYPE_MEMBER;
  /* guests */
  else if ((flags & PLAYER_MORTAL))
  {
    flags &= ~PLAYER_MORTAL;
    flags |= TYPE_GUEST;
  }
  /* trial players */
  else
    flags |= TYPE_TRIALPL;
#undef TYPE_DIRECTOR
#undef TYPE_GUEST
#undef TYPE_TRIALPL
#undef TYPE_MEMBER
#undef TYPE_ADMIN
  return flags;
}
static void scramble_to_link()
{
  dbref i, j;

  for (i = 0; i < db_top; i++)
  {
    if (Typeof(i) == TYPE_ROOM || Typeof(i) == TYPE_EXIT)
    {
      db[i].link = db[i].location;
      db[i].location = i;
    }
    else if (Typeof(i) == TYPE_THING || Typeof(i) == TYPE_CHANNEL
#ifdef USE_UNIV
	     || Typeof(i) == TYPE_UNIVERSE
#endif
	     || Typeof(i) >= TYPE_PLAYER)
    {
      db[i].link = db[i].exits;
      db[i].exits = -1;
    }
  }
  for (i = 0; i < db_top; i++)
  {
    if (Typeof(i) == TYPE_ROOM)
      for (j = db[i].exits; j != NOTHING; j = db[j].next)
	db[j].location = i;
  }
}

static void db_check()		/* put quotas in! */
{
  dbref i;

  for (i = 0; i < db_top; i++)
    if (Typeof(i) == TYPE_PLAYER)
    {
      dbref j;
      int cnt = (-1);

      for (j = 0; j < db_top; j++)
	if (db[j].owner == i)
	  cnt++;
      atr_add(i, A_QUOTA, tprintf("%ld", atol(atr_get(i, A_RQUOTA)) + cnt));
    }
}

static int db_read_object P((dbref, FILE *));

static void count_atrdef_refcounts()
{
  int i;
  ATRDEF *k;
  ALIST *l;

  for (i = 0; i < db_top; i++)
    for (k = db[i].atrdefs; k; k = k->next)
    {
      k->a.refcount = 1;
    }
  for (i = 0; i < db_top; i++)
    for (l = db[i].list; l; l = AL_NEXT(l))
    {
      if (AL_TYPE(l))
	ref_atr(AL_TYPE(l));
    }
}

static FILE *db_read_file = NULL;

void db_set_read(f)
FILE *f;
{
  db_read_file = f;
}
int loading_db = 0;
static void run_startups()
{
  dbref i;
  struct descriptor_data *d;
  int do_startups = 1;

  for (d = descriptor_list; d; d = d->next)
    if (d->state == RELOADCONNECT)
      db[d->player].flags &= ~CONNECT;	/* so announce_disconnect here *
					   won't get run. */
  {
    FILE *f;

    f = fopen("nostartup", "r");
    if (f)
    {
      fclose(f);
      do_startups = 0;
    }
  }

  for (i = 0; i < db_top; i++)
  {
    if (*atr_get(i, A_STARTUP) && do_startups)
      parse_que(i, atr_get(i, A_STARTUP), i);
    if (db[i].flags & CONNECT)
      announce_disconnect(i);
#ifdef USE_COMABT
    init_skill(i);
#endif
  }
}

static void welcome_descriptors()
{
  struct descriptor_data *d;

  for (d = descriptor_list; d; d = d->next)
    if (d->state == RELOADCONNECT && d->player >= 0)
    {
      d->state = CONNECTED;
      db[d->player].flags |= CONNECT;
      queue_string(d, tprintf("%s %s", muse_name, ONLINE_MESSAGE));
    }
}

void load_more_db()
{
  static dbref i = NOTHING;
  int j;

  combat_list = NOTHING;

  if (loading_db)
    return;

  if (i == NOTHING)
  {
    clear_players();
    clear_channels();
    db_free();
    i = 0;
  }
  for (j = 0; j < 123 && i >= 0; j++, i++)
  {
    if ((i % 1000 == 1) && db_init)
    {
      struct descriptor_data *d;
      char buf[1024];

      sprintf(buf, "Now loading object #%ld of %ld.\n", (long)(i - 1), (long)(db_init * 2 / 3));
      for (d = descriptor_list; d; d = d->next)
      {
	queue_string(d, buf);
      }
    }
    i = db_read_object(i, db_read_file);
  }
  if (i == -2)
  {
    loading_db = 1;
    read_mail(db_read_file);
    read_loginstats();
    count_atrdef_refcounts();
    run_startups();
    welcome_descriptors();
    log_important(tprintf("|G+%s %s|", muse_name, ONLINE_MESSAGE));
    strcpy(motd,"Muse back online.");
    strcpy(motd_who,"#1");
    return;
  }
  if (i == -1)
  {
    log_error("Couldn't load database; shutting down the muse.");
    exit_nicely(136);
  }
}

int dozonetemp;

static int db_read_object(i, f)
dbref i;
FILE *f;
{
  int c;
  struct object *o;
  char *end;
  static int db_version = 1;	/* old db's default to v1 */

  c = getc(f);
  switch (c)
  {
    /* read version number */
  case '@':
    db_version = getref(f);
    if (db_version != DB_VERSION)
      log_important(tprintf("Converting DB from v%d to v%d",
			    db_version, DB_VERSION));
    break;

    /* make sure database is at least this big *1.5 */
  case '~':
    db_init = (getref(f) * 3) / 2;
    break;

    /* TinyMUSH old object storage definition */
  case '#':
    /* another entry, yawn */
    if (i != getref(f))
      return -2;		/* we blew it */

    /* make space */
    db_grow(i + 1);

    /* read it in */
    o = db + i;
    o->list = NULL;
    getstring(f, o->name);
    s_Desc(i, getstring_noalloc(f));
    o->location = getref(f);
    o->zone = NOTHING;
    o->contents = getref(f);
    o->exits = getref(f);
    o->fighting = getref(f);
    o->link = NOTHING;		/* atleast until we scramble_to_link */
    o->next = getref(f);
    o->next_fighting = NOTHING;
#ifdef USE_UNIV
    o->universe = NOTHING;
#endif
    if (o->fighting != NOTHING)
      if (combat_list == NOTHING)
	combat_list = o->fighting;
      else
      {
	o->next_fighting = combat_list;
	combat_list = i;
      }
    o->mod_time = o->create_time = 0;
    getboolexp(i, f);
    s_Fail(i, getstring_noalloc(f));
    s_Succ(i, getstring_noalloc(f));
    s_Ofail(i, getstring_noalloc(f));
    s_Osucc(i, getstring_noalloc(f));
    o->owner = getref(f);
    o->flags = getref(f);	/* temp storage for pennies */
    s_Pennies(i, o->flags);
    o->flags = upgrade_flags(db_version, i, getref(f));
    s_Pass(i, getstring_noalloc(f));
    o->atrdefs = 0;
    o->parents = NULL;
    o->children = NULL;
    o->atrdefs = NULL;
    /* check to see if it's a player */
    if (Typeof(i) == TYPE_PLAYER)
      add_player(i);
    /* now check for channel */
    else if (Typeof(i) == TYPE_CHANNEL)
      add_channel(i);
    break;

    /* TinyMUSH new version object storage definition */
  case '!':			/* non-zone oriented database */
  case '&':			/* zone oriented database */

    /* make space */
    i = getref(f);
    db_grow(i + 1);
    /* read it in */
    o = db + i;
    getstring(f, o->name);
    if (db_version < 14)
      SET(o->cname, o->name);
    else
      getstring(f, o->cname);
    o->location = getref(f);
    if (c == '!')
      o->zone = NOTHING;
    else if (db_version >= 3)
      o->zone = getref(f);
    o->contents = getref(f);
    o->exits = getref(f);
    if (db_version < 12)
      o->fighting = NOTHING;
    else
      o->fighting = getref(f);
    if (db_version < 5)
      o->link = NOTHING;
    else
      o->link = getref(f);
    o->next = getref(f);
    o->next_fighting = NOTHING;
    if (o->fighting != NOTHING)
      if (combat_list == NOTHING)
	combat_list = o->fighting;
      else
      {
	o->next_fighting = combat_list;
	combat_list = i;
      }
    o->list = NULL;
    if (db_version <= 8)
      getboolexp(i, f);
    o->owner = getref(f);
    if (db_version <= 3)
    {
      long int k;

      k = getref(f);
      s_Pennies(i, k);
    }

    o->flags = upgrade_flags(db_version, i, getref(f));
    if (db_version >= 10)
    {
      o->mod_time = getref(f);
      o->create_time = getref(f);
    }
    else
      o->mod_time = o->create_time = 0;
    if (db_version <= 10)
    {
      if (i == 0 && o->zone == NOTHING)
	/* ack. */
	log_error("No #0 zone.");
      else if (Typeof(i) == TYPE_ROOM && o->zone == NOTHING)
	o->zone = db[0].zone;
      else if (Typeof(i) != TYPE_ROOM)
	o->zone = NOTHING;
    }

    if (db_version >= 6)
      if (Typeof(i) == TYPE_PLAYER)
      {
	get_powers(i, getstring_noalloc(f));
      }
      else
      {
	if (db_version == 6)
	  get_powers(i, getstring_noalloc(f));
	o->pows = NULL;
      }
    else
    {
      o->pows = NULL;		/* we'll fiddle with it later. */
    }

    /* read attribute list for item */
    if (!get_list(f, i, db_version))
    {
      log_error(tprintf("Bad attribute list object %ld", i));
      return -2;
    }
    if (db_version > 7)
    {
      o->parents = getlist(f);
      o->children = getlist(f);
      o->atrdefs = get_atrdefs(f, o->atrdefs);
    }
    else
    {
      o->parents = NULL;
      o->children = NULL;
      o->atrdefs = NULL;
    }
#ifdef USE_UNIV
    if (db_version > 12)
      get_univ_info(f, o);
    else if (((o->flags & TYPE_MASK) == TYPE_UNIVERSE))
      init_universe(o);
#else
#if 0
    if (db_version > 12)
    {
      char deuniv_k, *deuniv_s;
      int deuniv_x;

      for (deuniv_x = 1; deuniv_x;)
      {
	deuniv_k = fgetc(f);
	switch (deuniv_k)
	{
	case '\\':
	  deuniv_x = 0;
	  break;
	case '>':
	  o->universe = getref(f);
	  break;
	case '/':
	  deuniv_s = getstring_noalloc(f);
	  break;
	default:
	  break;
	}
      }
    }
    if ((o->flags & TYPE_MASK) == TYPE_UNIVERSE)
      o->flags = TYPE_THING;
#endif
#endif

    /* check to see if it's a player */
    if (Typeof(i) == TYPE_PLAYER ||
	(db_version < 6 && Typeof(i) > TYPE_PLAYER))
    {				/* ack! */
      add_player(i);
    }
    else if (Typeof(i) == TYPE_CHANNEL)
    {
      add_channel(i);
    }
    break;
    break;

  case '*':
    end = getstring_noalloc(f);
    if (strcmp(end, "**END OF DUMP***"))
    {
      /* SAFE_FREE((void *) end); */
      log_error(tprintf("No end of dump %ld.", i));
      return -2;
    }
    else
    {
      extern void zero_free_list P((void));

      /* SAFE_FREE((void *) end); */
      log_important("Done loading database.");
      zero_free_list();
      db_check();
      if (db_version < 6)
      {
	dbref i;

	atr_add(root, A_QUEUE, "-999999");	/* hope we don't have enough
						   players to overflow that. */
	for (i = 0; i < db_top; i++)
	{
	  if ((db[i].flags & TYPE_MASK) >= TYPE_PLAYER)
	  {

	    do_class(root, stralloc(tprintf("#%ld", i)), class_to_name(old_to_new_class(db[i].flags & TYPE_MASK)));
	    db[i].flags &= ~TYPE_MASK;
	    db[i].flags |= TYPE_PLAYER;
//	    db[i].pows = malloc(2 * sizeof(ptype));
	    SAFE_MALLOC(db[i].pows, ptype, 2);
	    db[i].pows[1] = 0;	/* don't care about the [0]... */
	  }
	}
      }
      if (db_version <= 4)
	scramble_to_link();
      if (db_version <= 8)
	convert_boolexp();
      if (db_version <= 10 && db_version >= 3)
      {
	int j;

	/* make the zone heirarchy. */
	for (j = 0; j < db_top; j++)
	  if (db[j].zone != NOTHING && db[db[j].zone].zone == NOTHING)
	    db[db[j].zone].zone = db[0].zone;
	if (db[0].zone != NOTHING)
	  db[db[0].zone].zone = NOTHING;
      }
      return -3;
    }
  default:
    log_error(tprintf("Failed object %ld.", i));
    return -2;
  }
  return i;
}
static dbref *getlist(f)
FILE *f;
{
  dbref *op;
  int len;

  len = getref(f);
  if (len == 0)
    return NULL;
//  op = malloc(sizeof(dbref) * (len + 1));
  SAFE_MALLOC(op,dbref,len + 1);
  op[len] = NOTHING;
  for (len--; len >= 0; len--)
    op[len] = getref(f);
  return op;
}

static void putlist(f, list)
FILE *f;
dbref *list;
{
  int k;

  if ((!list) || (list[0] == NOTHING))
  {
    putref(f, 0);
    return;
  }
  for (k = 0; list[k] != NOTHING; k++) ;
  putref(f, k);
  for (k--; k >= 0; k--)
    putref(f, list[k]);
}

char *unparse_attr(atr, dep)
ATTR *atr;
int dep;
{
  static char buf[1000];

  buf[dep] = 0;
  if (dep)
    for (dep--; dep >= 0; dep--)
      buf[dep] = '+';
  if (atr->obj == NOTHING)
    strcat(buf, atr->name);
  else
    sprintf(buf + strlen(buf), "#%ld.%s", atr->obj, atr->name);
  return buf;
}

#define DOATTR(var, name, flags, num) ATTR *var;
#define DECLARE_ATTR
#include "attrib.h"
#undef DECLARE_ATTR
#undef DOATTR

void init_attributes()
{
#define DOATTR(var, name, flags, num) var = builtin_atr(num);
#include "attrib.h"
#undef DOATTR
}

dbref update_bytes_counter = (-1);
void update_bytes()
{
  int n;
  int difference;
  int newsize;

  if ((++update_bytes_counter) >= db_top)
    update_bytes_counter = 0;
  for (n = 100; n > 0; n--)
  {
    if (db[update_bytes_counter].i_flags & I_UPDATEBYTES)
      break;
    if ((++update_bytes_counter) >= db_top)
      update_bytes_counter = 0;
  }
  if (!(db[update_bytes_counter].i_flags & I_UPDATEBYTES))
    return;			/* couldn't find any. */

  newsize = mem_usage(update_bytes_counter);
  difference = newsize - db[update_bytes_counter].size;
  add_bytesused(db[update_bytes_counter].owner, difference);	/* this has to be done right here 
								   in the middle, because it 
								   calls recalc_bytes which may
								   reset size and I_UPDATEBYTES. 
								 */
  db[update_bytes_counter].size = newsize;
  db[update_bytes_counter].i_flags &= ~I_UPDATEBYTES;
}

void remove_temp_dbs()
{
  int i;

#ifdef DBCOMP
  system(tprintf("cp %s.#%ld# %s", def_db_out, epoch, def_db_out));
#endif

  for (i = 0; i < 3; i++)
    unlink(tprintf("%s.#%ld#", def_db_out, epoch - i));
}

#ifdef USE_UNIV
void get_univ_info(f, o)
FILE *f;
struct object *o;
{
  char k, *s, *i;

  if (((o->flags & TYPE_MASK) == TYPE_UNIVERSE))
  {
    init_universe(o);
  }

  for (;;)
  {
    k = fgetc(f);
    switch (k)
    {
    case '\\':
      if (fgetc(f) != '\n')
	log_error("No atrdef newline.");
      return;
      break;

    case '>':
      o->universe = getref(f);
      break;
    case '/':
      s = getstring_noalloc(f);
      i = strchr(s, ':');
      *(i++) = '\0';
      if ((atoi(s) < NUM_UA) && (atoi(s) > -1) &&
	  ((o->flags & TYPE_MASK) == TYPE_UNIVERSE))
      {
	switch (univ_config[atoi(s)].type)
	{
	case UF_BOOL:
	case UF_INT:
	  o->ua_int[atoi(s)] = atoi(i);
	  break;
	case UF_FLOAT:
	  o->ua_float[atoi(s)] = atof(i);
	  break;
	case UF_STRING:
	  o->ua_string[atoi(s)] = realloc(o->ua_string[atoi(s)],
					  (strlen(i) + 1) * sizeof(char));

	  strcpy(o->ua_string[atoi(s)], i);
	  break;
	}
      }
      break;
    default:
      break;
    }
  }
}
#endif

/*****************************************************XXX BRG DB CODE XXX*/
#define DB_LOGICAL 0x15

/* atr_fputs - needed to support %r substitution                *
 * does: outputs string <what> on stream <fp>, quoting newlines *
 * with a DB_LOGICAL (currently ctrl-U).                        *
 * added on 4/26/94 by Brian Gaeke (Roodler)                    */

void atr_fputs(char *what, FILE * fp)
{
  while (*what)
  {
    if (*what == '\n')
      fputc(DB_LOGICAL, fp);
    fputc(*what++, fp);
  }
}


/* new atr_fgets() by Bobby Newmark  08/15/2000 */
char *atr_fgets(char *buffer, int size, FILE * fp)
{
  char newbuf1[size];
  char *s = NULL;
  char  newbuf2[size];

  if (fgets(newbuf2, size, fp) != NULL) 
  {
    while ((strlen(newbuf2) > 1) && (newbuf2[strlen(newbuf2) - 2] == DB_LOGICAL))
    {
      s = newbuf2 + strlen(newbuf2) - 1;
      if(*s == '\n')
      {
        *s-- = '\0';
        *s = '\n';
        fgets(newbuf1, size - strlen(newbuf2), fp);
        strncat(newbuf2, newbuf1, (size - strlen(newbuf2)));
      }
      else
      {
    /* we must have an erroneous DB_LOGICAL. doh! */
        newbuf2[strlen(newbuf2) - 2] = newbuf2[strlen(newbuf2) - 1];
        newbuf2[strlen(newbuf2) - 1] = '\0';
      }
    } 
  /* make sure our string isn't too big *
    if (strlen(newbuf2) > size)
    {
      newbuf2[size] = '\0';
    }
     this shouldn't be a problem now. */
  
    strcpy(buffer, newbuf2);
  }
  else
  {
    buffer[0] = '\n';
  }
  return(buffer);
}

/* atr_fgets - needed to support %r substitution              *
 * does: inputs a string, max <size> chars, from stream <fp>, *
 * into buffer <buffer>. if a DB_LOGICAL is encountered,      *
 * the next character (possibly a \n) won't terminate the     *
 * string, as it would in fgets.                              *
 * added on 4/26/94 by Brian Gaeke (Roodler)                  *

char *atr_fgets(char *buffer, int size, FILE * fp)
{
  int num_read = 0;
  char ch, *mybuf = buffer;

  for (;;)
  {
    ch = getc(fp);
    if (ch == EOF)
      break;
    if (ch == DB_LOGICAL)
    {
      ch = getc(fp);
      *mybuf++ = ch;
      num_read++;
      continue;
    }
    if (ch == '\n')
    {
      *mybuf++ = ch;
      num_read++;
      break;
    }
    *mybuf++ = ch;
    num_read++;
    if (num_read > size)
      break;
  }
  *mybuf = '\0';
  return buffer;

}
* end BRG's old atr_fgets() code */

void putstring(f, s)
FILE *f;
char *s;
{
  if (s)
    atr_fputs(s, f);
  fputc('\n', f);
}
