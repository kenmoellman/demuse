/* player.c */
/* $Id: player.c,v 1.17 1993/09/07 18:24:24 nils Exp $ */

#include <crypt.h>

#include "db.h"
#include "config.h"
#include "interface.h"
#include "externs.h"
#include "credits.h"
#include "player.h"
#include "admin.h"

static void destroy_player P((dbref));

dbref connect_player(name, password)
char *name;
char *password;
{
  dbref player;

  /* long tt; char *s; */

  player = lookup_player(name);
  if (player == NOTHING)
    return NOTHING;
#ifdef USE_INCOMING
  if (Typeof(player) != TYPE_PLAYER && !power(player, POW_INCOMING))
    return NOTHING;
#endif
  if (!password || !*password)
    return NOTHING;
  if ((*Pass(player) || Typeof(player) != TYPE_PLAYER)
   && (strcmp(Pass(player), password) || !strncmp(Pass(player), "XX", 2)) &&
      strcmp(crypt(password, "XX"), Pass(player))
    )
    if (*Pass(db[player].owner)
	&& (strcmp(Pass(db[player].owner), password) || !strncmp(Pass(db[player].owner), "XX", 2)) &&
	strcmp(crypt(password, "XX"), Pass(db[player].owner))
      )
      return PASSWORD;

  return player;
}

dbref create_guest(name, alias, password)
char *name;
char *alias;
char *password;
{
  dbref player;
  char key[10];

  /* make sure that old guest id's don't hang around! */
  player = lookup_player(name);
  if (player != NOTHING)
  {
    if (db[player].pows && Guest(player))
    {
      destroy_player(player);
    }
    else
    {
      return NOTHING;
    }
  }

  /* make new player */
  player = new_object();

  /* initialize everything */
  SET(db[player].name, name);
  SET(db[player].cname, name);
  db[player].location = guest_start;
  db[player].link = guest_start;
  db[player].owner = player;
  db[player].flags = TYPE_PLAYER;
  db[player].pows = malloc(sizeof(ptype) * 2);
  db[player].pows[0] = CLASS_GUEST;	/* re@class later. */
  db[player].pows[1] = 0;
  s_Pass(player, crypt(GUEST_PASSWORD, "XX"));
  giveto(player, initial_credits);	/* starting bonus */
  atr_add(player, A_RQUOTA, start_quota);
  atr_add(player, A_QUOTA, start_quota);
  /* link him to start */
  PUSH(player, db[guest_start].contents);

  add_player(player);
  do_force(root, tprintf("#%ld", player), "+channel +public");

  /* lock guest to him/her-self */
  sprintf(key, "#%ld", player);
  atr_add(player, A_LOCK, key);

  /* set guest's description */
  atr_add(player, A_DESC, guest_description);

  /* set guest's alias */
  delete_player(player);
  atr_add(player, A_ALIAS, alias);
  add_player(player);

  /* zero their quota */
  atr_add(player, A_RQUOTA, "0");
  atr_add(player, A_QUOTA, "0");

  return player;
}

/* extra precaution just in case a regular
   player is passed somehow to this routine */
void destroy_guest(guest)
dbref guest;
{
  if (!Guest(guest))
    return;

  destroy_player(guest);
}

char *class_to_name();

dbref create_player(name, password, class, start)
char *name;
char *password;
int class;
dbref start;
{
  dbref player;

  if (!ok_player_name(NOTHING, name, ""))
  {
    log_error("failed in name check");
    report();
    return NOTHING;
  } 
  if ((class != CLASS_GUEST) && (!ok_password(password)))
  {
    log_error("failed in password check");
    report();
    return NOTHING;
  } 
  
  if (strchr(name, ' '))
  {
    log_error("failed in name check - blank");
    report();
    return NOTHING;
  } 

  /* else he doesn't already exist, create him */
  player = new_object();

  /* initialize everything */
  SET(db[player].name, name);
  SET(db[player].cname, name);
  db[player].location = start;
  db[player].link = start;
  db[player].owner = player;
  db[player].flags = TYPE_PLAYER;
  db[player].pows = malloc(sizeof(ptype) * 2);
  db[player].pows[0] = CLASS_GUEST;	/* re@class later. */
  db[player].pows[1] = 0;
  s_Pass(player, crypt(password, "XX"));
  giveto(player, initial_credits);	/* starting bonus */
  atr_add(player, A_RQUOTA, start_quota);
  atr_add(player, A_QUOTA, start_quota);
  /* link him to start */
  PUSH(player, db[start].contents);

  add_player(player);
  if (class != CLASS_GUEST)
  {
    do_force(root, tprintf("#%ld", player), "+channel +public");
    do_class(root, stralloc(tprintf("#%ld", player)), class_to_name(class));
  }
  return player;
}

