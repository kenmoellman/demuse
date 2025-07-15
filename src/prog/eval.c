/* eval.c */
/* $Id: eval.c,v 1.16 1993/10/18 01:14:50 nils Exp $ */
/* Expression parsing module created by Lawrence Foard */

#include <sys/types.h>
#include <time.h>
#include <ctype.h>
#include <math.h>

#include "db.h"
#include "interface.h"
#include "config.h"
#include "externs.h"
#include "hash.h"

dbref match_thing(player, name)
dbref player;
char *name;
{
  init_match(player, name, NOTYPE);
  match_everything();

  return (noisy_match_result());
}

static void fun_rand(buff, args)
char *buff;
char *args[10];
{
  int mod = atoi(args[0]);

  if (mod < 1)
    mod = 1;
  sprintf(buff, "%d", (int)((random() & 65535) % mod));
}
static void fun_switch(buff, args, player, doer, nargs)
char *buff;
char *args[10];
dbref player;
dbref doer;
int nargs;
{
  char thing[1024];
  int i;
  char *ptrsrv[10];

  if (nargs < 2)
  {
    strcpy(buff, "WRONG NUMBER OF ARGS");
    return;
  }
  for (i = 0; i < 10; i++)
    ptrsrv[i] = wptr[i];
  strcpy(thing, args[0]);
  for (i = 1; (i + 1) < nargs; i += 2)
  {
    if (wild_match(args[i], thing))
    {
      strcpy(buff, args[i + 1]);
      for (i = 0; i < 10; i++)
	wptr[i] = ptrsrv[i];

      return;
    }
  }
  if (i < nargs)
    strcpy(buff, args[i]);
  else
    *buff = '\0';
  for (i = 0; i < 10; i++)
    wptr[i] = ptrsrv[i];
  return;
}

static void fun_attropts(buff, args, player, doer, nargs)
char *buff;
char *args[10];
dbref player;
dbref doer;
int nargs;
{
  dbref thing;
  char buf[1024];
  ATTR *attrib;

  if (nargs > 2 || nargs < 1)
  {
    strcpy(buff, "WRONG NUMBER OF ARGS");
    return;
  }
  if (nargs == 2)
  {
    char *k;
    static char *g[1];

    k = tprintf("%s/%s", args[0], args[1]);
    args = g;
    g[0] = k;
  }

  if (!parse_attrib(player, args[0], &thing, &attrib, 0))
  {
    strcpy(buff, "NO MATCH");
    return;
  }
  if (!can_see_atr(player, thing, attrib))
  {
    strcpy(buff, perm_denied());
    return;
  }
  *buf = '\0';
  buf[1] = '\0';
  if (attrib->flags & AF_WIZARD)
    strcat(buf, " Wizard");
  if (attrib->flags & AF_UNIMP)
    strcat(buf, " Unsaved");
  if (attrib->flags & AF_OSEE)
    strcat(buf, " Osee");
  if (attrib->flags & AF_INHERIT)
    strcat(buf, " Inherit");
  if (attrib->flags & AF_DARK)
    strcat(buf, " Dark");
  if (attrib->flags & AF_DATE)
    strcat(buf, " Date");
  if (attrib->flags & AF_LOCK)
    strcat(buf, " Lock");
  if (attrib->flags & AF_FUNC)
    strcat(buf, " Function");
  if (attrib->flags & AF_DBREF)
    strcat(buf, " Dbref");
  if (attrib->flags & AF_NOMEM)
    strcat(buf, " Nomem");
  if (attrib->flags & AF_HAVEN)
    strcat(buf, " Haven");
  strcpy(buff, buf + 1);
}

static void fun_foreach(buff, args, privs, doer)
char *buff;
char *args[10];
dbref privs;
dbref doer;
{
  char *k;
  char buff1[1024];
  char *ptrsrv;
  int i = 0, j = 0;

  ptrsrv = wptr[0];
  if (!args[0] || !strcmp(args[0], ""))
  {
    buff[0] = '\0';
    return;
  }
  while ((k = parse_up(&args[0], ' ')) && i < 1000)
  {
    wptr[0] = k;
    pronoun_substitute(buff1, doer, args[1], privs);
    for (j = strlen(db[doer].name) + 1; buff1[j] && i < 1000; i++, j++)
      buff[i] = buff1[j];
    buff[i++] = ' ';
    buff[i] = '\0';
  }
  if (i > 0)
    buff[i - 1] = '\0';
  wptr[0] = ptrsrv;
}

static void fun_get(buff, args, player, doer, nargs)
char *buff;
char *args[10];
dbref player;
dbref doer;
int nargs;
{
  dbref thing;
  ATTR *attrib;

  if (nargs > 2 || nargs < 1)
  {
    strcpy(buff, "WRONG NUMBER OF ARGS");
    return;
  }
  if (nargs == 2)
  {
    char *k;
    static char *g[1];

    k = tprintf("%s/%s", args[0], args[1]);
    args = g;
    g[0] = k;
  }

  if (!parse_attrib(player, args[0], &thing, &attrib, 0))
  {
    strcpy(buff, "NO MATCH");
    return;
  }
  if (can_see_atr(player, thing, attrib))
    strcpy(buff, atr_get(thing, attrib));
  else
    strcpy(buff, perm_denied());
  /* 
     if (!controls(player,thing,POW_SEEATR) && !(attrib->flags&AF_OSEE)) {
     strcpy(buff,"Permission denied"); return; } if (attrib->flags&AF_DARK && 

     !controls(player,attrib->obj,POW_SECURITY)) { strcpy(buff,"Permission
     denied"); return; } strcpy(buff,atr_get(thing,attrib)); */
}

static void fun_timedate(buff, args, privs, doer, nargs)
char *buff;
char *args[10];
dbref privs;
dbref doer;
int nargs;
{
  time_t cl;

  /* use supplied x-value if one is given */
  /* otherwise get the current x-value of time */
  if (nargs == 2)
    cl = atol(args[1]);
  else
    cl = now;

  if (nargs == 0)
    strcpy(buff, mktm(cl, "D", privs));
  else
    strcpy(buff, mktm(cl, args[0], privs));
}

static void fun_mstime(buff, args, privs, doer, nargs)
char *buff;
char *args[10];
dbref privs;
dbref doer;
int nargs;
{
  char tmp[64], mins1[8], secs1[8];
  int hour, mins, secs = 0;

  strcpy(tmp, mktm(now, "D", privs));

  strncpy(mins1, tmp + 14, 5);
  strncpy(secs1, tmp + 17, 5);
  hour = atoi(tmp + 11);
  mins = atoi(mins1);
  secs = atoi(secs1);
    
  sprintf(buff, "%02d:%02d:%02d", hour, mins, secs);

}

static void fun_mtime(buff, args, privs, doer, nargs)
char *buff;
char *args[10];
dbref privs;
dbref doer;
int nargs;
{
  char tmp[64], mins1[8];
  int hour, mins;

  strcpy(tmp, mktm(now, "D", privs));

  strncpy(mins1, tmp + 14, 5);
  hour = atoi(tmp + 11);
  mins = atoi(mins1);
  sprintf(buff, "%d:%d", hour, mins);

}

static void fun_time(buff, args, privs, doer, nargs)
char *buff;
char *args[10];
dbref privs;
dbref doer;
int nargs;
{
  char tmp[64], mins1[8];
  int hour, mins;

  strcpy(tmp, mktm(now, "D", privs));

  strncpy(mins1, tmp + 14, 5);
  hour = atoi(tmp + 11);
  mins = atoi(mins1);
  sprintf(buff, "%2d:%2d %cM", (hour > 12) ? hour - 12 : ((hour == 0) ? 12 : hour), mins, (hour > 11) ? 'P' : 'A');

}

static void fun_xtime(buff, args, privs, doer, nargs)
char *buff;
char *args[];
dbref privs;
dbref doer;
int nargs;
{
  time_t cl;

  if (nargs == 0)
    cl = now;
  else
  {
    cl = mkxtime(args[0], privs, (nargs > 1) ? args[1] : "");
    if (cl == -1L)
    {
      strcpy(buff, "#-1");
      return;
    }
  }
  sprintf(buff, "%ld", cl);
}

