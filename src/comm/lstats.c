/* lstats.c - login stats for the game.
   taken from MAZE v1.0 with permission from itsme  */
 
#include "config.h"
#include "externs.h"
#include "sock.h"
#include "db.h"
#include <ctype.h>


void give_allowances(void);

extern char ccom[1024];

static long oldhighestweek = 0;
static long oldhighestday = 0;
 
void do_loginstats(player)
  dbref player;
{ 
  struct tm *tim = localtime(&now);
  char *day = get_day(now);
  char *suffix = mil_to_stndrd(now);
  char *str = "|";
  char *str2 = "|";
  char buf[1024];
  int current_logins = 0;
  int total1, total2;
  int hour;
  struct descriptor_data *d;

  check_newday();

  total1 = nl.a_sun+nl.a_mon+nl.a_tue+nl.a_wed+nl.a_thu+nl.a_fri+nl.a_sat;
  total2 = nl.b_sun+nl.b_mon+nl.b_tue+nl.b_wed+nl.b_thu+nl.b_fri+nl.b_sat;
  
  for(d = descriptor_list;d;d = d->next)
    if(d->state == CONNECTED)
      current_logins++;

  hour = (tim->tm_hour%12)?tim->tm_hour%12:12;
  
  notify(player, tprintf("%s User Login Statistics as of %s %d/%d/%d - %d:%02d:%02d%s", muse_name,
    day, tim->tm_mon+1, tim->tm_mday, (1900+tim->tm_year), hour, tim->tm_min,
    tim->tm_sec, suffix));
  notify(player,
    tprintf("\n  Total Logins: %ld",
    nl.total));

  notify(player, tprintf("  |W!+Connections: ||Y!+Today:||G!+ %d (%3.1f%% of record) ||Y!+ Currently:||G!+ %d (%3.1f%% of record)|", 
                           nl.today,  ((nl.highest_day)?(double)nl.today/(double)nl.highest_day*100:0), 
                           current_logins, ((nl.highest_atonce)?(double)current_logins/(double)nl.highest_atonce*100:0)));
  notify(player, tprintf("\n  Records:  One Day: %-6d One Week: %-6d At Once: %-6d", nl.highest_day, nl.highest_week, nl.highest_atonce));
  notify(player, tprintf("            (on %s)   (on %s)    (on %s)", nl.date_day, nl.date_week, nl.date_atonce));

  notify(player, tprintf("\n.%s.", my_string("-", 61)));
  notify(player, tprintf("%s           %s Sun %s Mon %s Tue %s Wed %s Thu %s Fri %s Sat %s Total %s",
    str, str2, str2, str2, str2, str2, str2, str2, str2, str2));
  notify(player, "|-----------|-----|-----|-----|-----|-----|-----|-----|-------|");
  sprintf(buf, "%s This Week %s %3d %s %3d %s %3d %s %3d %s %3d %s %3d %s %3d %s %5d %s",
    str2, str, nl.a_sun, str, nl.a_mon, str, nl.a_tue, str, nl.a_wed, str,
    nl.a_thu, str, nl.a_fri, str, nl.a_sat, str, total1, str);
  notify(player, buf);
  sprintf(buf, "%s Last Week %s %3d %s %3d %s %3d %s %3d %s %3d %s %3d %s %3d %s %5d %s",
    str2, str, nl.b_sun, str, nl.b_mon, str, nl.b_tue, str, nl.b_wed, str,
    nl.b_thu, str, nl.b_fri, str, nl.b_sat, str, total2, str);
  notify(player, buf);
/*  sprintf(buf, "%s Total     %s %3d %s %3d %s %3d %s %3d %s %3d %s %3d %s %3d %s %5d %s",
    str2, str, nl.a_sun+nl.b_sun, str, nl.a_mon+nl.b_mon, str,
    nl.a_tue+nl.b_tue, str, nl.a_wed+nl.b_wed, str, nl.a_thu+nl.b_thu, str,
    nl.a_fri+nl.b_fri, str, nl.a_sat+nl.b_sat, str, total1+total2, str); * this is silly .. it proves nothing 
  notify(player, buf); */
  notify(player, tprintf("`%s'", my_string("-", 61)));

}

