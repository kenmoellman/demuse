
#include "config.h"
#include "externs.h"

#include <config.c>

void info_config(player)
dbref player;
{

  int i;

  notify(player, "  Permission denied messages:");
  for (i = 0; perm_messages[i]; i++)
  {
    notify(player, tprintf("    %s", perm_messages[i]));
  }

#define DO_NUM(str,var) notify(player,tprintf("  %-22s: %d",str,var));
#define DO_STR(str,var) notify(player,tprintf("  %-22s: %s",str,var));
#define DO_REF(str,var) notify(player,tprintf("  %-22s: #%ld", str, var));
#define DO_LNG(str,var) notify(player,tprintf("  %-22s: %ld",str,var));
#include "conf.h"
#undef DO_NUM
#undef DO_STR
#undef DO_REF
#undef DO_LNG
}

static void donum P((dbref, int *, char *));
static void donum(player, var, arg2)
dbref player;
int *var;
char *arg2;
{
  if (!strchr("-0123456789", *arg2))
  {
    notify(player, "Must be a number.");
    return;
  }
  (*var) = atoi(arg2);
  notify(player, "Set.");
}

static void dostr P((dbref, char **, char *));
static void dostr(player, var, arg2)
dbref player;
char **var;
char *arg2;
{
  if (!*arg2)
  {
    notify(player, "Must give new string.");
    return;
  }
  SET(*var, arg2);
  notify(player, "Set.");
}

static void doref P((dbref, dbref *, char *));
static void doref(player, var, arg2)
dbref player;
dbref *var;
char *arg2;
{
  dbref thing;

  thing = match_thing(player, arg2);
  if (thing == NOTHING)
    return;
  (*var) = thing;
  notify(player, "Set.");
}

static void dolng P((dbref, long *, char *));
static void dolng(player, var, arg2)
dbref player;
long *var;
char *arg2;
{
  if (!strchr("-0123456789", *arg2))
  {
    notify(player, "Must be a number.");
    return;
  }
  (*var) = atol(arg2);
  notify(player, "Set.");
}


void do_config(player, arg1, arg2)
dbref player;
char *arg1, *arg2;
{
  static int initted = 0;

  if (player != root)
  {
    notify(player, perm_denied());
    return;
  }
  if (!initted)
  {
    char *newv;

    /* we need to re-allocate strings so we can change them. */
#define DO_NUM(str,var) ;
#define DO_STR(str,var) newv=malloc(strlen(var)+1);strcpy(newv,var);var=newv;
#define DO_REF(str,var) ;
#define DO_LNG(str,var) ;
#include "conf.h"
#undef DO_NUM
#undef DO_STR
#undef DO_REF
#undef DO_LNG
    initted = 1;
  }
#define DO_NUM(str,var) if(!string_compare(arg1,str)) donum(player, &var, arg2); else
#define DO_STR(str,var) if(!string_compare(arg1,str)) dostr(player, &var, arg2); else
#define DO_REF(str,var) if(!string_compare(arg1,str)) doref(player, &var, arg2); else
#define DO_LNG(str,var) if(!string_compare(arg1,str)) dolng(player, &var, arg2); else
#include "conf.h"
#undef DO_NUM
#undef DO_STR
#undef DO_REF
#undef DO_LNG
  /* else */ notify(player, tprintf("no such config option: %s", arg1));
}