int mem_usage(thing)
dbref thing;
{
  int k;
  ALIST *m;
  ATRDEF *j;

  k = sizeof(struct object);

  k += strlen(db[thing].name) + 1;
  for (m = db[thing].list; m; m = AL_NEXT(m))
    if (AL_TYPE(m))
      if (AL_TYPE(m) != A_DOOMSDAY && AL_TYPE(m) != A_BYTESUSED && AL_TYPE(m) != A_IT)
      {
	k += sizeof(ALIST);
	if (AL_STR(m))
	  k += strlen(AL_STR(m));
      }
  for (j = db[thing].atrdefs; j; j = j->next)
  {
    k += sizeof(ATRDEF);
    if (j->a.name)
      k += strlen(j->a.name);
  }

  if (Typeof(thing) == TYPE_PLAYER)
    k += mail_size(thing);

  return k;
}

static void fun_objmem(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref thing;

  thing = match_thing(privs, args[0]);
  if (thing == NOTHING || !controls(privs, thing, POW_STATS))
  {
    strcpy(buff, "#-1");
    return;
  }
  sprintf(buff, "%d", mem_usage(thing));
}

static void fun_playmem(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  int tot = 0;
  dbref thing;
  dbref j;

  thing = match_thing(privs, args[0]);
  if (thing == NOTHING || !controls(privs, thing, POW_STATS) || !power(privs, POW_STATS))
  {
    strcpy(buff, "#-1");
    return;
  }
  for (j = 0; j < db_top; j++)
    if (db[j].owner == thing)
      tot += mem_usage(j);
  sprintf(buff, "%d", tot);
}

static void fun_mid(buff, args)
char *buff;
char *args[10];
{
  int l = atoi(args[1]), len = atoi(args[2]);

  if ((l < 0) || (len < 0) || (l > MAX_BUFF_LEN) || ((len+l) < 0) )
  {
    strcpy(buff, "OUT OF RANGE");
    return;
  }
  if (l < strlen(args[0]))
    strcpy(buff, args[0] + l);
  else
    *buff = 0;
  buff[len] = 0;
}

static void fun_add(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", atoi(args[0]) + atoi(args[1]));
}

static void fun_band(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", atoi(args[0]) & atoi(args[1]));
}

static void fun_bor(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", atoi(args[0]) | atoi(args[1]));
}

static void fun_bxor(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", atoi(args[0]) ^ atoi(args[1]));
}

static void fun_bnot(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", ~atoi(args[0]));
}

static int istrue(str)
char *str;
{
  return (((strcmp(str, "#-1") == 0) || (strcmp(str, "") == 0) ||
	   (strcmp(str, "#-2") == 0) ||
	   ((atoi(str) == 0) && isdigit(str[0]))) ? 0 : 1);
}

static void fun_land(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", istrue(args[0]) && istrue(args[1]));
}

static void fun_lor(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", istrue(args[0]) || istrue(args[1]));
}

static void fun_lxor(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", (istrue(args[0]) == 0 ? (istrue(args[1]) == 0 ? 0 : 1) :
		       (istrue(args[1]) == 0 ? 1 : 0)));
}

static void fun_lnot(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", (istrue(args[0]) == 0 ? 1 : 0));
}

static void fun_truth(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", (istrue(args[0]) ? 1 : 0));
}

static void fun_base(buff, args)
char *buff;
char *args[10];
{

  int i, digit, decimal, neg, oldbase, newbase;
  char tmpbuf[1000];

  oldbase = atoi(args[1]);
  newbase = atoi(args[2]);

  if ((oldbase < 2) || (oldbase > 36) || (newbase < 2) || (newbase > 36))
  {
    strcpy(buff, "BASES MUST BE BETWEEN 2 AND 36");
    return;
  }

  neg = 0;
  if (args[0][0] == '-')
  {
    neg = 1;
    args[0][0] = '0';
  }

  decimal = 0;
  for (i = 0; args[0][i] != 0; i++)
  {

    decimal *= oldbase;

    if (('0' <= args[0][i]) && (args[0][i] <= '9'))
      digit = args[0][i] - '0';
    else if (('a' <= args[0][i]) && (args[0][i] <= 'z'))
      digit = args[0][i] + 10 - 'a';
    else if (('A' <= args[0][i]) && (args[0][i] <= 'Z'))
      digit = args[0][i] + 10 - 'A';
    else
    {
      strcpy(buff, "ILLEGAL DIGIT");
      return;
    }

    if (digit >= oldbase)
    {
      strcpy(buff, "DIGIT OUT OF RANGE");
      return;
    }
    decimal += digit;
  }

  strcpy(buff, "");
  strcpy(tmpbuf, "");

  i = 0;
  while (decimal > 0)
  {
    strcpy(tmpbuf, buff);
    digit = (decimal % newbase);

    if (digit < 10)
      sprintf(buff, "%d%s", digit, tmpbuf);
    else
      sprintf(buff, "%c%s", digit + 'a' - 10, tmpbuf);

    decimal /= newbase;
    i++;
  }
  if (neg)
  {
    strcpy(tmpbuf, buff);
    sprintf(buff, "-%s", tmpbuf);
  }
}

static void fun_sgn(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", atoi(args[0]) > 0 ? 1 : atoi(args[0]) < 0 ? -1 : 0);
}

static void fun_sqrt(buff, args)
char *buff;
char *args[10];
{
  extern double sqrt P((double));
  int k;

  k = atoi(args[0]);
  if (k < 0)
    k = (-k);
  sprintf(buff, "%d", (int)sqrt((double)k));
}

static void fun_abs(buff, args)
char *buff;
char *args[10];
{
  extern int abs P((int));

  sprintf(buff, "%d", abs(atoi(args[0])));
}

static void fun_mul(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", atoi(args[0]) * atoi(args[1]));
}

static void fun_div(buff, args)
char *buff;
char *args[10];
{
  int bot = atoi(args[1]);

  if (bot == 0)
    bot = 1;
  sprintf(buff, "%d", atoi(args[0]) / bot);
}

static void fun_mod(buff, args)
char *buff;
char *args[10];
{
  int bot = atoi(args[1]);

  if (bot == 0)
    bot = 1;
  sprintf(buff, "%d", atoi(args[0]) % bot);
}

/* read first word from a string */
static void fun_first(buff, args)
char *buff;
char *args[10];
{
  char *s = args[0];
  char *b;

  /* get rid of leading space */
  while (*s && (*s == ' '))
    s++;
  b = s;
  while (*s && (*s != ' '))
    s++;
  *s++ = 0;
  strcpy(buff, b);
}

static void fun_rest(buff, args)
char *buff;
char *args[10];
{
  char *s = args[0];

  /* skip leading space */
  while (*s && (*s == ' '))
    s++;
  /* skip firsts word */
  while (*s && (*s != ' '))
    s++;
  /* skip leading space */
  while (*s && (*s == ' '))
    s++;
  strcpy(buff, s);
}

static void fun_flags(buff, args, privs, owner)
char *buff;
char *args[10];
dbref privs;
dbref owner;
{
  dbref thing;
  int oldflags;			/* really kludgy. */

  init_match(privs, args[0], NOTYPE);
  match_me();
  match_here();
  match_neighbor();
  match_absolute();
  match_possession();
  match_player();
  thing = match_result();

  if (thing == NOTHING || thing == AMBIGUOUS)
  {
    *buff = '\0';
    return;
  }

  /* 
     if ( ! controls(owner, thing, POW_FUNCTIONS) )  { *buff = '\0'; return;
     } */
  oldflags = db[thing].flags;	/* grr, this is kludgy. */
  if (!controls(privs, thing, POW_WHO) &&
      !could_doit(privs, thing, A_LHIDE))
    db[thing].flags &= ~(CONNECT);
  strcpy(buff, unparse_flags(thing));
  db[thing].flags = oldflags;
}

static void fun_modtime(buff, args, player)
char *buff;
char *args[10];
dbref player;
{
  dbref thing;

  thing = match_thing(player, args[0]);
  if (thing == NOTHING)
    strcpy(buff, "#-1");
  else
    sprintf(buff, "%ld", db[thing].mod_time);
}

