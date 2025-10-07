/* various this and thats taken from MAZE v1 */

#include "config.h"
#include "externs.h"
#include "sock.h"
#include <ctype.h>

#define OBJECTIVE  0
#define SUBJECTIVE 1
#define POSSESSIVE 2

char *my_center(char *, int);
char *my_center2(char *, int);
char *my_string(char *, int);
char *my_string2(char *, int);
void my_atr_add(dbref, ATTR *, long);
//int is_connected(NOTHING, dbref);
//int is_connected(dbref, dbref);
char *poss(dbref);
char *my_ljust(char *, int);
char *my_ljust2(char *, int);
void trig_idle_boot(void);
char *my_pronoun_substitute(dbref,int);
void notify_all2(char *, dbref);
void do_setbit(dbref, char *, char *);
static void init_sbar(dbref);
static void remove_sbar(dbref);
void update_sbar(dbref);
void do_sbar(dbref, char *);
int str_colorlen(char *);

char *comma(num)
  char *num;
{
  static char buf[2048];
  char buf2[2048];
  char array[(strlen(num)/3)+3][5];
  char *p = num;
  int i, len, ctr = 0, negative = 0;

  if(*p == '-')
    negative = 1;

  if(strchr(p, '.'))
  {
    p = strchr(p, '.');
    strcpy(buf2, p);
    *p = '\0';
    p = num;
  }
  else
    strcpy(buf2, "");

  p = (negative)?num+1:num;

  if(strlen(p) < 4)
  {
    sprintf(buf, "%s%s", num, buf2);
    return(buf);
  }

  if((len = strlen(p)%3))
  {
    for(i = 0;i < len;++i)
      array[0][i] = *p++;
    array[0][i] = '\0';
    ctr = 1;
  }

  while(*p)
  {
    for(i = 0;i < 3;++i)
      array[ctr][i] = *p++;
    array[ctr++][i] = '\0';
  }

  strcpy(buf, (num[0] == '-')?"-":"");

  for(i = 0;i < ctr-1;++i)
    strcat(buf, tprintf("%s,", array[i]));
  strcat(buf, array[i]);
  strcat(buf, buf2);

  return(buf);
}



char *my_center(str, width)
  char *str;
  int width;
{ 
  int i;
  int num = (width/2)-(strlen(str)/2);
  int color_len = str_colorlen(str);
  static char buffer[1000];
  char *b = buffer;
  
  if(width > 80)
    return("WIDTH OUT OF RANGE");
  
  for(i = 0;i < num+color_len/2;++i)
    *b++ = ' ';
  
  while(*str)
    *b++ = *str++;
  *b = '\0';

  for(i = strlen(buffer)-color_len;i < width;++i)
    *b++ = ' ';

  *b = '\0';
  
  return(buffer);
}

char *my_string(str, num)
  char *str;
  int num;
{ 
  int i,j,k=0;
  static char buffer[1000];

  if(num > 250)
    return("NUM OUT OF RANGE");

  for(i=0;i<num;i++)
  { 
    for(j=0;j<strlen(str);++j)
      buffer[k+j]=str[j];
    k+=strlen(str);
  }
  buffer[k]='\0';

  return(buffer);
}

char *my_string2(str, num)
  char *str;
  int num;
{ 
  int i,j,k=0;
  static char buffer[1000];

  if(num > 250)
    return("NUM OUT OF RANGE");

  for(i=0;i<num;i++)
  { 
    for(j=0;j<strlen(str);++j)
      buffer[k+j]=str[j];
    k+=strlen(str);
  }
  buffer[k]='\0';

  return(buffer);
}


