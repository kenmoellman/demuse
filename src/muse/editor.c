/* editor.c */
/* $Id: editor.c,v 1.4 1993/08/22 04:54:02 nils Exp $ */


#include "editor.h"

#include <stdio.h>
#include <sys/types.h>
#ifdef NeXT
#include <sys/dir.h>
#else
#include <dirent.h>
#endif
#include "interface.h"
#include "db.h"
#include "net.h"

char *make_pfile(dbref, char *);
static void prompt(struct descriptor_data *);




static void prompt(dsc)
struct descriptor_data *dsc;
{
  (void)queue_string(dsc, EPROMPT);
}

static void print(dsc, message, option)
struct descriptor_data *dsc;
char *message;
int option;			/* true if display for no prompts; false

				   otherwise */
{
  (void)queue_string(dsc, message);
  (void)queue_write(dsc, "\n", 1);
}
int valid_filename P((char *));
int valid_filename(fname)
char *fname;
{
  char *k;

  puts(fname);
  if (!fname || *fname == '\0')
  {
    puts("null");
    return 0;
  }
  if (!strncmp(fname, "../", 3) ||
      !strcmp(fname, ".."))
  {
    puts("back");
    return 0;
  }
  if ((k = strchr(fname, '/')))
    return valid_filename(k + 1);
  puts("ok.");
  return 1;
}

void do_mkdir(player, raw_fname)
dbref player;
char *raw_fname;
{

  if (!valid_filename(raw_fname))
  {
    notify(player, "Sorry, that isn't a valid filename.");
    return;
  }
  if (mkdir(make_pfile(player, raw_fname), 0755) < 0)
  {
    notify(player, tprintf("Error creating %s", raw_fname));
    return;
  }
  notify(player, tprintf("Directory %s created.", raw_fname));
}

void do_ls(player, raw_fname)
dbref player;
char *raw_fname;

/*
   {
   DIR *dirlist;
   #if  defined(SYSV) || defined(SYSV_MAYBE) || defined(HPUX)
   struct dirent *ent;
   #else
   struct direct *ent;
   #endif
   char *fname;
   char opbuf[1024];

   if (*raw_fname == '\0')
   raw_fname=".";
   else if(!valid_filename(raw_fname)) {
   notify(player,"Sorry, that isn't a valid filename.");
   return;
   }
   dirlist = opendir(fname = make_pfile(player,raw_fname));
   if(!dirlist) {
   notify(player, "Sorry, i can't open that file.");
   return;
   }
   while (ent = readdir (dirlist))
   if(strcmp(ent->d_name, "..")) {
   struct stat statbuf;
   sprintf(opbuf,"%s/%s",fname, ent->d_name);
   lstat(opbuf,&statbuf);
   sprintf(opbuf,"%-15s%c %d",
   ent->d_name,
   ((statbuf.st_mode&S_IFMT)==S_IFDIR)?'/':
   ((statbuf.st_mode&S_IFMT)==S_IFLNK)?'@':
   ' ',
   statbuf.st_size);
   notify(player, opbuf);
   }
   closedir (dirlist);
   }
 */
{
}
char *make_pfile(player, fname)
dbref player;
char *fname;
{
  static char op[1024];

  if (strlen(fname) > 900)
    fname[900] = '\0';
  sprintf(op, "files/p/%d/%s", player, fname);
  return op;
}

