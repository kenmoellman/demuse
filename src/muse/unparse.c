/* unparse.c */
/* $Id: unparse.c,v 1.7 1993/09/18 19:04:14 nils Exp $ */

#include "db.h"

#include "externs.h"
#include "config.h"
#include "interface.h"

char *unparse_flags(thing)
dbref thing;
{
  int c;
  char buf[BUFFER_LEN];
  char *p;
  char *type_codes = "RTEU----PPPPPPPP";

  p = buf;
  c = type_codes[Typeof(thing)];
  if (c != '-')
    *p++ = c;
  if (db[thing].flags & ~TYPE_MASK)
  {
    /* print flags */
    if (db[thing].flags & GOING)
      (p = buf, *p++ = 'G');
    if (db[thing].flags & PUPPET)
      *p++ = 'p';
    if (db[thing].flags & STICKY)
      *p++ = 'S';
    if (db[thing].flags & DARK)
      *p++ = 'D';
    if (db[thing].flags & LINK_OK)
      *p++ = 'L';
    if (db[thing].flags & HAVEN)
      *p++ = 'H';
    if (db[thing].flags & CHOWN_OK)
      *p++ = 'C';
    if (db[thing].flags & ENTER_OK)
      *p++ = 'e';
    if (db[thing].flags & SEE_OK)
      *p++ = 'v';
    /* if(db[thing].flags & UNIVERSAL) *p++='U'; */
    if (db[thing].flags & OPAQUE)
      if (Typeof(thing) == TYPE_EXIT)
	*p++ = 'T';
      else
	*p++ = 'o';
    if (db[thing].flags & INHERIT_POWERS)
      *p++ = 'I';
    if (db[thing].flags & QUIET)
      *p++ = 'q';
    if (db[thing].flags & BEARING)
      *p++ = 'b';
    if (db[thing].flags & CONNECT)
      *p++ = 'c';
    switch (Typeof(thing))
    {
    case TYPE_PLAYER:
      if (db[thing].flags & PLAYER_SLAVE)
	*p++ = 's';
      if (db[thing].flags & PLAYER_TERSE)
	*p++ = 't';
      if (db[thing].flags & PLAYER_MORTAL)
	*p++ = 'm';
/* get rid of NEWBIE flag. it's stupid.
      if (db[thing].flags & PLAYER_NEWBIE)
	*p++ = 'n';
*/
      if (db[thing].flags & PLAYER_NO_WALLS)
	*p++ = 'N';
      if (db[thing].flags & PLAYER_ANSI)
	*p++ = 'a';
      if (db[thing].flags & PLAYER_NOBEEP)
	*p++ = 'B';
/*
      if (db[thing].flags & PLAYER_WHEN)
	*p++ = 'w';
*/
      if (db[thing].flags & PLAYER_FREEZE)
	*p++ = 'F';
      if (db[thing].flags & PLAYER_SUSPECT)
	*p++ = '!';
      if (db[thing].flags & PLAYER_IDLE)
	*p++ = 'i';
      break;
    case TYPE_EXIT:
      if (db[thing].flags & EXIT_LIGHT)
	*p++ = 'l';
      break;
    case TYPE_THING:
      if (db[thing].flags & THING_KEY)
	*p++ = 'K';
      if (db[thing].flags & THING_DEST_OK)
	*p++ = 'd';
      /* if (db[thing].flags & THING_ROBOT) *p++='r'; */
      if (db[thing].flags & THING_SACROK)
	*p++ = 'X';
      if (db[thing].flags & THING_LIGHT)
	*p++ = 'l';
      break;
    case TYPE_ROOM:
      if (db[thing].flags & ROOM_JUMP_OK)
	*p++ = 'J';
      if (db[thing].flags & ROOM_AUDITORIUM)
	*p++ = 'A';
      if (db[thing].flags & ROOM_FLOATING)
	*p++ = 'f';
      break;
    }
  }
  *p = '\0';
  return stralloc(buf);
}
char *unparse_object_a(player, loc)
dbref player;
dbref loc;
{
  char *xx;
  char *zz = unparse_object(player, loc);

  xx = stack_em(strlen(zz) + 1);
  strcpy(xx, zz);
  return xx;
}

char *unparse_object(player, loc)
dbref player;
dbref loc;
{

  switch (loc)
  {
  case NOTHING:
    return "*NOTHING*";
  case HOME:
    return "*HOME*";
  default:
    if (loc < 0 || loc >= db_top)
    {
      return tprintf("<invalid #%ld>", loc);
    }
/* get rid of newbie, it's stupid.
    else if (!IS(player, TYPE_PLAYER, PLAYER_NEWBIE) ||
	     db[loc].owner == player)
*/
    else if ((db[loc].owner == player) ||
       (controls(player, loc, POW_EXAMINE) || can_link_to(player, loc,
         POW_EXAMINE) || (IS(loc, TYPE_ROOM, ROOM_JUMP_OK)) ||
         (db[loc].flags & CHOWN_OK) || (db[loc].flags & SEE_OK) ||
         power(player,POW_EXAMINE))) 
    {
      /* show everything */
      return tprintf("%s(#%ld%s)", db[loc].cname, loc, unparse_flags(loc));
    }
    else
    {
      /* show only the name */
      return stralloc(db[loc].cname);
    }
  }
}

char *unparse_object_caption(player, thing)
dbref player;
dbref thing;
{
  char buf[BUFFER_LEN];
  char buf2[BUFFER_LEN];

  switch (thing)
  {
  case NOTHING:
    return stralloc("*NOTHING*");
  case HOME:
    return stralloc("*HOME*");
  default:
    if (thing < 0 || thing >= db_top)
    {
      return tprintf("<invalid #%ld>", thing);
    }
    else
    {
      strcpy(buf, unparse_object_a(player, thing));
      if (*(atr_get(thing, A_TITLE)))
	strcat(buf, tprintf(" the %s", atr_get(thing, A_TITLE)));
      if (*(atr_get(thing, A_CAPTION)))
      {
	pronoun_substitute(buf2, player, atr_get(thing, A_CAPTION), thing);
	strcat(buf, buf2 + strlen(db[player].name));
      }
      return stralloc(buf);
    }
  }
}