static void fun_ctime(buff, args, player)
char *buff;
char *args[10];
dbref player;
{
  dbref thing;

  thing = match_thing(player, args[0]);
  if (thing == NOTHING)
    strcpy(buff, "#-1");
  else
    sprintf(buff, "%ld", db[thing].create_time);
}

static void fun_credits(buff, args, player)
char *buff;
char *args[10];
dbref player;
{
  dbref who;

  init_match(player, args[0], TYPE_PLAYER);
  match_me();
  match_player();
  match_neighbor();
  match_absolute();
  if ((who = match_result()) == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }

  if (!power(player, POW_FUNCTIONS) && !controls(player, who, POW_FUNCTIONS))
  {
    strcpy(buff, "#-1");
    return;
  }

  sprintf(buff, "%ld", Pennies(who));
}

static void fun_quota_left(buff, args, player)
char *buff;
char *args[10];
dbref player;
{
  dbref who;

  init_match(player, args[0], TYPE_PLAYER);
  match_me();
  match_player();
  match_neighbor();
  match_absolute();
  if ((who = match_result()) == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }

  if (!controls(player, who, POW_FUNCTIONS))
  {
    strcpy(buff, "#-1");
    return;
  }

  sprintf(buff, "%d", atoi(atr_get(who, A_RQUOTA)));
}

static void fun_quota(buff, args, player)
char *buff;
char *args[10];
dbref player;
{
  dbref who;

  init_match(player, args[0], TYPE_PLAYER);
  match_me();
  match_player();
  match_neighbor();
  match_absolute();
  if ((who = match_result()) == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }

  if (!controls(player, who, POW_FUNCTIONS))
  {
    strcpy(buff, "#-1");
    return;
  }

  /* count up all owned objects */
  /* owned = -1;  * a player is never included in his own quota */
  /* for ( thing = 0; thing < db_top; thing++ )  { if ( db[thing].owner ==
     who ) if ((db[thing].flags & (TYPE_THING|GOING)) != (TYPE_THING|GOING))
     ++owned; } */
  strcpy(buff, atr_get(who, A_QUOTA));
  /* sprintf(buff,"%d", (owned + atoi(atr_get(who, A_RQUOTA)))); */
}

static void fun_strlen(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", (int)strlen(args[0]));
}

static void fun_comp(buff, args)
char *buff;
char *args[10];
{
  int x;

  x = (atoi(args[0]) - atoi(args[1]));
  if (x > 0)
    strcpy(buff, "1");
  else if (x < 0)
    strcpy(buff, "-1");
  else
    strcpy(buff, "0");
}

static void fun_scomp(buff, args)
char *buff;
char *args[10];
{
  int x;

  x = strcmp(args[0], args[1]);
  if (x > 0)
    strcpy(buff, "1");
  else if (x < 0)
    strcpy(buff, "-1");
  else
    strcpy(buff, "0");
}

/* handle 0-9,va-vz,n,# */
static void fun_v(buff, args, privs, doer)
char *buff;
char *args[10];
dbref privs;
dbref doer;
{
  int c;

  if (args[0][0] && args[0][1])
  {
    /* not a null or 1-character string. */
    ATTR *attrib;

    if (!(attrib = atr_str(privs, privs, args[0])) || !can_see_atr(privs, privs, attrib))
    {
      *buff = '\0';
      return;
    }
    strcpy(buff, atr_get(privs, attrib));
    return;
  }
  switch (c = args[0][0])
  {
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    if (!wptr[c - '0'])
    {
      *buff = 0;
      return;
    }
    strcpy(buff, wptr[c - '0']);
    break;
  case 'v':
  case 'V':
    {
      int a;

      a = to_upper(args[0][1]);
      if ((a < 'A') || (a > 'Z'))
      {
	*buff = 0;
	return;
      }
      strcpy(buff, atr_get(privs, A_V[a - 'A']));
    }
    break;
  case 'n':
  case 'N':
    strcpy(buff, strip_color(safe_name(doer)));
    break;
  case 'c':
  case 'C':
    strcpy(buff, safe_name(doer));
    break;
  case '#':
    sprintf(buff, "#%ld", doer);
    break;
    /* info about location and stuff */
    /* objects # */
  case '!':
    sprintf(buff, "#%ld", privs);
    break;
  default:
    *buff = '\0';
    break;
  }
}

static void fun_s(buff, args, privs, doer)
char *buff;
char *args[10];
dbref privs;
dbref doer;
{
  char buff1[1024];

  pronoun_substitute(buff1, doer, args[0], privs);
  strcpy(buff, buff1 + strlen(db[doer].name) + 1);
}

static void fun_s_with(buff, args, privs, doer, nargs)
char *buff;
char *args[10];
dbref privs;
dbref doer;
int nargs;
{
  char buff1[1024];
  char *tmp[10];
  int a;

  if (nargs < 1)
  {
    strcpy(buff, "#-1");
    return;
  }

  for (a = 0; a < 10; a++)
    tmp[a] = wptr[a];

  wptr[9] = 0;
  for (a = 1; a < 10; a++)
    wptr[a - 1] = args[a];

  for (a = nargs; a < 10; a++)
    wptr[a - 1] = 0;

  pronoun_substitute(buff1, doer, args[0], privs);
  strcpy(buff, buff1 + strlen(db[doer].name) + 1);

  for (a = 0; a < 10; a++)
    wptr[a] = tmp[a];
}

static void fun_s_as(buff, args, privs, doer)
char *buff;
char *args[10];
dbref privs;
dbref doer;
{
  char buff1[1024];
  dbref newprivs;
  dbref newdoer;

  newdoer = match_thing(privs, args[1]);
  newprivs = match_thing(privs, args[2]);

  if (newdoer == NOTHING || newprivs == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }

  if (!controls(privs, newprivs, POW_MODIFY))
  {
    strcpy(buff, perm_denied());
    return;
  }

  pronoun_substitute(buff1, newdoer, args[0], newprivs);
  strcpy(buff, buff1 + strlen(db[newdoer].name) + 1);
}

static void fun_s_as_with(buff, args, privs, doer, nargs)
char *buff;
char *args[10];
dbref privs;
dbref doer;
int nargs;
{
  char buff1[1024];
  char *tmp[10];
  int a;
  dbref newprivs;
  dbref newdoer;

  if (nargs < 3)
  {
    strcpy(buff, "#-1");
    return;
  }

  newdoer = match_thing(privs, args[1]);
  newprivs = match_thing(privs, args[2]);
  if (newdoer == NOTHING || newprivs == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }

  if (!controls(privs, newprivs, POW_MODIFY))
  {
    strcpy(buff, perm_denied());
    return;
  }
  for (a = 0; a < 10; a++)
    tmp[a] = wptr[a];

  wptr[9] = wptr[8] = wptr[7] = 0;
  for (a = 3; a < 10; a++)
    wptr[a - 3] = args[a];

  for (a = nargs; a < 10; a++)
    wptr[a - 3] = 0;

  pronoun_substitute(buff1, newdoer, args[0], newprivs);
  strcpy(buff, buff1 + strlen(db[newdoer].name) + 1);

  for (a = 0; a < 10; a++)
    wptr[a] = tmp[a];
}

static void fun_num(buff, args, privs)
char *buff;
char *args[];
dbref privs;
{
  sprintf(buff, "#%ld", match_thing(privs, args[0]));
}

static void fun_con(buff, args, privs, doer)
char *buff;
char *args[];
dbref privs;
dbref doer;
{
  dbref it = match_thing(privs, args[0]);

  if ((it != NOTHING) && (controls(privs, it, POW_FUNCTIONS) ||
			  (db[privs].location == it) || (it == doer)))
  {
    sprintf(buff, "#%ld", db[it].contents);
    return;
  }
  strcpy(buff, "#-1");
  return;
}

/* return next exit that is ok to see */
static dbref next_exit(player, this)
dbref player;
dbref this;
{
  while ((this != NOTHING) &&
	 (db[this].flags & DARK) && !controls(player, this, POW_FUNCTIONS))
    this = db[this].next;
  return (this);
}