void do_editfile(player, fname)
dbref player;
char *fname;
{
  struct descriptor_data *dsc;
  char *name_buffer;
  char line_buff[80];
  FILE *fp;
  struct top *head;
  struct buffer *p;
  int count = 0;

  if (Typeof(player) != TYPE_PLAYER)
  {
    notify(player, "Excuse me, but you aren't a player.");
    return;
  }
  for (dsc = descriptor_list; dsc->state == CONNECTED && dsc->player != player;
       dsc = dsc->next) ;
  if (dsc == NULL)
  {
    notify(player, "But you don't seem to be connected!");
    return;
  }
  if (*fname == '\0')
  {
    notify(player, "Syntax: +edit <filename>");
    return;
  }
  if (!valid_filename(fname))
  {
    notify(player, "Sorry, that isn't a valid filename.");
    return;
  }
  /* make SURE filename does not overrun buffer, or disaster! */
  if (strlen(fname) + strlen(db[player].name) + 10 > 80)
  {
    fname[80 - strlen(db[player].name) - 10] = '\0';
  }
  name_buffer = make_pfile(player, fname);

  if ((fp = fopen(name_buffer, "r")) == NULL)
    if ((fp = fopen(name_buffer, "w")) == NULL)
    {
      notify(player, "Can't open/create file!");
      return;
    }
  fprintf(stderr, tprintf("Player %d(concid %d) opened %s for editing.\n",
			  player, dsc->concid, name_buffer));
  MALLOC(head, struct top, 1);
  MALLOC(head->next, struct buffer, 1);

  strcpy(head->filename, name_buffer);
  dsc->edit_buff = head;
  p = head->next;
  p->next = NULL;
  while (fgets(line_buff, 80, fp) != NULL)
  {
    MALLOC(p->next, struct buffer p, 1);

    p = p->next;
    line_buff[strlen(line_buff) - 1] = '\0';
    strcpy(p->line, line_buff);
    p->next = NULL;
    count++;
  }
  head->state = COMMAND;
  head->issaved = 1;

  fclose(fp);
  notify(player, "Welcome to MUSEdit V1.0");
  notify(player, "Type \"h\" for help.");
  notify(player, tprintf("Editing \"%s\", %d lines.", fname, count));
  prompt(dsc);
}

void parse_range(head, range, p1, p2, start_line)
struct top *head;
char *range;
struct buffer **p1;
struct buffer **p2;
int *start_line;

{
  char *c;
  int l1, l2, t, count;

  if (!*range)
  {				/* no arguments supplied */
    *p1 = head->next->next;
    *p2 = NULL;
    if (start_line)
      *start_line = 1;
    return;
  }
  c = strchr(range, '-');
  if ((l1 = atoi(range)) <= 0)
    l1 = 1;
  if (c)
  {
    *c++ = '\0';
    if ((l2 = atoi(c)) <= 0)
      l2 = 0;
  }
  else
    l2 = l1;
  if ((l1 > l2) && l2)
  {
    t = l1;
    l1 = l2;
    l2 = t;
  }
  *p1 = head->next->next;
  count = 1;
  while ((count < l1) && (*p1 != NULL))
  {
    count++;
    *p1 = (*p1)->next;
  }
  if (!l2)
  {
    *p2 = NULL;
  }
  else
  {
    for (*p2 = *p1; (count < l2) && (*p2 != NULL);
	 count++, *p2 = (*p2)->next) ;
  }
  if (start_line)
    *start_line = l1;
  return;
}

void do_ehelp(player)
dbref player;

{
  spit_file(player, EDIT_HELP_FILE, NULL);
  return;
}

void do_list(player, head, string)
dbref player;
struct top *head;
char *string;

{
  struct buffer *l, *u;

  parse_range(head, string, &l, &u, &(head->linenum));
  while ((l != NULL) && (u->next != l))
  {
    notify(player, tprintf("[%2d]: %s", head->linenum, l->line));
    l = l->next;
    head->linenum++;
  }
}

void do_esearch(player, head, string, case_sense)
dbref player;
struct top *head;
char *string;
int case_sense;

{
  int matches = 0, linenum = 0;
  struct buffer *s;
  char *t, *u, *v;

  for (s = head->next->next; s; s = s->next)
  {
    linenum++;
    t = s->line;
    while (*t)
    {
      u = t;
      v = string;
      while (*v && ((case_sense) ? *v : to_lower(*v))
	     == ((case_sense) ? *u : to_lower(*u)))
	v++, u++;
      if (*v == '\0')
      {
	matches++;
	notify(player,
	       tprintf("[%2d]: %s", linenum, s->line));
	break;
      }
      t++;
    }
  }
  if (matches == 0)
    notify(player, "No matches found.");
  else
    notify(player, tprintf("%d matches found.", matches));
}