static void destroy_player(player)
dbref player;
{
  dbref loc, thing;

  /* destroy all of the player's things/exits/rooms */
  for (thing = 0; thing < db_top; thing++)
  {
    if (db[thing].owner == player && thing != player)
    {
      moveto(thing, NOTHING);
      switch (Typeof(thing))
      {
      case TYPE_CHANNEL:
#ifdef USE_UNIV
      case TYPE_UNIVERSE:
#endif
      case TYPE_PLAYER:
/* stolen from immie */
	if (db[thing].owner == player && db[player].owner == thing)
	{
	  db[thing].owner = thing;
	  db[player].owner = player;
	  destroy_player(thing);
	}
	do_empty(thing);
	break;
/* end stolen code */
      case TYPE_THING:
	do_empty(thing);
	break;
      case TYPE_EXIT:
	loc = find_entrance(thing);
	s_Exits(loc, remove_first(Exits(loc), thing));
	do_empty(thing);
	break;
      case TYPE_ROOM:
	do_empty(thing);
	break;
      }
    }
  }

  boot_off(player);		/* disconnect player (just making sure) :) */
  do_halt(player, "", "");		/* halt processes ? */
  moveto(player, NOTHING);	/* send it to nowhere */
  delete_player(player);	/* remove player from player list */
  do_empty(player);		/* destroy player-object */
}

void do_pcreate(creator, player_name, player_password)
dbref creator;
char *player_name;
char *player_password;
{
  dbref player;

  if (!power(creator, POW_PCREATE))
  {
    log_important(tprintf("%s failed to: @pcreate %s=%s", unparse_object_a(root, creator),
			  player_name, player_password));
    notify(creator, perm_denied());
    return;
  }

  if (lookup_player(player_name) != NOTHING)
  {
    notify(creator, tprintf("There is already a %s", unparse_object(creator, lookup_player(player_name))));
    return;
  }

  if (!ok_player_name(NOTHING, player_name, "") || strchr(player_name, ' '))
  {
    notify(creator, tprintf("Illegal player name '%s'", player_name));
    return;
  }

  player = create_player(player_name, player_password, CLASS_CITIZEN, player_start);
  if (player == NOTHING)
  {
    notify(creator, tprintf("Failure creating '%s'", player_name));
    return;
  }

  notify(creator, tprintf("New player '%s' created with password '%s'",
			  player_name, player_password));
  log_important(tprintf("%s executed: @pcreate %s", unparse_object_a(root, creator), unparse_object_a(root, player)));
  log_sensitive(tprintf("%s executed: @pcreate %s=%s", unparse_object_a(root, creator),
			unparse_object_a(root, player), player_password));
}

void do_password(player, old, newobj)
dbref player;
char *old;
char *newobj;
{
  if (!has_pow(player, NOTHING, POW_MEMBER))
  {
    notify(player,
	   tprintf("Only registered %s users may change their passwords.",
		   muse_name));
    return;
  }


/* if no password set, or password doesn't match plaintext or encrypted.. */
  if (!(Pass(player) && *Pass(player)) ||
      (strcmp(old, Pass(player)) && strcmp(crypt(old, "XX"), Pass(player))))
  {
    notify(player, "Sorry");
  }
  else if (!ok_password(newobj))
  {
    notify(player, "Bad new password.");
  }
  else
  {
    s_Pass(player, crypt(newobj, "XX"));
    notify(player, "Password changed.");
  }
}