static void fun_exit(buff, args, privs, doer)
char *buff;
char *args[];
dbref privs;
dbref doer;
{
  dbref it = match_thing(privs, args[0]);

  if ((it != NOTHING) && (controls(privs, it, POW_FUNCTIONS) ||
			  (db[privs].location == it) || (it == doer)))
  {
    sprintf(buff, "#%ld", next_exit(privs,
				   db[it].exits));
    return;
  }
  strcpy(buff, "#-1");
  return;
}

static void fun_next(buff, args, privs, doer)
char *buff;
char *args[];
dbref privs;
dbref doer;
{
  dbref it = match_thing(privs, args[0]);

  if (it != NOTHING)
  {
    if (Typeof(it) != TYPE_EXIT)
    {
      if (controls(privs, db[it].location, POW_FUNCTIONS) ||
       (db[it].location == doer) || (db[it].location == db[privs].location))
      {
	sprintf(buff, "#%ld", db[it].next);
	return;
      }
    }
    else
    {
      sprintf(buff, "#%ld", next_exit(privs, db[it].next));
      return;
    }
  }
  strcpy(buff, "#-1");
  return;
}

static void fun_loc(buff, args, privs, doer)
char *buff;
char *args[];
dbref privs;
dbref doer;
{
  dbref it = match_thing(privs, args[0]);

  if ((it != NOTHING) &&
      (controls(privs, it, POW_FUNCTIONS) ||
       controls(privs, db[it].location, POW_FUNCTIONS) ||
       controls_a_zone(privs, it, POW_FUNCTIONS) ||
       power(privs, POW_FUNCTIONS) ||
       (it == doer) ||
       (Typeof(it) == TYPE_PLAYER && !(db[it].flags & DARK))))
  {
    sprintf(buff, "#%ld", db[it].location);
    return;
  }
  strcpy(buff, "#-1");
  return;
}

static void fun_link(buff, args, privs, doer)
char *buff;
char *args[];
dbref privs;
dbref doer;
{
  dbref it = match_thing(privs, args[0]);

  if ((it != NOTHING) &&
      (controls(privs, it, POW_FUNCTIONS) ||
       controls(privs, db[it].location, POW_FUNCTIONS) || (it == doer)))
  {
    sprintf(buff, "#%ld", db[it].link);
    return;
  }
  strcpy(buff, "#-1");
  return;
}

static void fun_linkup(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref it = match_thing(privs, args[0]);
  dbref i;
  int len = 0;

  if (!(controls(privs, it, POW_FUNCTIONS) ||
	controls(privs, db[it].location, POW_FUNCTIONS) || (it == privs)))
  {
    strcpy(buff, "#-1");
    return;
  }
  *buff = '\0';
  for (i = 0; i < db_top; i++)
    if (db[i].link == it)
      if (len)
      {
	static char smbuf[30];

	sprintf(smbuf, " #%ld", i);
	if ((strlen(smbuf) + len) > 990)
	{
	  strcat(buff, " #-1");
	  return;
	}
	strcat(buff, smbuf);
	len += strlen(smbuf);
      }
      else
      {
	sprintf(buff, "#%ld", i);
	len = strlen(buff);
      }
}

static void fun_class(buff, args, privs)
char *buff;
char *args[];
dbref privs;
{
  dbref it = match_thing(privs, args[0]);

  if (it == NOTHING)
    *buff = '\0';
  else
    sprintf(buff, "%s", get_class(it));
}

static void fun_is_a(buff, args, privs)
char *buff;
char *args[];
dbref privs;
{
  dbref thing, parent;

  thing = match_thing(privs, args[0]);
  parent = match_thing(privs, args[1]);
  if (thing == NOTHING || parent == NOTHING)
    strcpy(buff, "#-1");
  else
    strcpy(buff, is_a(thing, parent) ? "1" : "0");
}

static void fun_has(buff, args, privs)
char *buff;
char *args[];
dbref privs;
{
  dbref user, obj;
  dbref i;

  user = match_thing(privs, args[0]);
  obj = match_thing(privs, args[1]);
  if (obj == NOTHING || user == NOTHING)
    strcpy(buff, "#-1");
  else
  {
    strcpy(buff, "0");
    for (i = db[user].contents; i != NOTHING; i = db[i].next)
      if (i == obj)
	strcpy(buff, "1");
  }
}

static void fun_has_a(buff, args, privs)
char *buff;
char *args[];
dbref privs;
{
  dbref user, parent;
  dbref i;

  user = match_thing(privs, args[0]);
  parent = match_thing(privs, args[1]);
  if (parent == NOTHING || user == NOTHING)
    strcpy(buff, "#-1");
  else
  {
    strcpy(buff, "0");
    for (i = db[user].contents; i != NOTHING; i = db[i].next)
      if (is_a(i, parent))
	strcpy(buff, "1");
  }
}

static void fun_owner(buff, args, privs)
char *buff;
char *args[];
dbref privs;
{
  dbref it = match_thing(privs, args[0]);

  if (it != NOTHING)
    it = db[it].owner;
  sprintf(buff, "#%ld", it);
}

static void fun_name(buff, args, privs)
char *buff;
char *args[];
dbref privs;
{
  dbref it = match_thing(privs, args[0]);

  if (it == NOTHING)
    strcpy(buff, "");
  else if (Typeof(it) == TYPE_EXIT)
    strcpy(buff, strip_color(main_exit_name(it)));
  else
    strcpy(buff, db[it].name);
}

static void fun_cname(buff, args, privs)
char *buff;
char *args[];
dbref privs;
{
  dbref it = match_thing(privs, args[0]);

  if (it == NOTHING)
    strcpy(buff, "");
  else if (Typeof(it) == TYPE_EXIT)
    strcpy(buff, main_exit_name(it));
  else
    strcpy(buff, db[it].cname);
}

static void fun_pos(buff, args)
char *buff;
char *args[10];
{
  int i = 1;
  char *t, *u, *s = args[1];

  while (*s)
  {
    u = s;
    t = args[0];
    while (*t && *t == *u)
      ++t, ++u;
    if (*t == '\0')
    {
      sprintf(buff, "%d", i);
      return;
    }

    ++i, ++s;
  }

  strcpy(buff, "0");
  return;
}

static void fun_delete(buff, args)
char *buff;
char *args[10];
{
  char *s = buff, *t = args[0];
  int i, l = atoi(args[1]), len = atoi(args[2]);
  int a0len = strlen(args[0]);

  if ((l < 0) || (len < 0) || (len + l >= 1000))
  {
    strcpy(buff, "OUT OF RANGE");
    return;
  }
  for (i = 0; i < l && *s; i++)
    *s++ = *t++;
  if (len + l >= a0len)
  {
    *s = '\0';
    return;
  }
  t += len;
  while ((*s++ = *t++)) ;
  return;
}

static void fun_remove(buff, args)
char *buff;
char *args[10];
{
  char *s = buff, *t = args[0];
  int w = atoi(args[1]), num = atoi(args[2]), i;

  if (w < 1)
  {
    strcpy(buff, "OUT OF RANGE");
    return;
  }
  for (i = 1; i < w && *t; i++)
  {
    while (*t && *t != ' ')
      *s++ = *t++;
    while (*t && *t == ' ')
      *s++ = *t++;
  }
  for (i = 0; i < num && *t; i++)
  {
    while (*t && *t != ' ')
      t++;
    while (*t && *t == ' ')
      t++;
  }
  if (!*t)
  {
    if (s != buff)
      s--;
    *s = '\0';
    return;
  }
  while ((*s++ = *t++)) ;
  return;
}

static void fun_match(buff, args)
char *buff;
char *args[10];
{
  int a;
  int wcount = 1;
  char *s = args[0];
  char *ptrsrv[10];

  for (a = 0; a < 10; a++)
    ptrsrv[a] = wptr[a];

  do
  {
    char *r;

    /* trim off leading space */
    while (*s && (*s == ' '))
      s++;
    r = s;

    /* scan to next space and truncate if necessary */
    while (*s && (*s != ' '))
      s++;
    if (*s)
      *s++ = 0;

    if (wild_match(args[1], r))
    {
      sprintf(buff, "%d", wcount);
      for (a = 0; a < 10; a++)
	wptr[a] = ptrsrv[a];
      return;
    }
    wcount++;
  }

  while (*s)
  ;

  strcpy(buff, "0");

  for (a = 0; a < 10; a++)
    wptr[a] = ptrsrv[a];
}

