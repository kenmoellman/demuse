/* powers.c */
/* $Id: powers.c,v 1.3 1993/01/30 03:39:41 nils Exp $ */
 
#include "db.h"
#include "powers.h"
#include "externs.h"
#include <ctype.h>

static ptype powbuf[NUM_POWS * 2 + 2];
int get_pow(player, pow)
dbref player;
ptype pow;
{
  ptype *pows;

  if (is_root(player))
    return PW_YES;
  if (Typeof(player) != TYPE_PLAYER)
    if (!(db[player].flags & INHERIT_POWERS))
      return PW_NO;
  if (db[player].pows == NULL)
    return PW_NO;
  /* if(*db[player].pows) return PW_NO; */
  pows = db[player].pows;
  if (!pows)
    return PW_NO;
  if (Typeof(player) == TYPE_PLAYER)
    pows++;
  for (; *pows; pows += 2)
    if (*pows == pow)
    {
      return pows[1];
    }
  return PW_NO;
}
int has_pow(player, recipt, pow)
dbref player, recipt;
ptype pow;
{
  ptype pows;

  if (is_root(player))
  {
    return 1;
  }
  if (IS(player, TYPE_PLAYER, PLAYER_MORTAL))
  {
    return 0;
  }
  if (db[player].flags & INHERIT_POWERS)
    player = db[player].owner;
  pows = get_pow(player, pow);
  if ((pows == PW_YES) ||
      (recipt == NOTHING && (pows == PW_YESLT || pows == PW_YESEQ)) ||
      (pows == PW_YESLT && Levnm(recipt) < Level(player)) ||
      (pows == PW_YESEQ && Levnm(recipt) <= Level(player)))
    return 1;
  return 0;
}
static void del_pow(player, pow)
dbref player;
ptype pow;
{
  ptype *x;

  x = db[player].pows;
  if (!x)
    return;
  if (Typeof(player) == TYPE_PLAYER)
    x++;
  for (; *x; x += 2)
    if (*x == pow)
    {
      while (*x)
      {
	if (x[2])
	{
	  *x = x[2];
	  x[1] = x[3];
	  x += 2;
	}
	else
	  *x = x[2];
      }
      return;
    }
}

void set_pow(player, pow, val)
dbref player;
ptype pow;
ptype val;
{
  ptype *x;
  int nlist;			/* number of things in the powlist */

  del_pow(player, pow);
  if (val == PW_NO)		/* don't do anything! */
    return;
  if (!db[player].pows)
  {
//    db[player].pows = malloc(sizeof(ptype) * 3);
    SAFE_MALLOC(db[player].pows, ptype, 3);
    db[player].pows[0] = pow;
  }
  nlist = 0;
  x = db[player].pows;
  if (Typeof(player) == TYPE_PLAYER)
    x++, nlist++;
  for (; *x; x += 2, nlist += 2) ;
/*  bcopy(db[player].pows, powbuf, nlist * sizeof(ptype)); */
  memcpy(powbuf, db[player].pows, nlist * sizeof(ptype));
  SMART_FREE(db[player].pows);
  x = powbuf + nlist;
  /* didn't see it.. make it up */
  *x = pow;
  x++, nlist++;
  *x = val;
  x++, nlist++;
  *x = 0;
  nlist++;
//  db[player].pows = malloc(nlist * sizeof(ptype));
  SAFE_MALLOC(db[player].pows, ptype, nlist);
/*  bcopy(powbuf, db[player].pows, nlist * sizeof(ptype)); */
  memcpy(db[player].pows, powbuf, nlist * sizeof(ptype));
}
void get_powers(i, str)
dbref i;
char *str;
{
  int pos = 0;			/* pos in the powbuf */

  for (;;)
  {
    if (!strchr(str, '/'))
    {
      powbuf[pos++] = 0;
//      db[i].pows = malloc(pos * sizeof(ptype));
      SAFE_MALLOC(db[i].pows, ptype, pos);
/*      bcopy(powbuf, db[i].pows, pos * sizeof(ptype)); */
      memcpy(db[i].pows, powbuf, pos * sizeof(ptype));
      return;
    }
    if (isdigit(*str))
    {
      powbuf[pos++] = atoi(str);
    }
    else
      switch (*str)
      {
      case '<':
	powbuf[pos++] = PW_YESLT;
	break;
      case '=':
	powbuf[pos++] = PW_YESEQ;
	break;
      case 'y':
      case 'Y':
	powbuf[pos++] = PW_YES;
	break;
      default:
	powbuf[pos++] = PW_NO;
	break;
      }
    str = strchr(str, '/') + 1;
  }
}

void put_powers(f, i)
FILE *f;
dbref i;
{
  ptype *pows;

  if (!db[i].pows)
  {
    fputs("\n", f);
    return;
  }
  if (Typeof(i) == TYPE_PLAYER)
  {
    fprintf(f, "%d/", *db[i].pows);
    pows = db[i].pows + 1;
  }
  else
    pows = db[i].pows;
  for (;;)
    if (*pows)
    {
      fprintf(f, "%d/", *(pows++));
      switch (*(pows++))
      {
      case PW_YESLT:
	fputc('<', f);
	break;
      case PW_YESEQ:
	fputc('=', f);
	break;
      case PW_YES:
	fputc('y', f);
	break;
      default:			/* must be no */
	fputc('.', f);
	fprintf(f, "%d", *(pows - 1));
	break;
      }
      fputc('/', f);
    }
    else
    {
      fputs("0\n", f);
      return;
    }
}