void do_delete(dsc, player, head, string)
struct descriptor_data *dsc;
dbref player;
struct top *head;
char *string;

{
  struct buffer *l;

  if ((head->state == COMMAND) && !*string)
  {
    print(dsc, "Really delete everything? (y/n) ", 1);
    head->state = DELETING;
  }
  else
  {
    parse_range(head, string, &(head->current),
		&(head->bound), NULL);
    head->issaved = 0;
    for (l = head->next; l->next != head->current; l = l->next) ;
    if (head->bound == NULL)
      l->next = NULL;
    else
      l->next = head->bound->next;
    l = head->current->next;
    while (head->current != head->bound->next)
    {
      free(head->current);
      head->current = l;
      l = l->next;
    }
    notify(player, "Deleted.");
  }
}

void do_write(player, head, string)
dbref player;
struct top *head;
char *string;
{
  struct buffer *p;
  FILE *fp;
  char fname[80];

  if (head->next->next == NULL)
  {
    unlink(head->filename);
  }
  else
  {
    if (*string == '\0')
      strcpy(fname, head->filename);
    else
    {
      if (strlen(string) + strlen(db[player].name) + 10 > 80)
      {
	string[80 - strlen(db[player].name) - 10] = '\0';
      }
      sprintf(fname, tprintf("./files/p/%s/%s",
			     db[player].name, string));
    }
    if ((fp = fopen(fname, "w")) == NULL)
    {
      notify(player, "Error opening file!");
      fprintf(stderr, "File I/O error from %s.",
	      db[player].name);
      return;
    }
    for (p = head->next->next; p != NULL; p = p->next)
    {
      fputs(p->line, fp);
      fputs("\n", fp);
    }
    fclose(fp);
  }
  notify(player, "Written.");
  head->issaved = 1;
}

void set_change(dsc, player, head, string)
struct descriptor_data *dsc;
dbref player;
struct top *head;
char *string;

{
  head->state = CHANGE;
  parse_range(head, string, &(head->current), &(head->bound),
	      &(head->linenum));
  head->bound = head->bound->next;
  notify(player, tprintf("[%2d]: %s", head->linenum,
			 head->current->line));
  print(dsc, tprintf("[%2d]: ", head->linenum), 0);
}

void set_insert(dsc, player, head, string)
struct descriptor_data *dsc;
dbref player;
struct top *head;
char *string;
{
  head->state = INSERT;
  parse_range(head, string, &(head->current), &(head->bound),
	      &(head->linenum));
  head->bound = head->current->next;
  if (head->bound == NULL)
  {
    notify(player, "Use \"a\" to add to the end of a file.");
    head->state = COMMAND;
    return;
  }
  notify(player, tprintf("[%2d]: %s", head->linenum,
			 head->current->line));
  print(dsc, tprintf("[%2d]: ", ++head->linenum), 0);
}

void set_add(dsc, player, head)
struct descriptor_data *dsc;
dbref player;
struct top *head;
{
  head->state = ADD;
  head->current = head->next;
  head->linenum = 1;
  while (head->current->next != NULL)
  {
    head->current = head->current->next;
    head->linenum++;
  }
  print(dsc, tprintf("[%2d]: ", head->linenum), 0);
}

void do_change(dsc, player, head, string)
struct descriptor_data *dsc;
dbref player;
struct top *head;
char *string;