static void fun_extract(buff, args)
char *buff;
char *args[10];
{
  int start = atoi(args[1]), len = atoi(args[2]);
  char *s = args[0], *r;

  if ((start < 1) || (len < 1))
  {
    *buff = 0;
    return;
  }
  start--;
  while (start && *s)
  {
    while (*s && (*s == ' '))
      s++;
    while (*s && (*s != ' '))
      s++;
    start--;
  }
  while (*s && (*s == ' '))
    s++;
  r = s;
  while (len && *s)
  {
    while (*s && (*s == ' '))
      s++;
    while (*s && (*s != ' '))
      s++;
    len--;
  }
  *s = 0;
  strcpy(buff, r);
}

/*  this is pete's new extract function.  */
/*

   static void fun_extract(buff,args)
   char *buff;
   char *args[10];
   {
   int start=atoi(args[1]),len=atoi(args[2]);
   char *s=args[0],*r;
   if ((start<1) || (len<1)) {
   *buff=0;
   return;
   }
   start--;
   while(start && *s) {
   while(*s && (*s==' '))
   s++;
   while(*s && (*s!=' '))
   s++;
   start--;
   }
   while(*s && (*s==' '))
   s++;
   r=s;
   while(len && *s) {
   while(*s && (*s==' '))
   s++;
   while(*s && (*s!=' '))
   s++;
   len--;
   }
   *s=0;
   strcpy(buff,r);
   }
 */
static void fun_parents(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref it;
  int i;

  it = match_thing(privs, args[0]);

  if (it == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }

  *buff = '\0';

  for (i = 0; db[it].parents && db[it].parents[i] != NOTHING; i++)
    if (controls(privs, it, POW_EXAMINE) || controls(privs, it, POW_FUNCTIONS)
	|| controls(privs, db[it].parents[i], POW_EXAMINE) ||
	controls(privs, db[it].parents[i], POW_FUNCTIONS))
      if (*buff)
	sprintf(buff + strlen(buff), " #%ld", db[it].parents[i]);
      else
	sprintf(buff, "#%ld", db[it].parents[i]);
}

static void fun_universe(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref it;

  it = match_thing(privs, args[0]);

  if (it == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }

#ifdef USE_UNIV
  sprintf(buff, "#%ld", db[get_zone_first(it)].universe);
#else
  strcpy(buff, "#-1");
#endif
}

static void fun_uinfo(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref thing;
  int x, flag;

  thing = match_thing(privs, args[0]);

  if (thing == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }

  if (Typeof(thing) != TYPE_UNIVERSE)
  {
    strcpy(buff, "#-1");
    return;
  }

#ifdef USE_UNIV
  for (flag = 1, x = 0; ((x < NUM_UA) && flag); x++)
  {
    if (!strcasecmp(univ_config[x].label, args[1]))
    {
      switch (univ_config[x].type)
      {
      case UF_BOOL:
	sprintf(buff, "%s", db[thing].ua_int[x] ? "Yes" : "No");
	break;
      case UF_INT:
	sprintf(buff, "%d", db[thing].ua_int[x]);
	break;
      case UF_FLOAT:
	sprintf(buff, "%f", db[thing].ua_float[x]);
	break;
      case UF_STRING:
	sprintf(buff, "%s", db[thing].ua_string[x]);
	break;
      default:
	strcpy(buff, "#-1");
	break;
      }
      flag = 0;
    }
  }
  if (flag)
    strcpy(buff, "#-1");
#else
  strcpy(buff, "#-1");
#endif
}

static void fun_children(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref it;
  int i;

  it = match_thing(privs, args[0]);

  if (it == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }

  *buff = '\0';

  for (i = 0; db[it].children && db[it].children[i] != NOTHING; i++)
    if (controls(privs, it, POW_EXAMINE) || controls(privs, it, POW_FUNCTIONS)
	|| controls(privs, db[it].children[i], POW_EXAMINE) ||
	controls(privs, db[it].children[i], POW_FUNCTIONS))
      if (*buff)
      {
	sprintf(buff + strlen(buff), " #%ld", db[it].children[i]);
	buff[990] = '\0';
      }
      else
	sprintf(buff, "#%ld", db[it].children[i]);
}

static void fun_zone(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref thing;

  thing = match_thing(privs, args[0]);
  if (thing == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }
  sprintf(buff, "#%ld", db[thing].zone);
}

static void fun_getzone(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref thing;

  thing = match_thing(privs, args[0]);
  if (thing == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }
  sprintf(buff, "#%ld", get_zone_first(thing));
}

static void fun_wmatch(buff, args)
char *buff;
char *args[10];
{
  char *string = args[0], *word = args[1], *s, *t;
  int count = 0, done = 0;

  for (s = string; *s && !done; s++)
  {
    count++;
    while (isspace(*s) && *s)
      s++;
    t = s;
    while (!isspace(*t) && *t)
      t++;
    done = (!*t) ? 1 : 0;
    *t = '\0';
    if (!string_compare(s, word))
    {
      sprintf(buff, "%d", count);
      return;
    }
    s = t;
  }
  sprintf(buff, "0");
  return;
}

static void fun_inzone(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref it = match_thing(privs, args[0]);
  dbref i;
  int len = 0;

  if (!controls(privs, it, POW_EXAMINE))
  {
    strcpy(buff, "#-1");
    return;
  }
  *buff = '\0';
  for (i = 0; i < db_top; i++)
    if (Typeof(i) == TYPE_ROOM)
      if (is_in_zone(i, it))
	if (len)
	{
	  static char smbuf[30];	/* eek, i hope it's not gunna be this 

					   long */
	  sprintf(smbuf, " #%ld", i);
	  if ((strlen(smbuf) + len) > 990)
	  {
	    strcat(buff, " #-1");
	    return;
	  }
	  strcat(buff, smbuf);
	  len += strlen(smbuf);
	}
	else
	{
	  sprintf(buff, "#%ld", i);
	  len = strlen(buff);
	}
}

static void fun_zwho(buff, args, who)
char *buff;
char *args[10];
dbref who;
{
  dbref it = match_thing(who, args[0]);
  dbref i;
  int len = 0;

  if (!controls(who, it, POW_FUNCTIONS))
  {
    strcpy(buff, "#-1");
    return;
  }
  *buff = '\0';
  for (i = 0; i < db_top; i++)
    if (Typeof(i) == TYPE_PLAYER)
      if (is_in_zone(i, it))
	if (len)
	{
	  static char smbuf[30];	/* eek, i hope it's not gunna be this 

					   long */
	  sprintf(smbuf, " #%ld", i);
	  if ((strlen(smbuf) + len) > 990)
	  {
	    strcat(buff, " #-1");
	    return;
	  }
	  strcat(buff, smbuf);
	  len += strlen(smbuf);
	}
	else
	{
	  sprintf(buff, "#%ld", i);
	  len = strlen(buff);
	}
}

static void fun_objlist(buff, args, privs, doer)
char *buff;
char *args[10];
dbref privs;
dbref doer;
{
  dbref it = match_thing(privs, args[0]);

  *buff = '\0';
  if (it == NOTHING)
    return;
  if (Typeof(it) != TYPE_EXIT)
    if (!(controls(privs, db[it].location, POW_FUNCTIONS) ||
	  (db[it].location == doer) ||
	  (db[it].location == db[privs].location) ||
	  (db[it].location == privs)))
      return;
  while (it != NOTHING)
  {
    if (*buff)
      strcpy(buff, tprintf("%s #%ld", buff, it));
    else
      sprintf(buff, "#%ld", it);
    it = (Typeof(it) == TYPE_EXIT) ? next_exit(privs, db[it].next) : db[it].next;
  }
}