void do_nuke(player, name)
dbref player;
char *name;
{
  dbref victim;
  int x;

  /* if ( RLevel(player) < TYPE_ADMIN ) */
  if ((!power(player, POW_NUKE)) || (Typeof(player) != TYPE_PLAYER))
  {
    notify(player, "This is a restricted command.");
    return;
  }

  init_match(player, name, TYPE_PLAYER);
  match_neighbor();
  match_absolute();
  match_player(NOTHING, NULL);
  if ((victim = noisy_match_result()) == NOTHING)
    return;

  if (Typeof(victim) != TYPE_PLAYER)
  {
    notify(player, "You can only nuke players!");
  }
  else if (!controls(player, victim, POW_NUKE))
  {
    log_important(tprintf("%s failed to: @nuke %s", unparse_object_a(player, player),
			  unparse_object_a(victim, victim)));
    notify(player, perm_denied());
  }
  else if (owns_stuff(victim))
  {
    notify(player, "You must @wipeout their junk first.");
  }
  else
  {
    /* get rid of that guy, destroy him */
/*    while (boot_off(victim)) ;	* boot'm off lotsa times! */
    for (x = 0;x<1000;x++)
      boot_off(victim);
    do_halt(victim, "", "");
    delete_player(victim);
    db[victim].flags = TYPE_THING;
    db[victim].owner = root;
    destroy_obj(victim, atol(default_doomsday));

    notify(player, tprintf("%s - Nuked.", db[victim].cname));
    log_important(tprintf("%s executed: @nuke %s",
       unparse_object_a(player, player), unparse_object_a(victim, victim)));
  }
}
ptype name_to_pow(nam)
char *nam;
{
  int k;

  for (k = 0; k < NUM_POWS; k++)
    if (!string_compare(powers[k].name, nam))
      return powers[k].num;
  return 0;
}
char *pow_to_name(pow)
ptype pow;
{
  int k;

  for (k = 0; k < NUM_POWS; k++)
    if (powers[k].num == pow)
      return stralloc(powers[k].name);
  return stralloc("<unknown power>");
}
char *get_class(player)
dbref player;
{
  extern char *type_to_name();

  if (Typeof(player) == TYPE_PLAYER)
    return class_to_name(*db[player].pows);
  else
    return type_to_name(Typeof(player));
}

/* reclassify player */
void do_class(player, arg1, class)
dbref player;
char *arg1;
char *class;
{
  int i, newlevel;
  dbref who;

  if (*arg1 == '\0')
    who = player;
  else
  {
    init_match(player, arg1, TYPE_PLAYER);
    match_me();
    match_player(NOTHING, NULL);
    match_neighbor();
    match_absolute();
    if ((who = noisy_match_result()) == NOTHING)
      return;

  }

  if (Typeof(who) != TYPE_PLAYER)
  {
    notify(player, "Not a player.");
    return;
  }

  if (*class == '\0')
  {
    /* above search restricts this result to a PLAYER class */
    class = get_class(who);

    notify(player, tprintf("%s is %s %s", db[who].name,
		     (*class == 'O' || *class == 'A') ? "an" : "a", class));
    return;
  }

  /* find requested level assignment */
  i = name_to_class(class);
  if (i == 0)
  {
    notify(player, tprintf("'%s': no such classification", class));
    return;
  }
  newlevel = i;

  /* insure player has the power to make that assignment */
  /* root can reclass without restriction.  Directors can reclass people from 

     any lower rank to any other lower rank.  No other class may use this
     command. */

  if (!has_pow(player, who, POW_CLASS)
      || (Typeof(player) != TYPE_PLAYER)
      || ((newlevel >= db[player].pows[0]) && !is_root(player)))
  {
    log_important(tprintf("%s failed to: @class %s=%s", unparse_object_a(player,
			       player), unparse_object_a(who, who), class));
    notify(player, perm_denied());
    return;
  }

  /* root must remain a director.  This is a safety feature. */
  if (who == root && newlevel != CLASS_DIR)
  {
    notify(player,
	   tprintf("Sorry, player #%ld cannot resign their position.",
		   root));
    return;
  }
  log_important(tprintf("%s executed: @class %s=%s", unparse_object_a(player, player),
			unparse_object_a(who, who), class));
  /* db[who].flags &= ~TYPE_MASK; db[who].flags |= newlevel; */
  notify(player, tprintf("%s is now reclassified as: %s",
			 db[who].name, class_to_name(newlevel)));
  notify(who, tprintf("You have been reclassified as: %s",
		      class_to_name(newlevel)));
  if (!db[who].pows)
  {
    db[who].pows = malloc(sizeof(ptype) * 2);
    db[who].pows[1] = 0;
  }
  db[who].pows[0] = newlevel;
  for (i = 0; i < NUM_POWS; i++)
    set_pow(who, powers[i].num, powers[i].init[class_to_list_pos(newlevel)]);
}

