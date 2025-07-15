/* pueblo.c    --    Ken C. Moellman, Jr    --    02/29/2000
 *
 *    This code is based upon ansi.c, by Ben Kotcher (NetRunner) and was
 * modified by Ken C. Moellman, Jr. to work with the pueblo client 
 * translation of color codes.  It's a pain in the ass.  It also changes
 * certain other characters into the needed HTML version (&, <, >, ")
 *
 * peices of pueblo.c came from another pueblo.c file, that I've forgotten
 * the source of.  This code was also modified to work with the demuse 
 * implementation of pueblo, which is at the descriptor level, not at the
 * player level.
 *
 * HTML is yucky.                                        -wm
 */ 


#include "config.h"
#ifdef PUEBLO_CLIENT

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <varargs.h>
#include <stdio.h>

#include "interface.h"
#include "externs.h"
#include "externs.h"
#include "db.h"


#define CA_BRIGHT	1
#define CA_REVERSE	2
#define CA_UNDERLINE	4
#ifdef BLINK
#define CA_BLINK	8
#endif


char *make_font_string(char *, char *, int);
static void set_ca(int *, int);
char *color_pueblo(const char *);
/* char *normal = "\e[0m"; */
char *normal = "<font fgcolor=\"FFFFFF\" \"bgcolor=000000\">";
char *puebloize2(const char *);
char *strip_beep(char *);

char *scanchar(char);



char *html_exit(player, exit_name)
	dbref player;
	char *exit_name;
{
  static char newname[1024];
  char name[1024];
  char alias[1024];
  int i, flag=0;

  strcpy(name, "");
  strcpy(alias, "");

  for(i=0;i<strlen(exit_name);i++) {
    if(flag == 0 && exit_name[i] != ';')
      strcat(name, tprintf("%c", exit_name[i]));
    else if(exit_name[i] == ';')
      flag++;
   else if(flag == 1 && exit_name[i] == ';')
      flag = 2;
    else if(flag == 1 && exit_name[i] != ';')
      strcat(alias, tprintf("%c", exit_name[i]));
    }

  if(*alias)
    strcpy(newname, tprintf("<a xch_cmd=\"%s\">%s</a>", alias, 
      html_conversion(player, name)));
  else
    strcpy(newname, tprintf("%s", html_conversion(player, name)));
        
  return newname;  
}

char *html_remove(player, msg)
	dbref player;
	char *msg;
{
  int i, flag = 0;
  static char newmsg[1024];
  char buildmsg[1024];

  strcpy(buildmsg, "");
  if(d->pueblo != 0)
    strcpy(buildmsg, tprintf("%s", msg));
  else {
    for(i=0;i<strlen(msg);i++) {
      if(flag == 0 && msg[i] != '<' && msg[i] != '>') 
        strcat(buildmsg, tprintf("%c", msg[i]));
      else if(msg[i] == '<')
        flag = 1;
      else if(msg[i] == '>' && flag == 1)
        flag = 0;      
    }
  }
  sprintf(newmsg, buildmsg);
  return newmsg;
}

char *html_conversion(player, oldmsg)
	dbref player;
	char *oldmsg;
{
  static char newmsg[1024];
  char buildmsg[1024];
  int i;

  strcpy(buildmsg, "");

  if(d->pueblo != 0) 
  {
    for(i=0;i<strlen(oldmsg);i++) 
    {
      if(oldmsg[i] == '"')
        strcat(buildmsg, "&quot;");   
      else if(oldmsg[i] == '&')
        strcat(buildmsg, "&amp;");
      else if(oldmsg[i] == '<')
        strcat(buildmsg, "&lt;");
      else if(oldmsg[i] == '>')
        strcat(buildmsg, "&gt;");
      else
        strcat(buildmsg, tprintf("%c", oldmsg[i]));
    }
  } else 
    strcpy(buildmsg, tprintf("%s", oldmsg));
  
  strcpy(newmsg, tprintf("%s", buildmsg));
  return newmsg;
}