void write_loginstats(long epoch)
{ 
  FILE *fp;
  char *filename = tprintf("db/loginstatsdb.%ld", epoch);
  
  if((fp = fopen(filename, "w")) == NULL)
  { 
    log_error(tprintf("Couldn't open \"%s\" for writing", filename));
    return;
  }
  
  fprintf(fp, "%ld\n", now);
  fprintf(fp, "%d\n", nl.highest_day);
  fprintf(fp, "%d\n", nl.highest_week);
  fprintf(fp, "%d\n", nl.highest_atonce);
  fprintf(fp, "%s\n", nl.date_day);
  fprintf(fp, "%s\n", nl.date_week);
  fprintf(fp, "%s\n", nl.date_atonce);
  fprintf(fp, "%ld\n", nl.total);
  fprintf(fp, "%d\n", nl.today);
  fprintf(fp, "%d\n", nl.a_sun);
  fprintf(fp, "%d\n", nl.a_mon);
  fprintf(fp, "%d\n", nl.a_tue);
  fprintf(fp, "%d\n", nl.a_wed);
  fprintf(fp, "%d\n", nl.a_thu);
  fprintf(fp, "%d\n", nl.a_fri);
  fprintf(fp, "%d\n", nl.a_sat);
  fprintf(fp, "%d\n", nl.b_sun);
  fprintf(fp, "%d\n", nl.b_mon);
  fprintf(fp, "%d\n", nl.b_tue);
  fprintf(fp, "%d\n", nl.b_wed);
  fprintf(fp, "%d\n", nl.b_thu);
  fprintf(fp, "%d\n", nl.b_fri);
  fprintf(fp, "%d\n", nl.b_sat);
  
  fflush(fp);
  fclose(fp);

  system(tprintf("cp db/loginstatsdb.%ld db/loginstatsdb", epoch));
  system(tprintf("rm db/loginstatsdb.%ld", epoch - 3));
} 