void do_nopow_class(player, arg1, class)
dbref player;
char *arg1;
char *class;
{
  int i, newlevel;
  dbref who;

  if (*arg1 == '\0')
    who = player;
  else
  {
    init_match(player, arg1, TYPE_PLAYER);
    match_me();
    match_player(NOTHING, NULL);
    match_neighbor();
    match_absolute();
    if ((who = noisy_match_result()) == NOTHING)
      return;
  }
  if (Typeof(who) != TYPE_PLAYER)
  {
    notify(player, "Not a player.");
    return;
  }
  if (*class == '\0')
  {
    /* above search restricts this result to a PLAYER class */
    class = get_class(who);

    notify(player, tprintf("%s is %s %s", db[who].name,
		     (*class == 'O' || *class == 'A') ? "an" : "a", class));
    return;
  }

  /* find requested level assignment */
  i = name_to_class(class);
  if (i == 0)
  {
    notify(player, tprintf("'%s': no such classification", class));
    return;
  }
  newlevel = i;

  /* insure player has the power to make that assignment */
  /* root can reclass without restriction.  Directors can reclass people from 

     any lower rank to any other lower rank.  No other class may use this
     command. */
  if (!has_pow(player, who, POW_CLASS)
      || (Typeof(player) != TYPE_PLAYER)
      || (((newlevel >= db[player].pows[0]) ||
	 (db[who].pows && db[who].pows[0] > newlevel)) && !is_root(player)))
  {
    log_important(tprintf("%s failed to: @class %s=%s", unparse_object_a(player, player),
			  unparse_object_a(who, who), class));
    notify(player, perm_denied());
    return;
  }

  /* root must remain a director.  This is a safety feature. */
  if (who == root && newlevel != CLASS_DIR)
  {
    notify(player,
	   tprintf("Sorry, player #%ld cannot resign their position.",
		   root));
    return;
  }
  log_important(tprintf("%s executed: @nopow_class %s=%s", unparse_object(player, player),
			unparse_object_a(who, who), class));
  /* db[who].flags &= ~TYPE_MASK; db[who].flags |= newlevel; */
  notify(player, tprintf("%s is now reclassified as: %s",
			 db[who].name, class_to_name(newlevel)));
  notify(who, tprintf("You have been reclassified as: %s",
		      class_to_name(newlevel)));
  if (!db[who].pows)
  {
    db[who].pows = malloc(sizeof(ptype) * 2);
    db[who].pows[1] = 0;
  }
  db[who].pows[0] = newlevel;
}

