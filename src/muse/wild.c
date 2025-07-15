/* wild.c */
/* $Id: wild.c,v 1.3 1993/01/16 18:37:10 nils Exp $ */

/* wild card routine(s) created by Lawrence Foard */
#include <stdio.h>
#include <ctype.h>
#include "config.h"
#include "externs.h"

char *wptr[10];
int wlen[10];
char wbuff[2000];


/*
 * there could definately be a problem here.  We're passing in a static
 * in the wild_match() function, and passed into this function as p and
 * os.  Then, this function modifies p and os, which in itself is bad.
 * what makes things worse is that these values were passed in as a value
 * and not a variable containing a value.  So we're sdoing something 
 * VERY bad.  -- wm
 */
 
static int wild(s, d, p, os)
char *s;
char *d;
int p;				/* what argument are we on now? */
int os;				/* true if just came from wild card state */
{
  switch (*s)
  {
  case '?':			/* match any character in d, note end of
				   string is considered a match */
    /* if just in nonwildcard state record location of change */
    if (!os && (p < 10))
      wptr[p] = d;
    return (wild(s + 1, (*d) ? d + 1 : d, p, 1));
  case '*':			/* match a range of characters */
    if (s[1] == '*')
    {
      /* double star. bad. -shkoo */
      return 0;
    }
    if (!os && (p < 10))
    {
      wptr[p] = d;
    }
    return (wild(s + 1, d, p, 1) || ((*d) ? wild(s, d + 1, p, 1) : 0));
  default:
    if (os && (p < 10))
    {
      wlen[p] = d - wptr[p];
      p++;
    }
    return ((to_upper(*s) != to_upper(*d)) ? 0 :
	    ((*s) ? wild(s + 1, d + 1, p, 0) : 1));
  }
}

long wild_match(s, d)
char *s;
char *d;
{
  int a;

  for (a = 0; a < 10; a++)
    wptr[a] = NULL;

  switch (*s)
  {
  case '>':
    s++;
    /* if both first letters are #'s then numeric compare */
    if (isdigit(s[0]) || (*s == '-'))
      return (atol(s) < atol(d));
    else
      return (strcmp(s, d) < 0);
    break;
  case '<':
    s++;
    if (isdigit(s[0]) || (*s == '-'))
      return (atol(s) > atol(d));
    else
      return (long)(strcmp(s, d) > 0);
    break;
  default:
    if (wild(s, d, 0, 0))
    {
      int a, b;
      char *e, *f = wbuff;

      for (a = 0; a < 10; a++)
	if ((e = wptr[a]))
	{
	  wptr[a] = f;
	  for (b = wlen[a]; b--; *f++ = *e++)
	    ;
	  *f++ = 0;
	}
      return (1);
    }
    else
      return (0);
  }
  return 0;
}
