/* rob.c */
/* $Id: rob.c,v 1.3 1993/01/30 03:38:43 nils Exp $ */

#include "copyright.h"
#include <ctype.h>

#include "db.h"
#include "config.h"
#include "interface.h"
#include "match.h"
#include "externs.h"

void do_giveto(player, who, amnt)
dbref player;
char *who;
char *amnt;
{
  long amount;
  dbref recipt;

  if (!power(player, POW_MEMBER))
  {
    notify(player, "Silly, you can't give out money!");
    return;
  }
  init_match(player, who, TYPE_PLAYER);
  match_player();
  match_absolute();
  match_neighbor();
  recipt = noisy_match_result();
  if (recipt == NOTHING)
  {
    return;
  }
  amount = atol(amnt);
  if ((amount < 1) && !has_pow(player, recipt, POW_STEAL))
  {
    notify(player, "You can only @giveto positive amounts.");
    return;
  }
  if (!payfor(player, amount))
  {
    notify(player, "You can't pay for that much1");
    return;
  }
  giveto(recipt, amount);
  notify(player, "Given.");
}

void do_give(player, recipient, amnt)
dbref player;
char *recipient;
char *amnt;
{
  dbref who;
  long amount;
  char buf[BUFFER_LEN];
  char *s;

  if (Guest(db[player].owner))
  {
    notify(player, "Sorry, guests can't do that!");
    return;
  }

  /* check recipient */
  init_match(player, recipient, TYPE_PLAYER);
  match_neighbor();
  match_me();
  if (power(player, POW_REMOTE))
  {
    match_player();
    match_absolute();
  }

  switch (who = match_result())
  {
  case NOTHING:
    notify(player, "Give to whom?");
    return;
  case AMBIGUOUS:
    notify(player, "I don't know who you mean!");
    return;
  }

  if (Guest(real_owner(who)))
  {
    notify(player, "Sorry, guests can't do that!");
    return;
  }

  /* make sure amount is all digits */
  for (s = amnt; *s && ((isdigit(*s)) || (*s == '-')); s++) ;
  /* must be giving object */
  if (*s)
  {
    dbref thing;

    init_match(player, amnt, TYPE_THING);
    match_possession();
    match_me();
    switch (thing = match_result())
    {
    case NOTHING:
      notify(player, "You don't have that!");
      return;
    case AMBIGUOUS:
      notify(player, "I don't know which you mean!");
      return;
    default:
      if (((Typeof(thing) == TYPE_THING) || (Typeof(thing) == TYPE_PLAYER)) &&
	  (((db[who].flags & ENTER_OK) && could_doit(player, thing, A_LOCK))
	   || controls(player, who, POW_TELEPORT)))
      {
	moveto(thing, who);
	notify(who,
	       tprintf("%s gave you %s.", db[player].name, db[thing].name));
	notify(player, "Given.");
	notify(thing, tprintf("%s gave you to %s.", db[player].name,
			      db[who].name));
      }
      else
	notify(player, "Permission denied.");
    }
    return;
  }
  amount = atol(amnt);
  /* do amount consistency check */
  if ((amount < 1) && !has_pow(player, who, POW_STEAL))
  {
    notify(player, "You must specify a positive number of Credits.");
    return;
  }

  if (!power(player, POW_STEAL))
  {
    if (Pennies(who) + amount > max_pennies)
    {
      notify(player, "That player doesn't need that many Credits!");
      return;
    }
  }

  /* try to do the give */
  if (!payfor(player, amount))
  {
    notify(player, "You don't have that many Credits to give!");
  }
  else
  {
    /* objects work differently */
    if (Typeof(who) == TYPE_THING)
    {
      int cost;

      if (amount < (cost = atol(atr_get(who, A_COST))))
      {
	notify(player, "Feeling poor today?");
	giveto(player, amount);
	return;
      }
      if (cost < 0)
	return;
      if ((amount - cost) > 0)
      {
	sprintf(buf, "You get %ld in change.", amount - cost);
      }
      else
      {
	sprintf(buf, "You paid %ld credits.", amount);
      }
      notify(player, buf);
      giveto(player, amount - cost);
      giveto(who, cost);
      did_it(player, who, A_PAY, NULL, A_OPAY, NULL, A_APAY);
      return;
    }
    else
    {
      /* he can do it */
      notify(player,
	     tprintf("You give %ld Credits to %s.",
		     amount,
		     db[who].name));
      if (Typeof(who) == TYPE_PLAYER)
      {
	notify(who,
	       tprintf("%s gives you %ld Credits.",
		       db[player].name,
		       amount));
      }
      giveto(who, amount);
      did_it(player, who, A_PAY, NULL, A_OPAY, NULL, A_APAY);
    }
  }
}
