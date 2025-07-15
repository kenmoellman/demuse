/*************************************************************************
 *
 *    rlpage.c                            (c) 1998   Ken C. Moellman, Jr.   
 *
 *  This program provides an interface to pagers with email capabilities.
 *
 *
 *************************************************************************/




/* we're going to make a queue for rlpages and then have them tick off. *
 * this will make sending rlpages go much more quickly, and save us the *
 * trouble of having to use some external program. yay.   -wm           */


#include "config.h"
#ifdef USE_RLPAGE
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "externs.h"



#define SLEEPLEN 3


typedef struct rlq RLQ;




struct rlq {
	dbref from;
        char *to;
	char *msg;
	RLQ *next;
};

static RLQ *rlpq = NULL, *rlpqlast = NULL;


int send_rlpage(char *, char *);
int emailpage(char *, char *);
static time_t last_send = 0;

void rlpage_tick(void)
{
  /* send a page every 20 seconds. */
  if( rlpq && ( (now - last_send) > 19 ) )
  {
    RLQ *rlqc;
    int retval = 1;
    int counter = 0;

    rlqc = rlpq;
    rlpq = rlqc->next;
    if (rlpq == NULL)
    {
      rlpqlast = NULL;
    }

    while ( (counter < 5000) && (retval != 0) )
    {
      retval = send_rlpage(rlqc->to, rlqc->msg); 
      counter++;
    }

    if (retval != 0)
    {
      log_io(tprintf("Error sending page from %s to %s", db[rlqc->from].cname, rlqc->to));

      notify(rlqc->from, tprintf("Problem sending following chunk via rlpage to %s:", rlqc->to));
      notify(rlqc->from, rlqc->msg);
    }

  /* we're done with the top of the rlpage queue. free it. */

    free(rlqc->to);
    free(rlqc->msg);
    free(rlqc);
    last_send = now;
  }
}


int queue_rlpage (dbref from, char *to, char *msg)
{
  struct rlq *rlqnew;
  int templen;

  rlqnew = malloc(sizeof(RLQ));

  if (!rlpq)
  {
    rlpq = rlqnew;
  }
  else
  {
    rlpqlast->next = rlqnew;
  }
  rlpqlast = rlqnew;

  rlqnew->from = from;

  templen = strlen(to);
  rlqnew->to = malloc(templen + 1);
  strncpy(rlqnew->to, to, templen);
  rlqnew->to[templen] = 0;

  templen = strlen(msg);
  rlqnew->msg = malloc(templen + 1);
  strncpy(rlqnew->msg, msg, templen);
  rlqnew->msg[templen] = 0;
  
  rlqnew->next = NULL;

  return 0;
}
  




void do_rlpage(player, arg1, arg2)
dbref player;
char *arg1;
char *arg2;
{ 
  dbref target;
  char *s;
  long targlen;
  long pagelen;
  long pagetmp;
  char pagemsg[MAX_BUFF_LEN];
  int numerr;
  int chunks;

  char email[MAX_BUFF_LEN];
  char retaddr[MAX_BUFF_LEN];
  char buf[MAX_BUFF_LEN*3];

  numerr = 0;
  chunks = 0;
  




  if(!arg1 || !arg2)
  {
    notify(player,"Missing RLpage parameter.");
    return;
  }
  
  if(strcmp(arg1,"me") == 0)
    target = player;
  else
    target = lookup_player(arg1);
  
  if(!target)
  {
    notify(player,"Invalid RLpage target.");
    return;
  }
  
  strcpy(email,atr_get(target,A_RLPAGE));
  
  if((!email && !*email) || (strlen(email) == 0))
  { 
    notify(player,"Sorry, that user doesn't have rlpage set.");
    return;
  }
  
  if(!could_doit(player, target, A_LRLPAGE))
  {
    notify(player,"You cannot RLpage this person.");
    return;
  }  
  
  if(!could_doit(target, player, A_LRLPAGE))
  { 
    notify(player,"You cannot RLpage someone you're blocking pages from.");
    return;
  }


  strcpy(retaddr,atr_get(player,A_RLPAGE));

  if(((retaddr && *retaddr) && strlen(retaddr)) && could_doit(player, target, A_RLPAGESSF))
  {
    sprintf(buf,"\n%s@%s (%s):\n%s", db[player].name, muse_name, retaddr, arg2);
  }
  else
  {  
    sprintf(buf,"\n%s@%s:\n%s", db[player].name, muse_name, arg2);
  }

  targlen = atol(atr_get(target, A_RLPAGELEN));
  if(targlen == 0)
  {
    targlen = 9999;
  }
  pagelen = strlen(buf);

  log_io(tprintf("RLPAGE %s (%s) from %s",db[target].cname, email, db[player].cname));

  if(pagelen <= targlen)
  {
    numerr = queue_rlpage(player, email,buf);
    chunks++;
  }
  else
  {
    s = buf;

    for (pagetmp = strlen(s); pagetmp > 0;)
    {
      strncpy(pagemsg, s, targlen);
      pagemsg[targlen] = 0;
      numerr += queue_rlpage(player, email, pagemsg);
      chunks++;
      s += targlen;
      pagetmp -= targlen;
    }
  }

  notify(player,tprintf("RLPAGE to %s queued. %d chunks, %d error(s).",db[target].cname, chunks, numerr));

  if (numerr)
  {
    log_io(tprintf("Warning: %d error(s) occured with RLpage."));
  }
  return;
}

int send_rlpage(char *email, char *buf)
{
  char buf2[4096];
  FILE *fptr2;

  sprintf(buf2,"/usr/lib/sendmail %s", email);
  if((fptr2 = popen(buf2,"w")) == NULL )
  {
    log_io("problem calling sendmail\n");
    return 1;
  }

  fputs(buf,fptr2);
  fputs("\n.\n",fptr2);

  pclose(fptr2);

  sleep(1);

  return 0;
}

#endif
