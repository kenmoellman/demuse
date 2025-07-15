/* log.c */
/* $Id: log.c,v 1.7 1993/12/19 17:59:51 nils Exp $ */

#include <stdio.h>
#include "db.h"
#include "config.h"
#include "externs.h"
#include "log.h"

struct log
  important_log = {NULL, -1, "logs/important", "log_imp"}, 
  sensitive_log = {NULL, -1, "logs/sensitive", "*log_sens"}, 
  error_log     = {NULL, -1, "logs/error", "log_err"}, 
  io_log        = {NULL, -1, "logs/io", "*log_io"}, 
  gripe_log     = {NULL, -1, "logs/gripe", "log_gripe"}, 
  force_log     = {NULL, -1, "logs/force", "*log_force"}, 
  prayer_log    = {NULL, -1, "logs/prayer", "log_prayer"}, 
  command_log   = {NULL, -1, "logs/commands", NULL}, 
  combat_log    = {NULL, -1, "logs/combat", "log_combat"}, 
  suspect_log   = {NULL, -1, "logs/suspect", "*log_suspect"}
 ;

struct log *logs[] =
{
  &important_log,
  &sensitive_log,
  &error_log,
  &io_log,
  &gripe_log,
  &force_log,
  &prayer_log,
  &command_log,
  &combat_log,
  &suspect_log,
  0
};

void muse_log(l, str)
struct log *l;
const char *str;
{
  struct tm *bdown;

  if (l->com_channel)
  {
    char buf[2048];
    char channel[1024];
    
    sprintf(channel, "%s", l->com_channel);
    sprintf(buf, "|Y!+*| %s", str);
    com_send(channel, buf);
  }

  if (!l->fptr)
  {
    l->fptr = fopen(l->filename, "a");
    if (!l->fptr)
    {
      mkdir("logs", 0755);
      l->fptr = fopen(l->filename, "a");
      if (!l->fptr)
      {
	fprintf(stderr, "BLEEEP! couldn't open log file %s\n", l->filename);
	return;
      }
    }
  }

  bdown = localtime((time_t *) & now);
  fprintf(l->fptr, "%02d/%02d %02d:%02d:%02d| %s\n", bdown->tm_mon + 1, bdown->tm_mday, bdown->tm_hour, bdown->tm_min, bdown->tm_sec, strip_color(str));
  fflush(l->fptr);
  if (l->counter-- < 0)
  {
    l->counter = 32767;
    if (l->fptr)
      fclose(l->fptr);
    l->fptr = NULL;
    if (l == &command_log)
    {
      static char oldfilename[64];

      sprintf(oldfilename, "%s.%ld", l->filename, now);
      unlink(oldfilename);
      rename(l->filename, oldfilename);
    }
  };
}

void close_logs()
{
  int i;

  for (i = 0; logs[i]; i++)
    if (logs[i]->fptr)
      fclose(logs[i]->fptr);
}

void suspectlog(player, command)
dbref player;
char *command;
{
  FILE *fptr;
  struct tm *bdown;

  bdown = localtime((time_t *) & now);
  fptr = fopen(tprintf("logs/suspect.%ld",player),"a");
/*  fputs(tprintf("%s\n",add_stamp(command)), fptr); */
  fprintf(fptr, "%02d/%02d %02d:%02d:%02d| %s\n", bdown->tm_mon + 1, bdown->tm_mday, bdown->tm_hour, bdown->tm_min, bdown->tm_sec, strip_color(command));
  fflush(fptr);
  fclose(fptr);
  log_suspect(tprintf("%s: %s",db[player].cname, command));
}

