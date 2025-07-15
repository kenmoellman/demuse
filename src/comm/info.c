#include "config.h"
#include "externs.h"
#include "db.h"
#include <ctype.h>
/* #include <config.c> */

#include <malloc.h>

/* static struct mdb_entry *mdb; */
struct mdb_entry *mdb;

/* static long mdb_top; */
long mdb_top; 
//static long mdb_first_free;
extern long mdb_first_free;

static void info_cpu P((dbref));
static void info_mail P((dbref));

void info_mem(dbref);

void do_info(player, arg1)
dbref player;
char *arg1;
{

  if (!string_compare(arg1, "config"))
  {
    info_config(player);
  }
  else if (!string_compare(arg1, "db"))
  {
    info_db(player);
  }
  else if (!string_compare(arg1, "funcs"))
  {
    info_funcs(player);
  }
  else if (!string_compare(arg1, "memory")) 
  {
    info_mem(player);
  }
  else if(!string_compare(arg1, "mail"))
    info_mail(player);
#ifdef USE_PROC
  else if (!string_compare(arg1, "pid"))
  {
    info_pid(player);
  }
  else if(!string_compare(arg1, "cpu"))
    info_cpu(player);
#endif
  else
  {
    char temp[1024];

    sprintf(temp,"Usage: @info config|db|funcs|memory|mail");
#ifdef USE_PROC
    strcat(temp, "|pid|cpu");
#endif
    notify(player, temp);
  }
}

#ifdef USE_PROC
void info_pid(player)
dbref player;
{
#define PIDBUF 1024
  char filename[256];
  char buf[PIDBUF];
  FILE *fp;
  char *p;

  sprintf(filename, "/proc/%d/status", getpid());
  if ((fp = fopen(filename, "r")) == NULL)
  {
    fclose(fp);
    notify(player,
	   tprintf("Couldn't open \"%s\" for reading!", filename));
    return;
  }
  while (!feof(fp))
  {
    fgets(buf, PIDBUF, fp);
    if (!strncmp(buf, "VmSize", 6))
      break;
  }
  if (feof(fp))
  {
    fclose(fp);
    notify(player,
	   tprintf("Error reading \"%s\"!", filename));
    return;
  }
  fclose(fp);
  *(buf + strlen(buf) - 1) = '\0';
  for (p = buf; *p && !isspace(*p); p++) ;
  while (isspace(*p))
    p++;
  notify(player, tprintf("%s (deMUSE) Process ID info:",muse_name));
  notify(player, tprintf("  PID : %d", getpid()));
  notify(player, tprintf("  Size: %s", p));
}
#endif

void info_cpu(player)
  dbref player;
{ 
#define CPUBUF 80
  FILE *fp;
  char buf[CPUBUF];
  
  if((fp = fopen("/proc/cpuinfo", "r")) == NULL)
    return;
  
  while(!feof(fp))
  {
    fgets(buf, CPUBUF, fp);
    if(feof(fp))
      break;
    buf[strlen(buf)-1] = '\0';
    notify(player, buf);
  }
  fclose(fp);
}

void info_mem(dbref player)
{
        struct mallinfo M_MEMINF;
  
        M_MEMINF = mallinfo();

	notify(player, tprintf("\tStack Size / Blocks....: %d/%d", stack_size, number_stack_blocks));
	notify(player, tprintf("\tText Block Size/Blocks.: %d/%d", text_block_size, text_block_num));
        notify(player, tprintf("\tTotal Allocated Memory.: %d", M_MEMINF.arena));
        notify(player, tprintf("\tFree Allocated Memory..: %d", M_MEMINF.fordblks));
        notify(player, tprintf("\tFree Chunks of Memory..: %d", M_MEMINF.ordblks));
        notify(player, tprintf("\tTotal Used Memory......: %d", M_MEMINF.uordblks));
}


void info_mail(player)
  dbref player;
{ 
  long i;
  long new, deleted, read, unread;
  long total, total_size;
  long oldest, newest;
  
  new = deleted = read = unread = total = total_size = 0;
  
  oldest = now;
  newest = (long)0;
  
  for(i = 0;i < mdb_top;++i)
  {
/* First thing, see if the message exists */
/* If not then it's a free spot           */

    total_size = total_size + (long)(15);

    if(!mdb[i].message)
      continue;
  
    if(mdb[i].flags & MF_DELETED)
      deleted++;
    else if(mdb[i].flags & MF_READ)
      read++;
    else if(mdb[i].flags & MF_NEW)
      new++;
    else
      unread++;
  
    if(mdb[i].date < oldest)
      oldest = mdb[i].date;
    else if(mdb[i].date > newest)
      newest = mdb[i].date;

    total++;
    total_size = total_size + (long)((strlen(mdb[i].message)));

  }
  
  notify(player, tprintf("|B!+maildb_top   ||W!+: ||C!+#%ld|", mdb_top));
  notify(player, tprintf("|B!+first_free   ||W!+: ||C!+#%ld|",
    (mdb_first_free == -1)?mdb_top:mdb_first_free));
  notify(player, tprintf("|B!+maildb size  ||W!+: ||C!+%s|",
    comma(tprintf("%ld",total_size))));
  
  notify(player, tprintf("\n|B!+Oldest Message||W!+: ||C!+%s|",
    mktm(oldest, "D|", player)));
  notify(player, tprintf("|B!+Newest Message||W!+: ||C!+%s|",
    mktm(newest, "D|", player)));
  
  notify(player, tprintf("\n|B!+Total Messages       ||W!+: ||C!+%s|",
    comma(tprintf("%ld", total))));
  notify(player, tprintf("|B!+Deleted Messages     ||W!+: ||C!+%s|",
    comma(tprintf("%ld", deleted))));
  notify(player, tprintf("|B!+Read Messages        ||W!+: ||C!+%s|",
    comma(tprintf("%ld", read))));
  notify(player, tprintf("|B!+Unread Messages      ||W!+: ||C!+%s|",
    comma(tprintf("%ld", unread))));
  notify(player, tprintf("|B!+New Messages         ||W!+: ||C!+%s|",
    comma(tprintf("%ld", new))));
}


