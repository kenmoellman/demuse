/* color.c --  Ken C. Moellman, Jr.
 *
 *  A large portion of this code was from the TinyMUSE 97 sourcecode, and has 
 *  been modified to support the pueblo client.  It was originally ansi.c 
 *  under the tm97 release.
 *
 */


#include <stdio.h>
#include <string.h>
#include "externs.h"
#include "db.h"

#ifdef PUEBLO_CLIENT

#include <ctype.h>
#include <varargs.h>
#include <stdio.h>
#include "interface.h"

char *make_font_string(char *, char *, int);
static void set_ca(int *, int);
char *color_pueblo(const char *);
/* char *normal = "\e[0m"; */
char *normal_pueb = "<font fgcolor=\"FFFFFF\" \"bgcolor=000000\">";
char *puebloize2(const char *);
char *strip_beep(char *);
char *scanchar(char);

#endif PUEBLO_CLIENT


#define CA_BRIGHT	1
#define CA_REVERSE	2
#define CA_UNDERLINE	4
#ifdef BLINK
#define CA_BLINK	8
#endif


static int color2num(char); 
static int is_foreground(int);
static int is_background(int);
char *make_num_string(int, int, int);
static void set_ca(int *, int);
char *color_escape(const char *);
char *normal_ansi = "\e[0m";
char *colorize(const char *, int, int);
char *strip_beep(char *);

char *colorize(str, strip, pueblo)
const char *str;
int strip;
int pueblo;
{
  char *s, *colorend, *colorstr, *escape, *html;
  char buf[65535];

  strcpy(buf, str);

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
	  char buf2[65535];

	  *s++ = '\0';
	  *colorstr++ = '\0';
	  *colorend++ = '\0';

	  strncpy(buf2, buf, 65535);

	  if (!strip)
	  {
#ifdef PUEBLO_CLIENT
            if(!pueblo)  /* if it's not pueblo, it's ansi. */
            {
#endif
	      escape = color_escape(s);
	      s += strlen(escape) + strlen(colorstr) + strlen(normal_ansi) - 2;
	      strcat(buf2, escape);
	      strcat(buf2, colorstr);
	      strcat(buf2, normal_ansi);
#ifdef PUEBLO_CLIENT
            }
            else  /* it's pueblo! */
            {
	      html = html_string(s);
	      s += strlen(html) + strlen(colorstr) + strlen(normal_html) - 2;
	      strcat(buf2, html);
	      strcat(buf2, colorstr);
	      strcat(buf2, normal_html);
            }
#endif
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

static int color2num(c)
char c;
{
  switch (c)
  {
  case '!':
    return 1;
    break;
  case 'u':
    return 4;
    break;
#ifdef BLINK
  case 'b':
    return 5;
    break;
#endif
  case 'r':
    return 7;
    break;
  case 'N':
    return 30;
    break;
  case 'R':
    return 31;
    break;
  case 'G':
    return 32;
    break;
  case 'Y':
    return 33;
    break;
  case 'B':
    return 34;
    break;
  case 'M':
    return 35;
    break;
  case 'C':
    return 36;
    break;
  case 'W':
    return 37;
    break;
  case '0':
    return 40;
    break;
  case '1':
    return 41;
    break;
  case '2':
    return 42;
    break;
  case '3':
    return 43;
    break;
  case '4':
    return 44;
    break;
  case '5':
    return 45;
    break;
  case '6':
    return 46;
    break;
  case '7':
    return 47;
    break;
  default:
    return -1;
  }
}

static int is_foreground(num)
int num;
{
  return (num >= 30 && num <= 37);
}

static int is_background(num)
int num;
{
  return (num >= 40 && num <= 47);
}

char *make_num_string(fore, back, ca)
int fore, back, ca;
{
  char buff[2048];

  strcpy(buff, "");

  if (fore > 0)
    sprintf(buff, "%s%d;", buff, fore);

  if (back > 0)
    sprintf(buff, "%s%d;", buff, back);

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

char *color_escape(s)
const char *s;
{
  char escape[2048];
  int foreground = 37;
  int background = 40;
  int attribs = 0;
  int num, valid;

  for (valid = 0; *s; s++)
  {
    num = color2num(*s);
    if (num > 0)
    {
      valid = 1;
      if (is_foreground(num))
	foreground = num;
      else if (is_background(num))
	background = num;
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

    sprintf(escape, "\e[%sm", make_num_string(foreground, background, attribs));
  }
  else
    strcpy(escape, "");

  return stralloc(escape);
}

char *strip_color(str)
const char *str;
{
  return colorize(str, 1, 0);
}

char *strip_color_nobeep(str)
char *str;
{
  return colorize(strip_beep(str), 1, 0);
}

char *parse_color(str, pueblo)
const char *str;
{
  return colorize(str, 0, pueblo);
}

char *parse_color_nobeep(str, pueblo)
char *str;
{
  return colorize(strip_beep(str), 0, pueblo);
}

char *strip_beep(str)
char *str;
{
  char buf[2048];
  char *s, *p;

  for (p = buf, s = str; *s; s++)
    if (*s != '\a')
      *p++ = *s;

  *p = '\0';

  return stralloc(buf);
}

char *truncate_color(str, num)
char *str;
int num;
{
  char buf[2048];
  char *s;
  int count = 0;

  strcpy(buf, str);

  for (s = buf; *s && count < num; s++)
  {
    if (*s == '|')
    {
      char *color;

      color = strchr(s, '+');
      if (color && *(color + 1) == '{')
      {
	char *end_curl;

	end_curl = strchr(color + 2, '}');
	if (end_curl && *(end_curl + 1) == '|')
	{
	  for (s = color + 2; *s != '}' && count < num; s++, count++) ;
	  if (count >= num)
	  {
	    *s++ = '}';
	    *s = '|';
	  }
	  else
	    s++;
	}
	else if (strchr(color + 1, '|'))
	{
	  for (s = color + 1; *s != '|' && count < num; s++, count++) ;
	  if (count >= num)
	    *s = '|';
	}
      }
      else if (color && strchr(color + 1, '|'))
      {
	for (s = color + 1; *s != '|' && count < num; s++, count++) ;
	if (count >= num)
	  *s = '|';
      }
    }
    else
      count++;
  }

  *s = '\0';

  return stralloc(buf);
}


#ifdef PUEBLO_CLIENT

char *html_exit(player, exit_name)
	dbref player;
	char *exit_name;
{
  char newname[1024];
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
        
  return tprintf("%s", newname);  
}

char *html_remove(player, msg)
	dbref player;
	char *msg;
{
  int i, flag = 0;
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
  return tprintf("%s", buildmsg);
}

char *html_conversion(player, oldmsg)
	dbref player;
	char *oldmsg;
{
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
  
  return tprintf("%s", buildmsg);
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
  switch (num)
  {
  case 4:
    return "under";
    break;
#ifdef BLINK
  case 5:
    return "blink";
    break;
#endif
  case 30:
  case 40:
    return "black";
    break;
  case 31:
  case 41:
    return "red";
    break;
  case 32:
  case 42:
    return "green";
    break;
  case 33:
  case 43:
    return "yellow";
    break;
  case 34:
  case 44:
    return "blue";
    break;
  case 35:
  case 45:
    return "magenta";
    break;
  case 36:
  case 76:
    return "cyan";
    break;
  case 37:
  case 47:
    return "white";
    break;
  default:
    return -1;
  }
}


#endif /* PUEBLO_CLIENT */
