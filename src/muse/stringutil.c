/* stringutil.c */
/* $Id: stringutil.c,v 1.3 1993/01/30 03:39:49 nils Exp $ */

/* String utilities */
#include <ctype.h>

#include "db.h"
#include "config.h"
#include "externs.h"

#define DOWNCASE(x) to_lower(x)
int string_compare(s1, s2)
const char *s1;
const char *s2;
{
  while ((s1 && *s1) && (s2 && *s2) && (DOWNCASE(*s1) == DOWNCASE(*s2)))
  {
    s1++;
    s2++;
  }

  if (!(s1 && *s1))
  {
    if (!(s2 && *s2))
    {
      return 0;
    }
    else
    {
      return (0 - DOWNCASE(*s2));
    }
  }
  else
  {
    return (DOWNCASE(*s1) - DOWNCASE(*s2));
  }
}

int string_prefix(string, prefix)
const char *string;
char *prefix;
{
  while (*string && *prefix && DOWNCASE(*string) == DOWNCASE(*prefix))
    string++, prefix++;
  return *prefix == '\0';
}

/* accepts only nonempty matches starting at the beginning of a word */
char *string_match(src, sub)
char *src;
char *sub;
{
  if (*sub != '\0')
  {
    while (*src)
    {
      if (string_prefix(src, sub))
	return src;
      /* else scan to beginning of next word */
      while (*src && isalnum(*src))
	src++;
      while (*src && !isalnum(*src))
	src++;
    }
  }

  return 0;
}

char to_upper(x)
int x;
{
  if (x < 'a' || x > 'z')
    return x;
  return 'A' + (x - 'a');
}
char to_lower(x)
int x;
{
  if (x < 'A' || x > 'Z')
    return x;
  return 'a' + (x - 'A');
}
char *str_index(what, chr)
char *what;
int chr;
{
  char *x;

  for (x = what; *x; x++)
    if (chr == *x)
      return x;
  return (char *)0;
}
char *int_to_str(a)
int a;
{
  char buf[100];

  if (a != 0)
    sprintf(buf, "%d", a);
  else
    *buf = '\0';
  return stralloc(buf);
}