{
  if (strcmp(string, "."))
  {
    if (strlen(string) > 80)
      string[80] = '\0';
    strcpy(head->current->line, string);
    head->current = head->current->next;
    head->linenum++;
    head->issaved = 0;
    if (head->current == head->bound)
      head->state = COMMAND;
    else
    {
      notify(player, tprintf("[%2d]: %s",
			     head->linenum, head->current->line));
      print(dsc, tprintf("[%2d]: ", head->linenum), 0);
    }
  }
  else
    head->state = COMMAND;
}

void do_add(dsc, head, string)
struct descriptor_data *dsc;
struct top *head;
char *string;

{
  if (strcmp(string, "."))
  {
    MALLOC(head->current->next, struct buffer, 1);

    head->current = head->current->next;
    if (strlen(string) > 80)
      string[80] = '\0';
    strcpy(head->current->line, string);
    head->linenum++;
    print(dsc, tprintf("[%2d]: ", head->linenum), 0);
  }
  else
  {
    head->issaved = 0;
    head->state = COMMAND;
    head->current->next = NULL;
  }
}

void do_insert(dsc, head, string)
struct descriptor_data *dsc;
struct top *head;
char *string;
{
  if (strcmp(string, "."))
  {
    MALLOC(head->current->next, struct buffer, 1);

    head->current = head->current->next;
    if (strlen(string) > 80)
      string[80] = '\0';
    strcpy(head->current->line, string);
    head->linenum++;
    if ((head->linenum % 10) == 0)
      print(dsc, tprintf("%2d: ", head->linenum), 0);
    head->issaved = 0;
  }
  else
  {
    head->state = COMMAND;
    head->current->next = head->bound;
  }
}

void do_quit(dsc, player, head)
struct descriptor_data *dsc;
dbref player;
struct top *head;
{
  if (head->state == COMMAND && !head->issaved)
  {
    print(dsc, "But you haven't saved your changes! Really quit? (y/n) ", 1);
    head->state = QUITTING;
  }
  else
  {
    dsc->edit_buff = NULL;
    if (head->next->next == NULL)
      unlink(head->filename);
    notify(player, "Bye.");
    head->state = QUITTING;
  }
}

void edit_command(dsc, player, string)
struct descriptor_data *dsc;
dbref player;
char *string;

{
  char cmd = *string;
  struct top *head;

  head = dsc->edit_buff;
  if (head->state == COMMAND)
  {
    for (string++; *string && *string == ' '; string++) ;
    switch (cmd)
    {
    case 'c':
    case 'C':
      set_change(dsc, player, head, string);
      break;
    case 'l':
    case 'L':
      do_list(player, head, string);
      break;
    case 's':
      do_esearch(player, head, string, 0);
      break;
    case 'S':
      do_esearch(player, head, string, 1);
      break;
    case 'i':
    case 'I':
      set_insert(dsc, player, head, string);
      break;
    case 'h':
    case 'H':
      do_ehelp(player);
      break;
    case 'a':
    case 'A':
      set_add(dsc, player, head);
      break;
    case 'd':
    case 'D':
      do_delete(dsc, player, head, string);
      break;
    case 'w':
    case 'W':
      do_write(player, head, string);
      break;
    case 'q':
    case 'Q':
      do_quit(dsc, player, head);
      break;
    default:
      notify(player,
	     "Unknown command. (Type \"h\" for help.)");
      break;
    }
    if (head->state == COMMAND)
      prompt(dsc);
    return;
  }
  else
  {
    switch (head->state)
    {
    case CHANGE:
      do_change(dsc, player, head, string);
      break;
    case ADD:
      do_add(dsc, head, string);
      break;
    case INSERT:
      do_insert(dsc, head, string);
      break;
    case DELETING:
      if (to_lower(*string) == 'y')
	do_delete(dsc, player, head, "");
      head->state = COMMAND;
      break;
    case QUITTING:
      if (to_lower(*string) == 'y')
      {
	do_quit(dsc, player, head);
	return;
      }
      else
	head->state = COMMAND;
    }
    if (head->state == COMMAND)
      prompt(dsc);
  }
}

/********************************* EDITOR.C *********************************/
