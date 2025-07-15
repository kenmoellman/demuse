/* dbtop.c */
/* $Id: dbtop.c,v 1.4 1993/08/16 01:56:33 nils Exp $ */

#include "db.h"
#include "externs.h"

static void shuffle P((int));

static int top[30];
static dbref topp[30];

static void dbtop_internal P((dbref, long (*)(), char *));

static long dt_mem(x)
dbref x;
{
/*  dbref i; */
  if (db[x].owner != x)
    return -1;

  return atol(atr_get(x, A_BYTESUSED));
/*  for(i=0;i<db_top;i++) {
   if(db[i].owner == x) {
   tot+=mem_usage(i);
   }
   }
   return tot; */
}
static long dt_cred(x)
dbref x;
{
  return Pennies(x);
}
static long dt_cont(x)
dbref x;
{
  dbref l;
  long num = 0;

  if (Typeof(x) == TYPE_EXIT || db[x].contents == NOTHING)
    return -1;
  for (l = db[x].contents; l != NOTHING; l = db[l].next)
    num++;
  return num;
}
static long dt_exits(x)
dbref x;
{
  dbref l;
  long num = 0;

  if (Typeof(x) != TYPE_ROOM || Exits(x) == NOTHING)
    return -1;
  for (l = Exits(x); l != NOTHING; l = db[l].next)
    num++;
  return num;
}
static long dt_quota(x)
dbref x;
{
  if (Typeof(x) != TYPE_PLAYER)
    return (long)-1;
  if (power(x, POW_NOQUOTA))
    return (long)-1;
  return atol(atr_get(x, A_QUOTA));
  /* return atoi(atr_get(x,A_RQUOTA))+dt_obj(x); */
}
static long dt_obj(x)
register dbref x;
{

  if (Typeof(x) != TYPE_PLAYER)
    return -1;
  /* for(y=0;y<db_top;y++) if(db[y].owner == x) num++; */
  return atol(atr_get(x, A_QUOTA)) - atol(atr_get(x, A_RQUOTA));
  /* return num; */
}
static long dt_numdefs(x)
dbref x;
{
  register ATRDEF *j;
  register int k = 0;

  for (j = db[x].atrdefs; j; j = j->next)
    k++;
  return k;
}
extern long dt_mail P((dbref));	/* from mail.c */

void do_dbtop(player, arg1)
dbref player;
char *arg1;
{
  static struct dbtop_list
  {
    char *nam;
    long (*func) P((dbref));
  }
   *ptr, funcs[] =
  {
    {
      "numdefs", dt_numdefs
    }
    ,
    {
      "credits", dt_cred
    }
    ,
    {
      "contents", dt_cont
    }
    ,
    {
      "exits", dt_exits
    }
    ,
    {
      "quota", dt_quota
    }
    ,
    {
      "objects", dt_obj
    }
    ,
    {
      "memory", dt_mem
    }
    ,
    {
      "mail", dt_mail
    }
    ,
    {
      0, 0
    }
  };

  int nm = 0;

  if (!*arg1)
    arg1 = "foo! this shouldn't match anything";
  for (ptr = funcs; ptr->nam; ptr++)
  {
    if (string_prefix(ptr->nam, arg1) || !strcmp(arg1, "all"))
    {
      nm++;
#ifdef POW_DBTOP
      if (!power(player, POW_DBTOP))
      {
	notify(player, "@dbtop is a restricted command.");
	return;
      }
#endif
      dbtop_internal(player, ptr->func, ptr->nam);
    }
  }
  if (nm == 0)
  {
    notify(player, "Usage: @dbtop all|catagory");
    notify(player, "Catagories are:");
    for (ptr = funcs; ptr->nam; ptr++)
      notify(player, tprintf("  %s", ptr->nam));
  }
}

static void dbtop_internal(player, calc, nam)
dbref player;
long (*calc) ();
char *nam;
{
  long i, m, j;

  for (j = 0; j < 30; j++)
  {
    top[j] = top[j] = (-1);
    topp[j] = topp[j] = (dbref) 0;
  }
  notify(player, tprintf("** %s:", nam));
  for (i = 0; i < db_top; i++)
  {
    m = (*calc) (i);
    if (m > top[28])
    {				/* put it somewhere */
      for (j = 28; (j > 0) && (top[j] < m); j--) ;
      j++;
      shuffle(j);
      top[j] = m;
      topp[j] = i;
    }
  }
  for (j = 1; j < 27; j++)
  {
    notify(player, tprintf("%2d) %s has %ld %s", j, unparse_object(player, topp[j]), top[j], nam));
  }
}

static void shuffle(jj)
int jj;
{
  if (jj > 28)
    return;
  shuffle(jj + 1);
  top[jj + 1] = top[jj];
  topp[jj + 1] = topp[jj];
}