static void fun_lzone(buff, args, privs, doer)
char *buff;
char *args[10];
dbref privs;
dbref doer;
{
  dbref it = match_thing(privs, args[0]);
  int depth = 10;

  *buff = '\0';
  if (it == NOTHING)
    return;
  it = get_zone_first(it);
  while (it != NOTHING)
  {
    if (*buff)
      strcpy(buff, tprintf("%s #%ld", buff, it));
    else
      sprintf(buff, "#%ld", it);
    it = get_zone_next(it);
    depth--;
    if (depth <= 0)
      return;
  }
}

static void fun_strcat(buff, args)
char *buff;
char *args[10];
{
  strcpy(buff, tprintf("%s%s", args[0], args[1]));
}

static void fun_controls(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref player, object;
  ptype power;

  player = match_thing(privs, args[0]);
  object = match_thing(privs, args[1]);
  power = name_to_pow(args[2]);

  if (player == NOTHING || object == NOTHING || !power)
  {
    strcpy(buff, "#-1");
    return;
  }

  sprintf(buff, "%d", controls(player, object, power));
}

static void fun_entrances(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref it = match_thing(privs, args[0]);
  dbref i;
  int control_here;

  *buff = '\0';

  if (it == NOTHING)
    strcpy(buff, "#-1");
  else
  {
    control_here = controls(privs, it, POW_EXAMINE);
    for (i = 0; i < db_top; i++)
      if (Typeof(i) == TYPE_EXIT && db[i].link == it)
	if (controls(privs, i, POW_FUNCTIONS) || controls(privs, i, POW_EXAMINE) || control_here)
	  if (*buff)
	    strcpy(buff, tprintf("%s #%ld", buff, i));
	  else
	    sprintf(buff, "#%ld", i);
  }
}

static void fun_fadd(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%f", atof(args[0]) + atof(args[1]));
}
static void fun_fsub(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%f", atof(args[0]) - atof(args[1]));
}

static void fun_sub(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%d", atoi(args[0]) - atoi(args[1]));
}

static void fun_fmul(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%f", atof(args[0]) * atof(args[1]));
}
static void fun_fdiv(buff, args)
char *buff;
char *args[10];
{
  if (atof(args[1]) == 0)
    sprintf(buff, "Undefined");
  else
    sprintf(buff, "%f", atof(args[0]) / atof(args[1]));
}
static void fun_fsgn(buff, args)
char *buff;
char *args[10];
{
  if (atof(args[0]) < 0)
    sprintf(buff, "-1");
  else if (atof(args[0]) > 0)
    sprintf(buff, "1");
  else
    strcpy(buff, "0");
}
static void fun_fsqrt(buff, args)
char *buff;
char *args[10];
{
  if (atof(args[0]) < 0)
    sprintf(buff, "Complex");
  else
    sprintf(buff, "%f", sqrt(atof(args[0])));
}

static void fun_fabs(buff, args)
char *buff;
char *args[10];
{
  if (atof(args[0]) < 0)
    sprintf(buff, "%f", -atof(args[0]));
  else
    strcpy(buff, args[0]);
}
static void fun_fcomp(buff, args)
char *buff;
char *args[10];
{
  char buf[90];
  char *k = buf;

  sprintf(buf, "%f", atof(args[0]) - atof(args[1]));
  fun_fsgn(buff, &k);
}

static void fun_exp(buff, args)
char *buff;
char *args[10];
{
  if ((atof(args[0]) > 55) || (atof(args[0]) < -55))
    sprintf(buff, "Overflow");
  else
    sprintf(buff, "%f", exp(atof(args[0])));
}
static void fun_pow(buff, args)
char *buff;
char *args[10];
{
  float num = 0;

  if (atof(args[0]) < 0)
    num = floor(atof(args[1]));
  else
    num = atof(args[1]);

  if (num > (54.758627264 / log(fabs(atof(args[0])))))
    sprintf(buff, "Overflow");
  else
    sprintf(buff, "%f", pow(atof(args[0]), num));
}

static void fun_log(buff, args)
char *buff;
char *args[10];
{
  if (atof(args[0]) <= 0)
    sprintf(buff, "Undefined");
  else
    sprintf(buff, "%f", log10(atof(args[0])));
}

static void fun_ln(buff, args)
char *buff;
char *args[10];
{
  if (atof(args[0]) <= 0)
    sprintf(buff, "Undefined");
  else
    sprintf(buff, "%f", log(atof(args[0])));
}

static void fun_arctan(buff, args)
char *buff;
char *args[10];
{
  if ((atof(args[0]) > 1) || (atof(args[0]) < -1))
    sprintf(buff, "Undefined");
  else
    sprintf(buff, "%f", atan(atof(args[0])));
}

static void fun_arccos(buff, args)
char *buff;
char *args[10];
{
  if ((atof(args[0]) > 1) || (atof(args[0]) < -1))
    sprintf(buff, "Undefined");
  else
    sprintf(buff, "%f", acos(atof(args[0])));
}

static void fun_arcsin(buff, args)
char *buff;
char *args[10];
{
  if ((atof(args[0]) > 1) || (atof(args[0]) < -1))
    sprintf(buff, "Undefined");
  else
    sprintf(buff, "%f", asin(atof(args[0])));
}

static void fun_tan(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%f", tan(atof(args[0])));
}

static void fun_cos(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%f", cos(atof(args[0])));
}

static void fun_sin(buff, args)
char *buff;
char *args[10];
{
  sprintf(buff, "%f", sin(atof(args[0])));
}

static void fun_if(buff, args)
char *buff;
char *args[10];
{
  if (istrue(args[0]))
    sprintf(buff, "%s", args[1]);
  else
    *buff = '\0';
  return;
}

static void fun_ifelse(buff, args)
char *buff;
char *args[10];
{
  if (istrue(args[0]))
    sprintf(buff, "%s", args[1]);
  else
    sprintf(buff, "%s", args[2]);
  return;
}

static void fun_rmatch(buff, args, privs, doer)
char *buff;
char *args[10];
dbref privs;
dbref doer;
{
  dbref who;

  who = match_thing(privs, args[0]);
  if (!controls(privs, who, POW_EXAMINE) && who != doer)
  {
    strcpy(buff, "#-1");
    notify(privs, perm_denied());
    return;
  }
  init_match(who, args[1], NOTYPE);
  match_everything();
  sprintf(buff, "#%ld", match_result());
}

static void fun_wcount(buff, args)
char *buff;
char *args[10];
{
  char *p = args[0];
  long num = 0;

  while (*p)
  {
    while (*p && isspace(*p))
      p++;
    if (*p)
      num++;
    while (*p && !isspace(*p))
      p++;
  }
  sprintf(buff, "%ld", num);
  return;
}

/* MERLMOD */

static void fun_lwho(buff, privs, owner)
char *buff;
dbref privs;
dbref owner;
{
  char buf[1024];
  struct descriptor_data *d;

  if (Typeof(owner) != TYPE_PLAYER && !payfor(owner, 50))
  {
    notify(owner, "You don't have enough pennies.");
    return;
  }
  *buf = '\0';
  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED && d->player > 0)
    {
      if ((controls(owner, d->player, POW_WHO)) ||
	  could_doit(owner, d->player, A_LHIDE))
      {
	if (*buf)
	  strcpy(buf, tprintf("%s #%ld", buf, d->player));
	else
	  sprintf(buf, "#%ld", d->player);
      }
    }
  }
  strcpy(buff, buf);
}

static void fun_spc(buff, args, dumm1, dumm2)
char *buff;
char *args[10];
dbref dumm1, dumm2;
{
  char tbuf1[1024];
  int i;
  int s = atoi(args[0]);

  if (s <= 0)
  {
    strcpy(buff, "");
    return;
  }

  if (s > 950)
    s = 950;

  for (i = 0; i < s; i++)
    tbuf1[i] = ' ';
  tbuf1[i] = '\0';
  strcpy(buff, tbuf1);
}

static void do_flip(s, r)
char *s;
char *r;

     /* utility function to reverse a string */
{
  char *p;

  p = strlen(s) + r;
  for (*p-- = '\0'; *s; p--, s++)
    *p = *s;
}

static void fun_flip(buff, args, dumm1, dumm2)
char *buff;
char *args[10];
dbref dumm1, dumm2;
{
  do_flip(args[0], buff);
}

