/* who.c -- broken from bsd.c by Ken C. Moellman, Jr.  09/1999
 *
 * Why? because bsd.c should only be socketing. And who isn't
 * socket handling.
 */


#include "config.h"
#include "externs.h"

#include <ctype.h>

char *who_format P((char *, int, dbref));

char motd[2048];
char motd_who[11]; /* we'd better never get to 10 digit dbrefs! */


#define  WHO_BUF_SIZ	500
#define  DEF_SCR_COLS	78	/* must be less than WHO_BUF_SIZ */
#define  MIN_SEC_SPC	2
#define  MIN_GRP_SPC	4

#define  DEF_WHO_FLAGS	"nafoid"	/* close to the old WHO output format */
#define  DEF_WHO_ALIAS	""	/* what people see if no alias is set */

#define  W_NAME		0x001
#define  W_ALIAS	0x002
#define  W_FLAGS	0x004
#define  W_ONFOR	0x008
#define  W_IDLE		0x010
#define  W_CONCID	0x020
#define  W_HOST		0x040
#define  W_PORT         0x080
#define  W_DOING        0x100

static char who_flags[] = "nafoichpd";

static int who_sizes_small[] =
{10, 6, 4, 9, 4, 5, 20, 6, 40};

static int who_sizes_large[] =
{16, 16, 4, 9, 4, 13, 32, 6, 40};
char who_bh_buf[33];

/*
static char *who_fmt[WHO_SIZE];

static char *who_fmt_small[] =
{
  "%s ", "%-6.6s ", "%-4.4s ",
  "%9.9s ", "%4.4s ", "%-5.5s ", "%-20.20s ", "%-6s ", "%s ",
};
static char *who_fmt_large[] =
{
  "%s ", "%-16.16s ", "%-4.4s ", "%9.9s ", "%4.4s ", "%-13.13s ",
  "%-32.32s ", "%-6s ", "%s "};
*/

#define  WHO_SIZE	9
static int who_sizes[WHO_SIZE];

extern char *spc(num)
int num;
{
  char buf[64];
  char *s;
  int i;

  for (s = buf, i = 0; i < num; i++, s++)
    *s = ' ';

  *s = '\0';

  return tprintf("%s", buf);
}

char *who_format(str, i, player)
char *str;
int i;
dbref player;
{
  char *buf;
  char *str2;

  if (player == 0)
    str2 = stralloc(str);
  else
  {
#ifdef WHO_IDLE_COLOR
    if (player && (player > -1 && player < 999999) && (db[player].flags & PLAYER_IDLE) && (i != 0))
      str2 = tprintf("|R+%s|",strip_color(str));
    else
#endif /* WHO_IDLE_COLOR */
      str2 = tprintf("%s",str);
  }


  if (who_sizes[i] < strlen(strip_color(str2)))
  {
    buf = truncate_color(str2, who_sizes[i]);
  }
  else if (who_sizes[i] > strlen(strip_color(str2)))
  {
    if ((i == 3) || (i == 4) || (i == 5))
    {
      buf = tprintf("%s%s", spc(who_sizes[i] - strlen(strip_color(str2))), str2);
    }
    else
    {
      buf = tprintf("%s%s", str2, spc(who_sizes[i] - strlen(strip_color(str2))));
    }
  }
  else 
  {
    buf = str2;
  }

  return (buf);
}

void dump_users(w, arg1, arg2, k)
dbref w;
char *arg1, *arg2;
struct descriptor_data *k;	/* if non-zero, use k instead of w */
{
  char *p, *b, *names, *longline;
  char buf[WHO_BUF_SIZ + 1];
  struct descriptor_data *d;
  char fbuf[5], flags[WHO_SIZE + 1];
  int done, pow_who, hidden = 0, see_player;
  int i, ni, bit, bits, num_secs, scr_cols, hc, header;
  int grp, grp_len, num_grps, total_count, hidden_count, inactive_count;
  dbref who, nl_size, *name_list;

  strcpy(flags, DEF_WHO_FLAGS);