char *my_center2(str, width)
  char *str;
  int width;
{ 
  int i;
  int num = (width/2)-(strlen(str)/2);
  int color_len = str_colorlen(str);
  static char buffer[1000];
  char *b = buffer;

  if(width > 80)
    return("WIDTH OUT OF RANGE");
  
  for(i = 0;i < num+color_len/2;++i)
    *b++ = ' ';
  
  while(*str)
    *b++ = *str++;
  *b = '\0';
  
  for(i = strlen(buffer)-color_len;i < width;++i)
    *b++ = ' ';
  
  *b = '\0';
  
  return(buffer);
}
  
void my_atr_add(thing, attr, increase)
  dbref thing;
  ATTR *attr;
  long increase;
{ 
  int temp;
  
  temp=atol(atr_get(thing,attr));
  atr_add(thing,attr,tprintf("%ld",temp+increase));
}


/* REMOVED - Now using unified is_connected in player.c
 * Call is_connected(NOTHING, who) for raw check
 */

/* REMOVED - Now using unified is_connected in player.c
 * Call is_connected(viewer, target) for visibility check
 */


char *poss(thing)
  dbref thing;
{ 
  static char buf[1024];
  char *p = db[thing].name;
  
  if(to_lower(*(p+strlen(p)-1) == 's'))
    sprintf(buf, "%s'", db[thing].cname);
  else
    sprintf(buf, "%s's", db[thing].cname);
  
  return(buf);
} 

char *my_ljust(str, field)
  char *str;
  int field;
{
  static char buf[MAX_BUFF_LEN];
  int i=0;

  strcpy(buf, str);

  if (i < strlen(strip_color(str)))
  {
    strcpy(buf, truncate_color(str, i));
  }
  else if (i > strlen(strip_color(str)))
  { 
    strcat(buf, spc(i - strlen(strip_color(str))));
  }
  return buf;
}

char *my_ljust2(str, field)
  char *str;
  int field;

{
  return(my_ljust(str, field));
}

int str_colorlen(char *str)
{
  return(strlen(str)-strlen(strip_color(str)));
}

void trig_idle_boot()
{ 
  extern long guest_boot_time;
  struct descriptor_data *d, *dnext;

  if(!guest_boot_time)
    return;
  for(d = descriptor_list;d;d = dnext)
  { 
    dnext = d->next;

/* boot non-connected descriptors */

    if(d->state != CONNECTED)
    {
      if(now-d->last_time <= 0)
      {
        d->last_time = now;
      }
      if(now-d->last_time > guest_boot_time)
      { 
        queue_string(d, "You have been idle for too long. Sorry.\n");
        flush_all_output();
        log_io(tprintf("Concid %ld, host %s@%s, was idle booted.",
          d->concid, d->user, d->addr));
        shutdownsock(d);
      }
      continue; 
    }

#ifdef BOOT_GUESTS
/* Make sure this part of the loop is here or it will crash because */
/* of d->player */
    if(Guest(d->player))
    { 
      if(now-d->last_time > guest_boot_time)
      { 
        notify(d->player, "You have been idle for too long. Sorry.");
        flush_all_output();
        log_io(tprintf("Concid %d (%s) was idle booted.",
          d->concid, name(d->player)));
        shutdownsock(d);
      }
    }
#endif
  }
}

char *my_pronoun_substitute(who,subtype)
  dbref who;
  int subtype;
{ 
  int sex;
  static char *objective[3]={"him","her","it"};
  static char *subjective[3]={"he","she","it"};
  static char *possessive[3]={"his","her","its"};
  
  switch(*atr_get(who,A_SEX))
  { 
    case 'm':
    case 'M':
      sex=0;
      break;
    case 'f':
    case 'F':
    case 'w':
    case 'W':
      sex=1;
      break;
    default:
      sex=2;
  }
  
  switch(subtype)
  { 
    case OBJECTIVE:
      return(objective[sex]);
    case SUBJECTIVE:
      return(subjective[sex]);
  }
  return(possessive[sex]);
}