void read_loginstats()
{ 
  FILE *fp;
  char *filename = "db/loginstatsdb";
  char buf[LOGINDBBUF];
  struct tm *tim;
   
  if((fp = fopen(filename, "r")) == NULL)
  {
    log_error(tprintf("Couldn't open \"%s\" for reading", filename));
    nl.total = nl.today = nl.a_sun = nl.a_mon = nl.a_tue = nl.a_wed = nl.a_thu = nl.a_fri = nl.a_sat = nl.b_sun = nl.b_mon = nl.b_tue = nl.b_wed = nl.b_thu = nl.b_fri = nl.b_sat = 0;
    return;
  }
  
  fgets(buf, LOGINDBBUF , fp);    /* When it was saved - ignore */
  nl.time = atol(buf);
  tim = localtime(&nl.time);
  nl.day = tim->tm_wday;
  fgets(buf, LOGINDBBUF , fp);
  nl.highest_day = atoi(buf);
  oldhighestday = nl.highest_day;
  fgets(buf, LOGINDBBUF , fp);
  nl.highest_week = atoi(buf);
  oldhighestweek = nl.highest_week;
  fgets(buf, LOGINDBBUF , fp);
  nl.highest_atonce = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  buf[strlen(buf)-1] = '\0';
  strcpy(nl.date_day, buf);
  fgets(buf, LOGINDBBUF , fp);
  buf[strlen(buf)-1] = '\0';
  strcpy(nl.date_week, buf);
  fgets(buf, LOGINDBBUF , fp);
  buf[strlen(buf)-1] = '\0';
  strcpy(nl.date_atonce, buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.total = atol(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.today = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.a_sun = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.a_mon = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.a_tue = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.a_wed = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.a_thu = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.a_fri = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.a_sat = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.b_sun = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.b_mon = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.b_tue = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.b_wed = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.b_thu = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.b_fri = atoi(buf);
  fgets(buf, LOGINDBBUF , fp);
  nl.b_sat = atoi(buf);

  fclose(fp);
}

void add_login(player)
  dbref player;
{ 
  struct descriptor_data *d;
  struct tm *tim = localtime(&now);
  int total;

  check_newday();
  
  switch(tim->tm_wday)
  {
    case 0:
      nl.a_sun++;
      break;
    case 1:
      nl.a_mon++;
      break;
    case 2:
      nl.a_tue++;
      break;
    case 3:
      nl.a_wed++;
      break;
    case 4:
      nl.a_thu++;
      break;
    case 5:
      nl.a_fri++;
      break;
    case 6:
      nl.a_sat++;
      break;
  }
  nl.total++;
  nl.today++;

  if(nl.today > nl.highest_day)
  {

    nl.highest_day = nl.today;
    sprintf(nl.date_day, "%02d/%02d/%02d",
      tim->tm_mon+1, tim->tm_mday, (tim->tm_year - 100));
  }
  
  total = nl.a_sun+nl.a_mon+nl.a_tue+nl.a_wed+nl.a_thu+nl.a_fri+nl.a_sat;
  
  if(total > nl.highest_week)
  {
/* move notice to beginning of each week rather than bugging everyone every day. */
    nl.highest_week = total;
    sprintf(nl.date_week, "%02d/%02d/%02d",
      tim->tm_mon+1, tim->tm_mday, (tim->tm_year - 100));
  }
  
  total = 0;
  
  for(d = descriptor_list;d;d = d->next)
    total++;
  
  if(total > nl.highest_atonce)
  {
    if(!*atr_get(player, A_LHIDE))
      notify_all(tprintf("** This is the most players ever connected to %s at once! There are currently %d players connected.",
        muse_name, total), NOTHING, 1);
  
    nl.highest_atonce = total;
    sprintf(nl.date_atonce, "%02d/%02d/%02d",
      tim->tm_mon+1, tim->tm_mday, (tim->tm_year - 100));
  }
}

void check_newday()
{
  struct tm *tim;
  static int old_day = 8;
  time_t new_time;
  int day = 0;

  

/* check to wsee if the game's been down a while.. */
  if (old_day == 8)
  {
    if (nl.time > now - 604800)
    {
      old_day = nl.day;
    }
  }

  new_time = time(NULL);
  tim = localtime(&new_time);
  day = tim->tm_wday;

  if(day == old_day)
    return;

  if(day < old_day)
  {
    nl.b_sun = nl.a_sun;
    nl.b_mon = nl.a_mon;
    nl.b_tue = nl.a_tue;
    nl.b_wed = nl.a_wed;
    nl.b_thu = nl.a_thu;
    nl.b_fri = nl.a_fri;
    nl.b_sat = nl.a_sat;
    nl.a_sun = nl.a_mon = nl.a_tue = nl.a_wed = nl.a_thu = nl.a_fri = nl.a_sat = 0;
    if (nl.highest_week > oldhighestweek)
    {
      notify_all(tprintf("|R!+This was a record-breaking week! Connections this week: ||C!+%ld||R!+ Previous: ||C!+%ld|", nl.highest_week, oldhighestweek), NOTHING, 1);
      oldhighestweek = nl.highest_week;
    }
    else
      notify_all("A new week begins!", NOTHING, 1);

  }
  else
  {
    if(nl.highest_day > oldhighestday)
    {
      notify_all(tprintf("|R!+This was a record-breaking day! Connections today: ||C!+%ld||R!+ Previous: ||C!+%ld|", nl.highest_day, oldhighestday), NOTHING, 1);
      oldhighestday = nl.highest_day;
    }
    else
    {
      notify_all("A new day begins!", NOTHING, 1);
    }
  }

  give_allowances();

  nl.today = 0;
  old_day = day;

  log_command("Dumping.");
  fork_and_dump();

#ifdef USE_COMBAT
  clear_deathlist();
#endif
}


void give_allowances()
{ 
  struct descriptor_data *d;

  struct plist_str 
  {
    dbref player;
    struct plist_str *next;
  } *plist = NULL;

  struct plist_str *x;

  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED && (Typeof(d->player) == TYPE_PLAYER) && power(d->player, POW_MEMBER) && (db[d->player].owner == d->player))
    {
      int found = 0;

      if (plist)
      {
        for (x = plist; x; x = x->next)
        {
          if (x->player == d->player)
          {
            found = 1;
          }
        }
      }
      if (!found)
      {
        struct plist_str *e;

        e = malloc(sizeof(struct plist_str));
        e->player = d->player;
        e->next = plist;
        plist = e;
      }
    }
  }
  for (x = plist; x; ) 
  {
    struct plist_str *y;

    giveto(x->player, allowance);
    notify(x->player, tprintf("You collect %d credits.", allowance));
    y = x;
    x = y->next;
    free(y);
  }
}