  /* setup special data */
  names = "";
  scr_cols = DEF_SCR_COLS;
  if (!k)
  {
    if (Typeof(w) != TYPE_PLAYER && !payfor(w, 50))
    {
      notify(w, "You don't have enough pennies.");
      return;
    }

    if (*(p = atr_get(w, A_COLUMNS)) != '\0')
    {
      scr_cols = atoi(p);
      if (scr_cols > WHO_BUF_SIZ + 1)
	scr_cols = WHO_BUF_SIZ;
    }
    if (*(p = atr_get(w, A_WHOFLAGS)) != '\0')
    {
      *flags = '\0';
      strncat(flags, p, WHO_SIZE);
    }
    /* in case this value used, we must process names before atr_get is
       called again */
    if (*(p = atr_get(w, A_WHONAMES)) != '\0')
      names = p;
  }

  /* override data */
  if (arg1 != NULL)
    if (*arg1 != '\0')
    {
      *flags = '\0';
      strncat(flags, arg1, WHO_SIZE);
    }
  if (arg2 != NULL)
    if (*arg2 != '\0')
      names = arg2;

  /* Check for power to distinguish class names */
  pow_who = (k) ? 0 : has_pow(w, NOTHING, POW_WHO);

  /* process flags */
  bits = 0;
  grp_len = 0;
  num_secs = 0;
  for (p = flags; *p != '\0'; p++)
  {
    for (i = 0, bit = 1; i < WHO_SIZE; i++, bit <<= 1)
      if (to_lower(*p) == who_flags[i])
      {
	++num_secs;
	bits |= bit;
	if (islower(*p))
	{
	  who_sizes[i] = who_sizes_small[i];
	}
	else
	{
	  who_sizes[i] = who_sizes_large[i];
	}
	grp_len += who_sizes[i];
	break;
      }
    if (i == WHO_SIZE)
    {
      sprintf(buf, "%c: bad who flag.%s", (int)*p, (k) ? "\n" : "");
      if (k)
	queue_string(k, strip_color(buf));
      else
	notify(w, buf);
      return;
    }
  }

  /* process names (before atr_get called that overwrites static buffer) */
  if (*names == '\0')
  {
    /* avoid error msg in lookup_players() for missing name list */
    nl_size = 0;
    name_list = &nl_size;
  }
  else
  {
    name_list = lookup_players((k) ? NOTHING : w, names);
    if (!name_list[0])
      return;			/* we tried, but no users matched. */
  }

  /* add in min section spacing */
  grp_len += (num_secs - 1) * MIN_SEC_SPC;

  /* calc # of groups */
  num_grps = ((scr_cols - grp_len) / (grp_len + MIN_GRP_SPC)) + 1;
  if (num_grps < 1)
    num_grps = 1;

  /* calc header size based on see-able users */
  for (hc = 0, d = descriptor_list; d; d = d->next)
  {
    if (d->state != CONNECTED || d->player <= 0)
      continue;
    if (*atr_get(d->player, A_LHIDE))
    {
      if (k)
	continue;
      if (!controls(w, d->player, POW_WHO) && !could_doit(w, d->player, A_LHIDE))
	continue;
    }
#ifdef USE_BLACKLIST
    if (!( ((!strlen(atr_get(real_owner(d->player), A_BLACKLIST))) && (!strlen(atr_get(real_owner(w), A_BLACKLIST)))) ||
         !((could_doit(real_owner(w), real_owner(d->player), A_BLACKLIST)) && (could_doit(real_owner(d->player), real_owner(w), A_BLACKLIST))) ))
      continue;
#endif /* USE_BLAKLIST */

    if (name_list[0] > 0)
    {
      for (i = 1; i <= name_list[0]; i++)
	if (d->player == name_list[i])
	  break;
      if (i > name_list[0])
	continue;
    }
    if (++hc >= num_grps)
      break;
  }

  /* process names or, (if no names requested) loop once for all names */
  b = buf;
  grp = 1;
  header = 1;
  ni = 1;
  who = NOTHING;
  done = 0;
  total_count = inactive_count = hidden_count = 0;