static void fun_lnum(buff, args, dumm1, dumm2)
char *buff;
char *args[10];
dbref dumm1, dumm2;
{
  int x, i;

  x = atoi(args[0]);
  if ((x > 250) || (x < 0))
  {
    strcpy(buff, "#-1 Number Out Of Range");
    return;
  }
  else
  {
    strcpy(buff, "0");
    for (i = 1; i < x; i++)
      sprintf(buff, "%s %d", buff, i);
  }
}

static void fun_cstrip(buf, args, privs)
char *buf;
char *args[10];
dbref privs;
{
  strcpy(buf, strip_color(args[0]));
}

static void fun_ctrunc(buf, args, privs)
char *buf;
char *args[10];
dbref privs;
{
  strcpy(buf, truncate_color(args[0], atoi(args[1])));
}

static void fun_string(buf, args, privs)
char *buf;
char *args[10];
dbref privs;
{
  int num, i;
  char *letter;

  *buf = '\0';
  num = atoi(args[1]);
  letter = args[0];

  if (((num * strlen(letter)) <= 0) || ((num * strlen(letter)) > 950))
  {
    strcpy(buf, "#-1 Out Of Range");
    return;
  }

  *buf = '\0';
  for (i = 0; i < num; i++)
    strcat(buf, letter);
}

static void fun_ljust(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  int number = atoi(args[1]);
  char *text;
  int leader;
  char tbuf1[1000];
  char buf[1000];
  int i;

  if (number <= 0 || number > 950)
  {
    sprintf(buff, "#-1 Number out of range.");
    return;
  }

  text = args[0];
  leader = number - strlen(strip_color(text));

  if (leader <= 0)
  {
    strcpy(buff, truncate_color(text, number));
    return;
  }

  if (leader > 950)
    leader = 950;

  for (i = 0; i < leader; i++)
    tbuf1[i] = ' ';
  tbuf1[i] = '\0';
  sprintf(buf, "%s%s", text, tbuf1);
  strcpy(buff, buf);
}

static void fun_rjust(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  int number = atoi(args[1]);
  char *text;
  int leader;
  char tbuf1[1000];
  char buf[1000];
  int i;

  if (number <= 0 || number > 950)
  {
    sprintf(buff, "#-1 Number out of range.");
    return;
  }

  text = args[0];
  leader = number - strlen(strip_color(text));

  if (leader <= 0)
  {
    strcpy(buff, truncate_color(text, number));
    return;
  }

  if (leader > 950)
    leader = 950 - 1;

  for (i = 0; i < leader; i++)
    tbuf1[i] = ' ';
  tbuf1[i] = '\0';
  sprintf(buf, "%s%s", tbuf1, text);
  strcpy(buff, buf);
}

static void fun_lattrdef(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref it = match_thing(privs, args[0]);
  int len = 0;
  ATRDEF *k;

  *buff = '\0';
  if ((db[it].atrdefs) && (controls(privs, it, POW_EXAMINE) || (db[it].flags & SEE_OK)))
  {
    for (k = db[it].atrdefs; k; k = k->next)
    {
      if (len)
      {
	static char smbuf[100];

	sprintf(smbuf, " %s", k->a.name);
	if ((strlen(smbuf) + len) > 990)
	{
	  strcat(buff, " #-1");
	  return;
	}
	strcat(buff, smbuf);
	len += strlen(smbuf);
      }
      else
      {
	sprintf(buff, "%s", k->a.name);
	len = strlen(buff);
      }
    }
  }
}

static void fun_lattr(buff, args, privs)
char *buff;
char *args[10];
dbref privs;
{
  dbref it = match_thing(privs, args[0]);
  int len = 0;
  ALIST *a;
  char temp[1024];

  *buff = '\0';
  if (db[it].list)
  {
    for (a = db[it].list; a; a = AL_NEXT(a))
    {
      if (AL_TYPE(a) && can_see_atr(privs, it, AL_TYPE(a)))
      {
	sprintf(temp, (*buff) ? " %s" : "%s", unparse_attr(AL_TYPE(a), 0));
	if ((len + strlen(temp)) > 960)
	{
	  strcat(buff, " #-1");
	  return;
	}
	strcpy(buff + len, temp);
	len += strlen(temp);
      }
    }
  }
}

static void fun_type(buff, args, privs, dumm1)
char *buff;
char *args[10];
dbref privs;
dbref dumm1;
{
  extern char *type_to_name();
  dbref it = match_thing(privs, args[0]);

  if (it == NOTHING)
  {
    strcpy(buff, "#-1");
    return;
  }
  strcpy(buff, type_to_name(Typeof(it)));
}

static void fun_idle(buff, args, owner)
char *buff;
char *args[10];
dbref owner;
{
  char buf[1024];
  struct descriptor_data *d;
  dbref who = 0;

  if (!string_compare(args[0], "me"))
    who = owner;
  else
    who = lookup_player(args[0]);
  if (!who)
  {
    sprintf(buff, "#-1");
    return;
  }
  sprintf(buf, "#-1");
  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED && d->player > 0)
    {
      if ((controls(owner, d->player, POW_WHO)) ||
	  could_doit(owner, d->player, A_LHIDE))
      {
	if (d->player == who)
	{
	  sprintf(buf, "%ld", (now - d->last_time));
	  break;
	}
      }
    }
  }
  if (buf == NULL)
    sprintf(buf, "#-1");
  strcpy(buff, buf);
}

static void fun_onfor(buff, args, owner)
char *buff;
char *args[10];
dbref owner;
{
  char buf[1024];
  struct descriptor_data *d;
  dbref who = 0;

  if (!(string_compare(args[0], "me")))
    who = owner;
  else
    who = lookup_player(args[0]);
  if (!who)
  {
    sprintf(buff, "#-1");
    return;
  }
  sprintf(buf, "#-1");
  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED && d->player > 0)
    {
      if ((controls(owner, d->player, POW_WHO)) ||
	  could_doit(owner, d->player, A_LHIDE))
      {
	if (d->player == who)
	{
	  sprintf(buf, "%ld", (now - d->connected_at));
	  break;
	}
      }
    }
  }
  if (buf == NULL)
    sprintf(buf, "#-1");
  strcpy(buff, buf);
}

static void fun_port(buff, args, owner)
char *buff;
char *args[10];
dbref owner;
{
  char buf[1024];
  struct descriptor_data *d;
  dbref who = 0;

  if (!(string_compare(args[0], "me")))
    who = owner;
  else
    who = lookup_player(args[0]);

  sprintf(buf, "#-1");
  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED && d->player > 0)
    {
      if ((controls(owner, d->player, POW_WHO)) ||
	  could_doit(owner, d->player, A_LHIDE))
      {
	if (d->player == who)
	{
	  sprintf(buf, "%d", ntohs(d->address.sin_port));
	  break;
	}
      }
    }
  }
  strcpy(buff, buf);
}

static void fun_host(buff, args, owner)
char *buff;
char *args[10];
dbref owner;
{
  char buf[1024];
  struct descriptor_data *d;
  dbref who = 0;

  if (!(string_compare(args[0], "me")))
    who = owner;
  else
    who = lookup_player(args[0]);

  sprintf(buf, "#-1");
  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED && d->player > 0)
    {
      if ((controls(owner, d->player, POW_WHO)) ) 
      {
/* || could_doit(owner, d->player, A_LHIDE)) */
	if (d->player == who)
	{
	  sprintf(buf, "%s@%s", d->user, d->addr);
	  break;
	}
      }
    }
  }
  strcpy(buff, buf);
}

static void fun_tms(buff, args)
char *buff;
char *args[10];
{
  int num = atoi(args[0]);

  if (num < 0)
  {
    sprintf(buff, "#-1");
  }
  else
  {
    sprintf(buff, "%s", time_format_2(num));
  }
}

static void fun_tml(buff, args)
char *buff;
char *args[10];
{
  int num = atoi(args[0]);

  if (num < 0)
  {
    sprintf(buff, "#-1");
  }
  else
  {
    sprintf(buff, "%s", time_format_1(num));
  }
}