void do_empower(player, whostr, powstr)
dbref player;
char *whostr, *powstr;
{
  ptype pow;
  ptype powval;
  dbref who;
  int k;
  char *i;

  if (Typeof(player) != TYPE_PLAYER)
  {
    notify(player, "You're not a player, silly!");
    return;
  }
  i = strchr(powstr, ':');
  if (!i)
  {
    notify(player, "Badly formed power specification. need powertype:powerval");
    return;
  }
  *(i++) = '\0';

  if (!string_compare(i, "yes"))
    powval = PW_YES;
  else if (!string_compare(i, "no"))
    powval = PW_NO;
  else if (!string_compare(i, "yeseq"))
    powval = PW_YESEQ;
  else if (!string_compare(i, "yeslt"))
    powval = PW_YESLT;
  else
  {
    notify(player, "The power value must be one of yes, no, yeseq, or yeslt");
    return;
  }
  pow = name_to_pow(powstr);
  if (!pow)
  {
    notify(player, tprintf("unknown power: %s", powstr));
    return;
  }
  who = match_thing(player, whostr);
  if (who == NOTHING)
    return;
  if (Typeof(who) != TYPE_PLAYER)
  {
    notify(player, "Not a player.");
    return;
  }
  if (!has_pow(player, who, POW_SETPOW))
  {
    log_important(tprintf("%s failed to: @empower %s=%s:%s",
			  unparse_object_a(player, player), unparse_object_a(who, who), powstr, i));
    notify(player, perm_denied());
    return;
  }
  if (get_pow(player, pow) < powval && !is_root(player))
  {
    notify(player, "But you yourself don't have that power!");
    return;
  }
  for (k = 0; k < NUM_POWS; k++)
  {
    if (powers[k].num == pow)
    {
      if (powers[k].max[class_to_list_pos(*db[db[who].owner].pows)] >= powval)
      {
	set_pow(who, pow, powval);
	log_important(tprintf("%s executed: @empower %s=%s:%s",
			      unparse_object_a(player, player), unparse_object_a(who, who), powstr, i));
	if (powval != PW_NO)
	{
	  notify(who, tprintf("You have been given the power of %s.", pow_to_name(pow)));
	  notify(player, tprintf("%s has been given the power of %s.", db[who].name, pow_to_name(pow)));
	}
	switch (powval)
	{
	case PW_YES:
	  notify(who, "You can use it on anyone");
	  break;
	case PW_YESEQ:
	  notify(who, "You can use it on people your class and under");
	  break;
	case PW_YESLT:
	  notify(who, "You can use it on people under your class");
	  break;
	case PW_NO:
	  notify(who, tprintf("Your power of %s has been removed.", pow_to_name(pow)));
	  notify(player, tprintf("%s's power of %s has been removed.", db[who].name, pow_to_name(pow)));
	  break;
	}
	return;
      }
      else
      {
	notify(player, "Sorry, that is beyond the maximum for that level.");
	return;
      }
    }
  }
  notify(player, "Internal error. Help.");
}
void do_powers(player, whostr)
dbref player;
char *whostr;
{
  dbref who;
  int k;
  char buf1[100];
  char buf2[2048];

  who = match_thing(player, whostr);
  if (who == NOTHING)
    return;
  if (Typeof(who) != TYPE_PLAYER)
  {
    notify(player, "Not a player.");
    return;
  }
  if (!controls(player, who, POW_EXAMINE) && player != who)
  {
    notify(player, perm_denied());
    return;
  }
  notify(player, tprintf("%s's powers:", db[who].name));
  for (k = 0; k < NUM_POWS; k++)
  {
    ptype m;
    char *l = "";

    m = get_pow(who, powers[k].num);
    switch (m)
    {
    case PW_YES:
      l = "|R!+ALL|";
      break;
    case PW_YESLT:
      l = "|M!+LESS|";
      break;
    case PW_YESEQ:
      l = "|Y!+EQUAL|";
      break;
    case PW_NO:
      continue;
    }

    if (l)
    {
      sprintf(buf1, "|C!+[||B!+%s||C!+:|%s|C!+]|", powers[k].name, l);
      sprintf(buf2, " ");
      strncat(buf2, "                 ", 20 - strlen(strip_color(buf1)));
      notify(player, tprintf("%s%s|G+%s|", buf1, buf2, powers[k].description));
    }
  }
  notify(player, "-- end of list --");
}
int old_to_new_class(lev)
int lev;
{
  switch (lev)
  {
  case 0x8:
    return CLASS_GUEST;
  case 0x9:
    return CLASS_VISITOR;
  case 0xA:
    return CLASS_CITIZEN;
  case 0xB:
    return CLASS_JUNOFF;
  case 0xC:
    return CLASS_OFFICIAL;
  case 0xD:
    return CLASS_BUILDER;
  case 0xE:
    return CLASS_ADMIN;
  case 0xF:
    return CLASS_DIR;
  default:
    return CLASS_VISITOR;
  }
}
void do_money(player, arg1, arg2)
dbref player;
char *arg1, *arg2;
{
  int amt, assets;
  dbref who;
  char *credits, buf[20];
  dbref total;
  dbref obj[NUM_OBJ_TYPES];
  dbref pla[NUM_CLASSES];

  if (*arg1 == '\0')
    who = player;
  else
  {
    init_match(player, arg1, TYPE_PLAYER);
    match_me();
    match_player(NOTHING, NULL);
    match_neighbor();
    match_absolute();
    if ((who = noisy_match_result()) == NOTHING)
      return;
  }

  if (!power(player, POW_EXAMINE))
  {
    if (*arg2 != '\0')
    {
      notify(player, "You don't have the authority to do that.");
      return;
    }

    if (player != who)
    {
      notify(player,
	     "You need a search warrant to do that.");
      return;
    }
  }

  /* calculate assets */
  calc_stats(who, &total, obj, pla);
  assets = obj[TYPE_EXIT] * exit_cost +		/* exits */
    obj[TYPE_THING] * thing_cost +	/* things (objects) */
    obj[TYPE_ROOM] * room_cost +	/* rooms */
    (obj[TYPE_PLAYER] - 1) * robot_cost;	/* robots */

  /* calculate credits */
  if (inf_mon(who))
  {
    amt = 0;
    credits = "UNLIMITED";
  }
  else
  {
    amt = Pennies(who);
    sprintf(buf, "%d credits.", amt);
    credits = buf;
  }

  notify(player, tprintf("Cash...........: %s", credits));
  notify(player, tprintf("Material Assets: %d credits.", assets));
  notify(player, tprintf("Total Net Worth: %d credits.", assets + amt));
  notify(player, " ");
  notify(player, "Note: material assets calculation is only an approximation (for now).");
}