  notify(w, "|C+--||C!+<||W+ Who List ||C!+>||C+-----------------------------------------------------|");
  while (!done)
  {
    /* if a player is on the names list, process them */
    if (ni <= name_list[0])
      who = name_list[ni++];
    /* if no names left on list, this is final run through loop */
    if (ni > name_list[0])
      done = 1;

    /* Build output lines and print them */
    d = descriptor_list;
    while (d)
    {
      if (!header)
      {
	/* skip records that aren't in use */
	/* if name list, skip those not on list */
	if (d->state != CONNECTED || d->player <= 0 ||
	    (who != NOTHING && who != d->player))
	{
	  d = d->next;
	  continue;
	}
#ifdef USE_BLACKLIST
        if (!( ((!strlen(atr_get(real_owner(d->player), A_BLACKLIST))) && (!strlen(atr_get(real_owner(w), A_BLACKLIST)))) ||
             !((could_doit(real_owner(w), real_owner(d->player), A_BLACKLIST)) && (could_doit(real_owner(d->player), real_owner(w), A_BLACKLIST))) ))
	{
	  d = d->next;
	  continue;
	}
#endif /* USE_BLACKLIST */

	/* handle hidden players, keep track of player counts */
	hidden = 0;
	see_player = 1;

	if (k ? *atr_get(d->player, A_LHIDE) : !could_doit(w, d->player, A_LHIDE))
	{
	  hidden = 1;
	  see_player = !k && controls(w, d->player, POW_WHO);
	}

	/* do counts */
	if (see_player || name_list[0] == 0)
	{
	  ++total_count;
	  if (hidden)
	    ++hidden_count;
	}

	if (!see_player)
	{
	  d = d->next;
	  continue;
	}
      }

      /* build output line */
      for (i = 0, bit = 1; i < WHO_SIZE; i++, bit <<= 1)
      {
	switch (bits & bit)
	{
	case W_NAME:
	  if (header)
	  {
	    p = who_format("Name", i, 0);
	  }
	  else
          {
	    if (db[d->player].pows && (db[d->player].pows[0] > CLASS_ADMIN))
              p = who_format(tprintf("|W!+@| %s", db[d->player].cname),i, d->player);
	    else if (db[d->player].pows && (db[d->player].pows[0] > CLASS_CITIZEN))
              p = who_format(tprintf("|C!+#| %s", db[d->player].cname),i, d->player);
            else 
              p = who_format(db[d->player].cname, i, d->player);
          }
	  break;
	case W_ALIAS:
	  if (header)
	    p = who_format("Alias",i,0);
	  else
          {
            if (Typeof(d->player) != TYPE_PLAYER)
	      p = tprintf("#%ld", d->player);
	    else
            {
              if (*(p = atr_get(d->player, A_ALIAS)) == '\0')
	        p = DEF_WHO_ALIAS;
            }
            p = who_format(p, i, d->player);
          } 
	  break;
	case W_FLAGS:
	  if (header)
	    p = who_format("Flg",i,0);
	  else
	  {
	    fbuf[0] = (hidden) ? 'h' : ' ';
	    fbuf[1] = (k ? *atr_get(d->player, A_LPAGE) :
		       !could_doit(w, d->player, A_LPAGE)) ? 'H' : ' ';
	    fbuf[2] = (db[d->player].flags & PLAYER_NO_WALLS) ? 'N' : ' ';
	    fbuf[3] = (db[d->player].flags & PLAYER_IDLE) ? 'i' : ' ';
	    fbuf[4] = '\0';
	    p = fbuf;
            p = who_format(p, i, d->player);
	  }
	  break;
	case W_DOING:
	  if (header)
	    p = who_format("Doing", i, 0);
	  else
	  {
	    char buff[MAX_BUFF_LEN];
	    char *bf = buff;

	    pronoun_substitute(buff, w, atr_get(d->player, A_DOING),
			       d->player);
	    bf += strlen(db[w].name) + 1;
	    p = who_format(bf, i, d->player);
	  }
	  break;
	case W_ONFOR:
	  p = header ? who_format("On For",i,0) : who_format(time_format_1(now - d->connected_at),i,d->player);
	  break;
	case W_IDLE:
	  p = header ? who_format("Idle",i,0) : who_format(time_format_2(now - d->last_time),i,d->player);
	  break;
	case W_CONCID:
	  if (header)
	    p = who_format("Concid",i,0);
	  else
          {
	    if ((!k) && (db[w].pows && (db[w].pows[0] == CLASS_DIR)))
            {
	      p = who_format(tprintf("%ld", d->concid),i,d->player);
            }
	    else
	    {
              p = who_format(tprintf("concid"), i, d->player);
	    }
          }

	  break;
	case W_HOST:
	  p = "?";
	  if (header)
	    p = who_format("Hostname",i,0);
/*	  else if ((!k) && (db[w].pows && (db[w].pows[0] == CLASS_DIR))) */
          else if ((controls(w, d->player, POW_WHO)) )
	  {
            
              char dah[4096];  /* build in ample runover space. */
	      sprintf(dah,"%s@%s", d->user, d->addr);
	      if (who_sizes[i] > who_sizes_small[i])
	      {
                char llama[4096];

	        sprintf(llama,"[");
                strncat(llama, dah, who_sizes[i] - 2);
	        strcat(llama, "]");
                strcpy(dah,llama);
              }
              p = who_format(dah,i,d->player);
          }
	  else
	  {
            p = who_format(tprintf("[unknown]"), i, d->player);
	  }
	  break;
	case W_PORT:
	  if (header)
	    p = who_format("Port",i,0);
	  else
	    p = who_format(tprintf("%d", ntohs(d->address.sin_port)),i,d->player);
	  break;
	default:
	  continue;
	}
        sprintf(b, "%s ", p);
 	b += strlen(p) + 1; 
      }
      b--;

      if (header)
	--hc;

      if ((!header && ++grp <= num_grps) || (header && hc))
      {
	/* make space between groups */
	strcpy(b, "    ");
	b += 4;
      }
      else
      {
	/* trim excess spaces */
	while (*--b == ' ')
	  ;

	/* print output line */
	if (k)
	{
	  *++b = '\n';
	  *++b = '\0';
	  queue_string(k, strip_color(buf));
	}
	else
	{
	  *++b = '\0';
	  notify(w, buf);
	}

	/* reset for new line */
	grp = 1;
	b = buf;
      }

      if (header)
      {
	if (grp == 1 && hc <= 0)
	  hc = header = 0;
      }
      else
      {
        if ( (d->player) && (d->player > -1 && d->player < 999999) && (db[d->player].flags & PLAYER_IDLE) )
        {
          inactive_count++;
        }
	d = d->next;
      }
    }
  }

