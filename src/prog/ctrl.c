
/* $Id: ctrl.c,v 1.7 1994/02/18 22:45:48 nils Exp $ */
/* file containing programming commands. */

#include "config.h"
#include "externs.h"

void do_switch(player, exp, argv, cause)
dbref player;
char *exp;
char *argv[];
dbref cause;
{
  int any = 0, a, c;
  char buff[1024];
  char *ptrsrv[10];

  strcpy(buff, exp);
  if (!argv[1])
    return;
  for (c = 0; c < 10; c++)
    ptrsrv[c] = wptr[c];
  /* now try a wild card match of buff with stuff in coms */
  for (a = 1; (a < (MAX_ARG - 1)) && argv[a] && argv[a + 1]; a += 2)
    if (wild_match(argv[a], buff))
    {
      any = 1;
      for (c = 0; c < 10; c++)
	wptr[c] = ptrsrv[c];
      parse_que(player, argv[a + 1], cause);
    }
  for (c = 0; c < 10; c++)
    wptr[c] = ptrsrv[c];
  if ((a < MAX_ARG) && !any && argv[a])
    parse_que(player, argv[a], cause);
}

void do_foreach(player, list, command, cause)
dbref player;
char *list, *command;
dbref cause;
{
  char *k;
  char *ptrsrv[10];
  int c;

  for (c = 0; c < 10; c++)
    ptrsrv[c] = wptr[c];
  for (c = 0; c < 10; c++)
    wptr[c] = "";
  while ((k = parse_up(&list, ' ')))
  {
    wptr[0] = k;
    parse_que(player, command, cause);
  }
  for (c = 0; c < 10; c++)
    wptr[c] = ptrsrv[c];
}

void do_trigger(player, object, argv)
dbref player;
char *object;
char *argv[];
{
  dbref thing;
  ATTR *attrib;
  int a;

  if (!parse_attrib(player, object, &thing, &attrib, POW_SEEATR))
  {
    notify(player, "No match.");
    return;
  }
  if (!controls(player, thing, POW_MODIFY))
  {
    notify(player, perm_denied());
    return;
  }
  if (thing == root)
  {
    notify(player, "You can't trigger root.");
    return;
  }
  for (a = 0; a < 10; a++)
    wptr[a] = argv[a + 1];
  did_it(player, thing, NULL, 0, NULL, 0, attrib);
  if (!(db[player].flags & QUIET))
    notify(player,
	   tprintf("%s - Triggered.", db[thing].cname));
}

void do_trigger_as(player, object, argv)
dbref player;
char *object;
char *argv[];
{
  dbref thing;
  dbref cause;
  ATTR *attrib;
  int a;

  if (!argv[1] || !*argv[1] || ((cause = match_thing(player, argv[1])) == NOTHING))
    return;

  if (!parse_attrib(player, object, &thing, &attrib, POW_SEEATR))
  {
    notify(player, "No match.");
    return;
  }
  if (!controls(player, thing, POW_MODIFY))
  {
    notify(player, perm_denied());
    return;
  }
  if (thing == root)
  {
    notify(player, "You can't trigger root.");
    return;
  }
  for (a = 0; a < 9; a++)
    wptr[a] = argv[a + 2];
  did_it(cause, thing, NULL, 0, NULL, 0, attrib);
  if (!(db[player].flags & QUIET))
    notify(player,
	   tprintf("%s - Triggered.", db[thing].cname));
}

void do_decompile(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{
  dbref obj;
  int i;
  ALIST *a;
  ATRDEF *k;
  char buf[1024];
  char *s;

  obj = match_thing(player, arg1);
  if (obj == NOTHING)
    return;
  if ((!controls(player, obj, POW_SEEATR) || !controls(player, obj, POW_EXAMINE)) && !(db[obj].flags & SEE_OK))
  {
    notify(player, perm_denied());
    return;
  }
  s = flag_description(obj);
  if ((s = strchr(s, ':')) && (s = strchr(++s, ':')))
  {
    char *g;

    s += 2;
    while ((g = parse_up(&s, ' ')))
      notify(player, tprintf("@set %s=%s", (*arg2) ? arg2 : arg1, g));
  }

  if (db[obj].atrdefs)
  {
    for (k = db[obj].atrdefs; k; k = k->next)
    {
      sprintf(buf, "@defattr %s/%s%s",
	      (*arg2) ? arg2 : arg1,
	      k->a.name,
	      (k->a.flags) ? "=" : "");
      if (k->a.flags & AF_WIZARD)
	strcat(buf, " wizard");
      if (k->a.flags & AF_UNIMP)
	strcat(buf, " unsaved");
      if (k->a.flags & AF_OSEE)
	strcat(buf, " osee");
      if (k->a.flags & AF_INHERIT)
	strcat(buf, " inherit");
      if (k->a.flags & AF_DARK)
	strcat(buf, " dark");
      if (k->a.flags & AF_DATE)
	strcat(buf, " date");
      if (k->a.flags & AF_LOCK)
	strcat(buf, " lock");
      if (k->a.flags & AF_FUNC)
	strcat(buf, " function");
      notify(player, buf);
    }
  }

  for (i = 0; db[obj].parents && db[obj].parents[i] != NOTHING; i++)
    notify(player, tprintf("@addparent %s=#%ld",
			   (*arg2) ? arg2 : arg1, db[obj].parents[i]));

  for (a = db[obj].list; a; a = AL_NEXT(a))
    if (AL_TYPE(a))
      if (!(AL_TYPE(a)->flags & AF_UNIMP))
	if (can_see_atr(player, obj, AL_TYPE(a)))
	{
	  /* if ( AL_TYPE(a)->obj == NOTHING ) */
	  sprintf(buf, "%s", unparse_attr(AL_TYPE(a), 0));
	  /* else { sprintf(buf, "#%d", obj); sprintf(buf, "%s%s",
	     db[obj].name, unparse_attr(AL_TYPE(a),0)+strlen(buf)); } */
	  notify(player, tprintf("@nset %s=%s:%s", (*arg2) ? arg2 : arg1,
				 buf,
				 (AL_STR(a))));
	}
}

void do_cycle(player, arg1, argv)
dbref player;
char *arg1;
char **argv;
{
  int i;
  dbref thing;
  ATTR *attrib;
  char *curv;

  if (!parse_attrib(player, arg1, &thing, &attrib, POW_SEEATR))
  {
    notify(player, "No match.");
    return;
  }
  if (!argv[1])
  {
    notify(player, "You must specify an attribute.");
    return;
  }
  if (!can_set_atr(player, thing, attrib) || attrib == A_ALIAS)
  {
    notify(player, perm_denied());
    return;
  }
  curv = atr_get(thing, attrib);
  for (i = 1; i < 10 && argv[i]; i++)
    if (!string_compare(curv, argv[i]))
    {
      i++;
      if (!(db[player].flags & QUIET))
	notify(player, "Cycling...");
      if (i < 10 && argv[i])
	atr_add(thing, attrib, argv[i]);
      else
	atr_add(thing, attrib, argv[1]);
      return;
    }
  if (!(db[player].flags & QUIET))
    notify(player, "Defaulting to first in cycle.");
  atr_add(thing, attrib, argv[1]);
}
