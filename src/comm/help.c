/* help.c */
/* $Id: help.c,v 1.11 1993/09/18 19:03:41 nils Exp $ */

#include <stdio.h>

/* commands for giving help */

#include "db.h"
#include "config.h"
#include "interface.h"
#include "externs.h"
#include "help.h"
#ifdef sun
#include <errno.h>
#else
#include <sys/errno.h>
#endif

static void do_help P((dbref, char *, char *, char *, char *, ATTR *));

void do_text(player, arg1, arg2, trig)
dbref player;
char *arg1;
char *arg2;
ATTR *trig;
{
  char indx[20];
  char text[20];

  if (strlen(arg1) < 1 || strlen(arg1) > 10 || strchr(arg1, '/') || strchr(arg1, '.'))
  {
    notify(player, "Illegal text file.");
    return;
  }
  sprintf(indx, "msgs/%sindx", arg1);
  sprintf(text, "msgs/%stext", arg1);
  do_help(player, arg2, arg1, indx, text, trig);
}

static void do_help(player, arg1, deftopic, indxfile, textfile, trigthis)
dbref player;
char *arg1;
char *deftopic;
char *indxfile;
char *textfile;
ATTR *trigthis;
{
  int help_found;
  help_indx entry;
  FILE *fp;
  char *p, line[LINE_SIZE + 1];
  char tempy[90];
  int tempx;

  if (*arg1 == '\0')
    arg1 = deftopic;

  if ((fp = fopen(indxfile, "r")) == NULL)
  {
    if ((fp = fopen(textfile, "r")) == NULL)
    {
      if (errno == ENOENT)
      {
	/* no such file or directory. no such text bundle. */
	notify(player, tprintf("No such file or directory: %s", textfile));
	return;
      }
      else
      {
	log_error(tprintf("%s: errno %d", textfile, errno));
	notify(player, tprintf("%s: error number %d", textfile, errno));
	perror(textfile);
	return;
      }
    }
    else
    {
      notify(player, tprintf("%s isn't indexed.", textfile));
      fclose(fp);
      return;
    }
  }
  while ((help_found = fread(&entry, sizeof(help_indx), 1, fp)) == 1) 
    if (string_prefix(entry.topic, arg1))
      break;
  fclose(fp);
  if (help_found <= 0)
  {
    notify(player, tprintf("No %s for '%s'.", deftopic, arg1));
    return;
  }

  if ((fp = fopen(textfile, "r")) == NULL)
  {
    notify(player,
	   tprintf("%s: sorry, temporarily not available.", deftopic));
    log_error(tprintf("can't open %s for reading", textfile));
    return;
  }
  if (fseek(fp, entry.pos, 0) < 0L)
  {
    notify(player,
	   tprintf("%s: sorry, temporarily not available.", deftopic));
    log_error(tprintf("seek error in file %s", textfile));
    fclose(fp);
    return;
  }
  sprintf(tempy,"------------------------------------------------------------------------------");
#ifdef DEBUG
  notify(player,tprintf("arg1: %s %d, deftopic: %s %d",arg1,strlen(arg1),deftopic,strlen(deftopic)));
#endif
  tempx = 36-((strlen(deftopic)+strlen(entry.topic))/2);
  tempy[tempx] = '\0';
  notify(player, tprintf("%s %s on %s %s",tempy,deftopic,entry.topic,tempy));
  for (;;)
  {
    if (fgets(line, LINE_SIZE, fp) == NULL)
      break;
    if (line[0] == '&')
      break;
    for (p = line; *p != '\0'; p++)
      if (*p == '\n')
	*p = '\0';
    notify(player, line);
  }
  tempx = (strlen(tempy) * 2) + strlen(entry.topic) + strlen(deftopic) + 6;
  sprintf(tempy,"---------------------------------------------------------------------------------------");
  tempy[tempx] = '\0';
  notify(player, tempy);
  fclose(fp);
  if (trigthis)
  {
    dbref zon;

    wptr[0] = entry.topic;
    did_it(player, player, NULL, NULL, NULL, NULL, trigthis);
    DOZONE(zon, player)
      did_it(player, zon, NULL, NULL, NULL, NULL, trigthis);
  }
}

void do_motd(player)
dbref player;
{
  struct descriptor_data *d;

  d = descriptor_list;
  while (d && d->player != player)
    d = d->next;

  connect_message(d, motd_msg_file, 0);
}