  /* print last line (if incomplete) */
  if (grp > 1)
  {
    /* trim extra spaces */
    while (*--b == ' ')
      ;

    if (k)
    {
      *++b = '\n';
      *++b = '\0';
      queue_string(k, strip_color(buf));
    }
    else
    {
      *++b = '\0';
      notify(w, buf);
    }
  }

  b = buf;
  sprintf(b, "|C!+Users:| |Y!+Total:| |G!+%d||Y!+, Active:| |G!+%d||Y!+, Hidden:| |G!+%d||Y!+.| |C!+Avg:| ", total_count, (total_count - inactive_count), hidden_count);

  {
    int avgidle = 0, count = 0, avgonfor = 0;

    for (d = descriptor_list; d; d = d->next)
      if (d->state == CONNECTED)
      {
	count++;
	avgidle += now - d->last_time;
	avgonfor += now - d->connected_at;
      }
    if (count)
    {
      avgidle /= count;
      avgonfor /= count;
      sprintf(b + strlen(b), "|Y!+Idle:| |G!+%s||Y!+, OnFor:| |G!+%s||Y!+.|", stralloc(time_format_1(avgidle)), stralloc(time_format_1(avgonfor)));
    }
  }

  longline = tprintf("-------------------------------------------------------------------");

  if (k)
  {
    queue_string(k, tprintf("|C+%s|\n", longline));
    queue_string(k, buf);
    queue_string(k, "\n");
  }
  else
  {
    dbref messenger = atol(tprintf("%s",motd_who + 1));

    notify(w, tprintf("|C+%s|", longline));
    notify(w, buf);
    if ( (motd && *motd)
#ifdef USE_BLACKLIST
      && ( ((!strlen(atr_get(real_owner(messenger), A_BLACKLIST))) && (!strlen(atr_get(real_owner(w), A_BLACKLIST)))) ||
             (!((could_doit(real_owner(w), real_owner(messenger), A_BLACKLIST)) && (could_doit(real_owner(messenger), real_owner(w), A_BLACKLIST)))) )  
#endif /* USE_BLACKLIST */
       )
    {
      char *from;
      char *newstring;
      char *trunc;

      notify(w, "|C+--||C!+<| |W+Message of The Day| |C!+>||C+-------------------------------------------|");

      notify(w, motd);
  
      if (messenger < 0)
        from = tprintf("|W!+Anonymous|");
      else
        from = tprintf("%s", db[messenger].cname);
  
      trunc = truncate_color(from, 16);
  
      newstring = tprintf("|C+---------------------------------------------%s||C!+<| %s |C!+>||C+--|",truncate_color(longline, (16 - strlen(strip_color_nobeep(from)))), from);
    
  
      notify(w, newstring);
    }
  }
}