/* Just like notify_all() but doesn't check if the descriptor is connected */
/* Also doesn't support color */
/* Made for the db loading routine */
void notify_all2(arg, exception)
  char *arg;
  dbref exception;
{ 
  struct descriptor_data *d;
  
  for(d = descriptor_list;d;d = d->next)
  { 
    if(d->player == exception)
      continue;
    queue_string(d, arg);
    if(arg[strlen(arg)-1] != '\n')
      queue_string(d, "\n");
  }
}


void do_setbit(player, arg1, arg2)
  dbref player;
  char *arg1;
  char *arg2;
{
  int bit;
  int num_entries = 4;
  long bit_field[4] = {0x0, 0x1, 0x2, 0x4};
  dbref thing;
#ifdef USE_COMBAT
  PARTY *p;
  PARTY_MEMBER *pm;
#endif 

  if(!*arg1)
    thing = player;
  else
  {
    init_match(player, arg1, TYPE_PLAYER);
    match_me();
    match_here();
    match_neighbor();
    match_absolute();
    match_possession();
    match_player(NOTHING, NULL);
  }

  if(!*arg2)
  {
    notify(player, "No bit specified.");
    return;
  }

  if((thing = noisy_match_result()) == NOTHING)
    return;

  if(!Wizard(db[player].owner))
  {
    notify(player, perm_denied());
    return;
  }

  bit = atoi(arg2);

  if(bit >= num_entries)
  {
    notify(player, "No such bit entry.");
    return;
  }

#ifdef USE_COMBAT
  if((p = find_party(thing)))
  {
    for(pm = p->members;pm;pm = pm->next)
    {
      if(!is_following_party(pm->player))
        continue;
      if(Typeof(pm->player) != TYPE_PLAYER)
        continue;

      if(!bit)
        db[thing].bitmap = 0L;
      else
        db[thing].bitmap |= bit_field[bit];
    }
  }
  else
#endif
  {
    if(!bit)
      db[thing].bitmap = 0L;
    else
      db[thing].bitmap |= bit_field[bit];
  }
  notify(player, tprintf("New bitmap value: %ld", db[thing].bitmap));
}



static void init_sbar(player)
  dbref player;
{
  notify(player, "\33[2;25r");
  atr_add(player, A_SBAR, "1");
  update_sbar(player);
}

static void remove_sbar(player)
  dbref player;
{
  notify(player, "\33[1;25r\33[24;1H");
  atr_add(player, A_SBAR, "0");
}

void do_sbar(player, arg)
  dbref player;
  char *arg;
{
  notify(player, "This feature is temporarily disabled. Sorry.");
  return;
  if(!string_compare(arg, "on"))
    init_sbar(player);
  else if(!string_compare(arg, "off"))
    remove_sbar(player);
  else
  {
    if(atoi(atr_get(player, A_SBAR)))
      remove_sbar(player);
    else
      init_sbar(player);
  }
}

void update_sbar(player)
  dbref player;
{
  char hpbuf[1024];

  return;
  if(Typeof(player) != TYPE_PLAYER || !atoi(atr_get(player, A_SBAR)))
    return;

  if(atoi(atr_get(player, A_HITPOINTS)) < atoi(atr_get(player, A_MAXHP))/4)
    sprintf(hpbuf, "|bR|%d|+W|", atoi(atr_get(player, A_HITPOINTS)));
  else
    sprintf(hpbuf, "%d", atoi(atr_get(player, A_HITPOINTS)));

  notify(player,
    tprintf("\33[1;44m\33[1;1H\33[K\33[1;1HHitPoints: %3s/%3d   MagicPoints: %3d/%3d   Exp to Next: %ld",
    hpbuf, atoi(atr_get(player, A_MAXHP)),
    atoi(atr_get(player, A_MAGICPOINTS)), atoi(atr_get(player, A_MAXMP)),
    atol(atr_get(player, A_NEXTEXP))-atol(atr_get(player, A_EXP))));
  notify(player, "|n|\33[24;1H");
}