void do_quota(player, arg1, arg2)
dbref player;
char *arg1, *arg2;
{
  dbref who;
  char buf[20];
  long owned, limit;

  /* Stop attempts to change quota without authority */
  if (*arg2)
    if (!power(player, POW_SETQUOTA))
    {
      notify(player,
	     "You don't have the authority to change someone's quota!");
      return;
    }

  if (*arg1 == '\0')
    who = player;
  else if ((who = lookup_player(arg1)) == NOTHING || Typeof(who) != TYPE_PLAYER)
  {
    notify(player, "who?");
    return;
  }

  if (Robot(who))
  {
    notify(player, "Robots don't have quotas!");
    return;
  }

  /* check players authority to control his target */
  if (!controls(player, who, POW_SETQUOTA))
  {
    notify(player, tprintf("You can't %s that player's quota.",
			   (*arg2) ? "change" : "examine"));
    return;
  }

  /* count up all owned objects */
  /* 
     owned = -1;  * a player is never included in his own quota * 

     for ( thing = 0; thing < db_top; thing++ )  
     { 
       if ( db[thing].owner == who ) 
         if ((db[thing].flags & (TYPE_THING|GOING)) != (TYPE_THING|GOING)) 
           ++owned;
     } 
   */

  owned = atol(atr_get(who, A_QUOTA)) - atol(atr_get(who, A_RQUOTA));

  if (inf_quota(who))
    notify(player, tprintf("Objects: %d   Limit: UNLIMITED", owned));
  else
  {
    /* calculate and/or set new limit */
    if (*arg2 == '\0')
      limit = owned + atol(atr_get(who, A_RQUOTA));
    else
    {
      limit = atol(arg2);

      /* stored as a relative value */
      sprintf(buf, "%ld", limit - owned);
      atr_add(who, A_RQUOTA, buf);
      sprintf(buf, "%ld", limit);
      atr_add(who, A_QUOTA, buf);
    }
    notify(player, tprintf("Objects: %ld   Limit: %ld", owned, limit));
  }
}

dbref *match_things(player, list)
dbref player;
char *list;
{
  char *x;
  static dbref npl[10000];
  char in[1000];
  char *inp = in;

  if (!*list)
  {
    notify(player, "You must give a list of things.");
    npl[0] = 0;
    return npl;
  }
  npl[0] = 0;
  strcpy(in, list);
  while (inp)
  {
    x = parse_up(&inp, ' ');
    if (!x)
      inp = x;
    else
    {
      if (*x == '{' && *(strchr(x, '\0') - 1) == '}')
      {
	x++;
	*(strchr(x, '\0') - 1) = '\0';
      }
      npl[++npl[0]] = match_thing(player, x);
      if (npl[npl[0]] == NOTHING)
	npl[0]--;
    }
  }
  return npl;
}

dbref *lookup_players(player, list)
dbref player;
char *list;
{
  char *x;
  static dbref npl[1000];	/* first is number of them. */
  char in[1000];
  char *inp = in;

  if (!*list)
  {
    notify(player, "You must give a list of players.");
    npl[0] = 0;
    return npl;
  }
  npl[0] = 0;
  strcpy(in, list);
  while (inp)
  {
    x = parse_up(&inp, ' ');
    if (!x)
      inp = x;
    else
    {
      npl[++npl[0]] = lookup_player(x);
      if (npl[npl[0]] == NOTHING)
      {
	notify(player, tprintf("I don't know who %s is.", x));
	npl[0]--;
      }
    }
  }
  return npl;
}

void do_misc(player, arg1, arg2)
dbref player;
char *arg1, *arg2;
{
}