static void fun_tmf(buff, args)
char *buff;
char *args[10];
{
  int num = atoi(args[0]);

  if (num < 0)
  {
    sprintf(buff, "#-1");
  }
  else
  {
    sprintf(buff, "%s", time_format_3(num));
  }
}

static void fun_tmfl(buff, args)
char *buff;
char *args[10];
{
  int num = atoi(args[0]);

  if (num < 0)
  {
    sprintf(buff, "#-1");
  }
  else
  {
    sprintf(buff, "%s", time_format_4(num));
  }
}

typedef struct fun FUN;

#include "funcs.c"

void info_funcs(player)
dbref player;
{
  int i;

  notify(player, tprintf("Builtin functions:"));
  notify(player, tprintf("%10s %s", "function", "nargs"));
  for (i = 0; i < (sizeof(wordlist) / sizeof(*wordlist)); i++)
    if (wordlist[i].name && wordlist[i].name[0])
      if (wordlist[i].nargs == -1)
	notify(player, tprintf("%10s any", wordlist[i].name));
      else
	notify(player, tprintf("%10s %d", wordlist[i].name, wordlist[i].nargs));
}

static int udef_fun(str, buff, privs, doer)
char **str;
char *buff;
dbref privs;
dbref doer;
{
  ATTR *attr = NULL;
  dbref tmp, defed_on = NOTHING;
  char obuff[1024], *args[10], *s;
  int a;

  /* check for explicit redirection */
  if ((buff[0] == '#') && ((s = strchr(buff, ':')) != NULL))
  {
    *s = '\0';
    tmp = (dbref) atoi(buff + 1);
    *s++ = ':';
    if (tmp >= 0 && tmp < db_top && ((attr = atr_str(privs, tmp, s)) != NULL) &&
	(attr->flags & AF_FUNC) &&
	can_see_atr(privs, tmp, attr) && !(attr->flags & AF_HAVEN))
      defed_on = tmp;
  }
  /* check the object doing it */
  else if (((attr = atr_str(privs, tmp = privs, buff)) != NULL) &&
	   (attr->flags & AF_FUNC) && !(attr->flags & AF_HAVEN))
    defed_on = tmp;

  /* check that object's zone */
  else
  {
    DOZONE(tmp, privs)
      if (((attr = atr_str(privs, tmp, buff)) != NULL) &&
	  (attr->flags & AF_FUNC) && !(attr->flags & AF_HAVEN))
    {
      defed_on = tmp;
      break;
    }
  }
  if (defed_on != NOTHING)
  {
    char result[1024], ftext[1024], *saveptr[10];

    for (a = 0; a < 10; a++)
      args[a] = "";
    for (a = 0; (a < 10) && **str && (**str != ')'); a++)
    {
      if (**str == ',')
	(*str)++;
      museexec(str, obuff, privs, doer, 1);
      strcpy(args[a] = stack_em_fun(strlen(obuff) + 1), obuff);
    }
    for (a = 0; a < 10; a++)
    {
      saveptr[a] = wptr[a];
      wptr[a] = args[a];
    }
    if (**str)
      (*str)++;
    strcpy(ftext, atr_get(defed_on, attr));
    pronoun_substitute(result, doer, ftext, privs);
    for (--a; a >= 0; a--)
    {
      wptr[a] = saveptr[a];
    }
    strcpy(buff, result + strlen(db[doer].name) + 1);
    return 1;
  }
  else
    return 0;
}

static void do_fun(str, buff, privs, doer)
char **str;
char *buff;
dbref privs;
dbref doer;
{
  FUN *fp;
  char *args[10];
  char obuff[1024];
  int a;

  /* look for buff in flist */
  strcpy(obuff, buff);

  for (a = 0; obuff[a]; a++)
    obuff[a] = to_lower(obuff[a]);

  fp = lookup_funcs_gperf(obuff, strlen(obuff));

  strcpy(obuff, buff);

  if (!fp)
    if (udef_fun(str, buff, privs, doer))
      return;
    else
    {
      int deep = 2;
      char *s = buff + strlen(obuff);

      strcpy(buff, obuff);
      *s++ = '(';
      while (**str && deep)
	switch (*s++ = *(*str)++)
	{
	case '(':
	  deep++;
	  break;
	case ')':
	  deep--;
	  break;
	}
      if (**str)
      {
	(*str)--;
	s--;
      }
      *s = 0;
      return;
    }
  /* now get the arguments to the function */
  for (a = 0; (a < 10) && **str && (**str != ')'); a++)
  {
    if (**str == ',')
      (*str)++;
    museexec(str, obuff, privs, doer, 1);
    strcpy(args[a] = (char *)stack_em_fun(strlen(obuff) + 1), obuff);
  }
  if (**str)
    (*str)++;
  if ((fp->nargs != -1) && (fp->nargs != a))
    strcpy(buff, tprintf("Function (%s) only expects %d arguments",
			 fp->name, fp->nargs));
  else
  {

#ifdef SIGEMT
    extern int floating_x;

    floating_x = 0;
#endif
    fp->func(buff, args, privs, doer, a);
#ifdef SIGEMT
    if (floating_x)
      strcpy(buff, "Floating exception.");
#endif
  }
}

static int lev = 0;		/* the in depth level which we're at. */
void func_zerolev()
{
  lev = 0;
}				/* called from process_command just in case
				   this goes bezerko */
/* execute a string expression, return result in buff */
void museexec(str, buff, privs, doer, coma)
char **str;
char *buff;
dbref privs;
dbref doer;
int coma;
{
  char *s, *e = buff;
  int recursion_limit = 15000;

  if (db[privs].pows)
    if (*db[privs].pows == CLASS_GUEST)
      recursion_limit = 1000;

  lev += 10;			/* enter the func. */
  if (lev > recursion_limit)
  {
    strcpy(buff, "Too many levels of recursion.");
    return;
  }
  *buff = 0;
  /* eat preceding space */
  for (s = *str; *s && isspace(*s); s++) ;
  /* parse until (,],) or , */
  for (; *s; s++)
    switch (*s)
    {
    case ',':			/* comma in list of function arguments */
    case ')':			/* end of arguments */
      if (!coma)
	goto cont;
    case ']':			/* end of expression */
      /* eat trailing space */
      while ((--e >= buff) && isspace(*e)) ;
      e[1] = 0;
      *str = s;
      lev--;
      return;
    case '(':			/* this is a function */
      while ((--e >= buff) && isspace(*e)) ;
      e[1] = 0;
      *str = s + 1;
      /* if just ()'s by them self then it is quoted */
      if (*buff)
	do_fun(str, buff, privs, doer);
      lev--;
      return;
    case '{':
      if (e == buff)
      {
	int deep = 1;

	e = buff;
	s++;
	while (deep && *s)
	  switch (*e++ = *s++)
	  {
	  case '{':
	    deep++;
	    break;
	  case '}':
	    deep--;
	    break;
	  }
	if ((e > buff) && (e[-1] == '}'))
	  e--;
	while ((--e >= buff) && isspace(*e)) ;
	e[1] = 0;
	*str = s;
	lev--;
	return;
      }
      else
      {
	/* otherwise is a quote in middle, search for other end */
	int deep = 1;

	*e++ = *s++;
	while (deep && *s)
	  switch (*e++ = *s++)
	  {
	  case '{':
	    deep++;
	    break;
	  case '}':
	    deep--;
	    break;
	  }
	s--;
      }
      break;
    default:
    cont:
      *e++ = *s;
      break;
    }
  while ((--e >= buff) && isspace(*e)) ;
  e[1] = 0;
  *str = s;
  lev -= 9;
  return;
}

/* function to split up a list given a seperator */
/* note str will get hacked up */
char *parse_up(str, delimit)
char **str;
int delimit;
{
  int deep = 0;
  char *s = *str, *os = *str;

  if (!*s)
    return (NULL);
  while (*s && (*s != delimit))
    if (*s++ == '{')
    {
      deep = 1;
      while (deep && *s)
	switch (*s++)
	{
	case '{':
	  deep++;
	  break;
	case '}':
	  deep--;
	  break;
	}
    }
  if (*s)
    *s++ = 0;
  *str = s;
  return (os);
}