char *puebloize(char *inp)
{
  int x;
  char *p, *q, *newchar;
  char oldstring[65535];
  char newstring[65535];
  char *s, *colorend, *colorstr, *escape;
  char buf[2048];

/* search through string, replace ansi and crap, and put in HTML instead. */
  x=0;
  strncpy(oldstring, inp, 65530);
  q = newstring;
  for (;(oldstring[x]!=0);)
  {
   /* this might be a bit buggy here. especially the moving q along newstring. */
    newchar = scanchar(oldstring[x]);
    x++;
    strcpy(q, newchar);
    q=q+strlen(newchar);
  }
  *q++='\0';

  strcpy(buf, newstring);

  for (s = buf; *s; s++)
  {
    if (*s == '|')
    {
      colorstr = strpbrk(s + 1, "+|");	/* Look and see if it's color. If it
					   is, colorstr points to the plus
					   before the string to be colorized. 

					 */
      if (colorstr && *colorstr == '|')		/* If it's not, keep going. */
	continue;
      else if (colorstr)
      {
	if (*(colorstr + 1) == '{')
	{			/* If the colorstr begins and ends with */
	  char *end_curl;

	  end_curl = strchr(colorstr + 1, '}');		/* curlies, ignore
							   any bars  */
	  if (end_curl && *(end_curl + 1) == '|')
	  {			/* within. */
	    *end_curl++ = '\0';
	    *colorstr++ = '\0';
	    colorend = end_curl;
	  }
	  else
	    colorend = strchr(colorstr + 1, '|');
	}
	else
	  colorend = strchr(colorstr + 1, '|');		/* Find the end of
							   the color. */
	if (colorend && *colorend)
	{			/* If it has an end, parse it. */
	  char buf2[2048];

	  *s++ = '\0';
	  *colorstr++ = '\0';
	  *colorend++ = '\0';

	  strcpy(buf2, buf);

	  if (!strip)
	  {
	    escape = color_pueblo(s);
	    s += strlen(escape) + strlen(colorstr) + strlen(normal) - 2;
	    strcat(buf2, escape);
	    strcat(buf2, colorstr);
	    strcat(buf2, normal);
	  }
	  else
	  {
	    s += strlen(colorstr) - 2;
	    strcat(buf2, colorstr);
	  }

	  strcat(buf2, colorend);
	  strcpy(buf, buf2);
	}
      }
    }
  }
  return stralloc(buf);
}

char *scanchar(char inp)
{
  char retval[6];
  switch(inp)
  {
    case '&':
      strcpy(retval, "&amp;");
      break;
    case '<':
      strcpy(retval, "&lt;");
      break;
    case '>':
      strcpy(retval, "&gt;");
      break;
    case '"':
      strcpy(retval, "&quot;");
      break;
    default:
      strcpy(retval, tprintf("%c", inp));
  }

  return(stralloc(retval));

}



char *make_font_string(char *fore, char *back, int ca)
{
  char buff[2048];
  char fgcolor[6];
  char bgcolor[6];
  char *retval;


  strcpy(fgcolor, "");
  strcpy(bgcolor, "");
  strcpy(buff, "");



  if (fore)
    strncpy(fgcolor, "fgcolor=%s ", buff, fore);

  if (back)
    sprintf(bgcolor, "bgcolor=%s ", buff, back);

  if (ca > 0)
  {
    if (ca & CA_BRIGHT)
      strcat(buff, "1;");
    if (ca & CA_REVERSE)
      strcat(buff, "7;");
    if (ca & CA_UNDERLINE)
      strcat(buff, "4;");
#ifdef BLINK
    if (ca & CA_BLINK)
      strcat(buff, "5;");
#endif
  }

  *strrchr(buff, ';') = '\0';

  return stralloc(buff);
}

static void set_ca(attribs, num)
int *attribs, num;
{
  if (num == 1)
    *attribs |= CA_BRIGHT;
  else if (num == 7)
    *attribs |= CA_REVERSE;
  else if (num == 4)
    *attribs |= CA_UNDERLINE;
#ifdef BLINK
  else if (num == 5)
    *attribs |= CA_BLINK;
#endif
  else
    log_error("Eeek! Something evil happened in set_ca!");
}

char *color_pueblo(s)
const char *s;
{
  char escape[2048];
  char *foreground = "FFFFFF";
  char *background = "000000";
  int attribs = 0;
  int num, valid;

  for (valid = 0; *s; s++)
  {
    num = color2num(*s);
    if (num > 0)
    {
      valid = 1;
      if (is_foreground(num))
	foreground = pueblo_color(num);
      else if (is_background(num))
	background = pueblo_color(num);
      else
	set_ca(&attribs, num);
    }
  }

  if (valid)
  {
    if (foreground == (background - 10))
      if (foreground == 30 && !(attribs & CA_BRIGHT))
	background = 47;
      else
	background = 40;
    sprintf(escape, "%s", make_font_string(foreground, background, attribs));
  }
  else
    strcpy(escape, "");

  return stralloc(escape);
}

char *pueblo_color(int num)
{

  

}


#endif /* PUEBLO_CLIENT */
