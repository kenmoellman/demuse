/* rwho.c */
/* $Id: rwho.c,v 1.4 1993/01/31 14:43:59 nils Exp $ */

/*  This sets the MUSE up to use Marcus J. Ranum's RWHO server.  *
 *  If you opt to set this system up with your muse be sure and  *
 *  #define USE_RWHO in config.h, if you want users to be able   *
 *  to online RWHO dump (not recomended unless your machine is   *
 *  near the RWHO server) make sure to #define USE_RWHO_DUMP as  *
 *  well. Also, if you have any fixes or improvements to this    *
 *  code please send them to micromuse-rwho@chezmoto.ai.mit.edu  *
 *  -Michael Majere                                              */

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "config.h"
#include "externs.h"
#include "credits.h"		/* to get BASE_VERSION */

#define ENVHOST "MUDWHOSERVER"

#define STREAMPORT 6889

extern int connect();

/* Global boolean */
int rwho_on = 1;

/* @rwho utility control command. */
void do_rwho(player, arg1)
dbref player;
char *arg1;
{
  if (!power(player, POW_RWHO))
  {
    notify(player, perm_denied());
    return;
  }
  else
  {
    if (!strcmp(arg1, "start") && rwho_on)
    {
      notify(player, "RWHO transmission already on.");
      return;
    }
    if (!strcmp(arg1, "start") && !rwho_on)
    {
      rwhocli_setup(RWHO_SERVER, RWHO_PASSWORD, muse_name, BASE_VERSION);
      rwho_on = 1;
      rwho_update();
      notify(player, "RWHO Transmission started.");
      return;
    }
    if (!strcmp(arg1, "stop") && !rwho_on)
    {
      notify(player, "RWHO transmission already off.");
      return;
    }
    if (!strcmp(arg1, "stop") && rwho_on)
    {
      rwhocli_shutdown();
      rwho_on = 0;
      notify(player, "RWHO transmission stopped.");
      return;
    }
    if (!strcmp(arg1, "status") && rwho_on)
    {
      notify(player, "RWHO is transmitting.");
      return;
    }
    if (!strcmp(arg1, "status") && !rwho_on)
    {
      notify(player, "RWHO is not transmitting.");
      return;
    }
    notify(player, "Valid arguments are: start, stop and status.");
    return;
  }
}

/* Dump RWHO server list to player. */
void dump_rusers(player, arg1, arg2)
dbref player;
char *arg1, *arg2;
{
  struct sockaddr_in addr;
  struct hostent *hp;
  struct descriptor_data *d;
  char rbuf[1024], *p, *srv;
  char *checkmud = (char *)0;
  char *checkwho = (char *)0;
  int fd, red;

  /* Don't do it if not a connected player. */
  if (!(db[player].flags & CONNECT))
  {
    notify(player, "Invalid player.");
    return;
  }

  /* Don't do it if rwho has been stopped. */
  if (!rwho_on)
  {
    notify(player, "RWHO is not available now.");
    return;
  }

  /* Check for, and assign arguments, if any. */
  if (arg1 != '\0')
  {
    if ((arg2 == '\0') && (!strcmp(arg1, "mud")))
    {
      notify(player, "Second argument must be a mud name.");
      return;
    }
    if ((arg2 == '\0') && (!strcmp(arg1, "user")))
    {
      notify(player, "Second argument must be a user name.");
      return;
    }
    if (!strcmp(arg1, "user"))
    {
      checkwho = arg2;
    }
    if (!strcmp(arg1, "mud"))
    {
      checkmud = arg2;
    }
  }
  else
  {
    notify(player, "Valid arguments are 'user' or 'mud'.");
    return;
  }

  /* Look for host machine. */
  alarm(0);
  p = srv = RWHO_SERVER;
  while ((*p != '\0') && ((*p == '.') || isdigit(*p)))
    p++;
  if (*p != '\0')
  {
    if ((hp = gethostbyname(srv)) == (struct hostent *)0)
    {
      notify(player, "Couldn't find RWHO host.");
      alarm(1);
      return;
    }
    memcpy((char *)&addr.sin_addr, hp->h_addr_list[0], hp->h_length);
  }
  else
  {
    u_long f;

    if ((f = inet_addr(srv)) == -1L)
    {
      notify(player, "Couldn't find RWHO host.");
      alarm(1);
      return;
    }
    memcpy((char *)&addr.sin_addr, (char *)&f, sizeof(f));
  }

  /* Find the host's socket. */
  addr.sin_port = htons(STREAMPORT);
  addr.sin_family = AF_INET;
  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    notify(player, "Couldn't open RWHO socket.");
    alarm(1);
    return;
  }

  /* Connect to the host. */
  if (connect(fd, &addr, sizeof(addr)) < 0)
  {
    notify(player, "Couldn't connect to RWHO server.");
    close(fd);
    alarm(1);
    return;
  }

  /* Make sure we don't have two arguments. (Just in case) */
  if (checkmud != (char *)0 && checkwho != (char *)0)
  {
    notify(player, "You can only search for one user or mud entry.");
    close(fd);
    alarm(1);
    return;
  }

  /* Check our powers, charge if we don't. */
  if (!power(player, POW_RWHO))
  {
    if (!payfor(player, RWHO_COST))
    {
      notify(player, tprintf("It takes %d credits to do an rwho.",
			     RWHO_COST));
      close(fd);
      alarm(1);
      return;
    }
    notify(player, tprintf("You have been charged %d credits.",
			   RWHO_COST));
  }

  /* Sort out arguments, if any. */
  if (checkmud != (char *)0 || checkwho != (char *)0)
  {
    char xuf[512];
    int xlen;

    {
      sprintf(xuf, "%s=%.30s",
	      checkmud == (char *)0 ? "who" : "mud",
	      checkmud == (char *)0 ? checkwho : checkmud);
      xlen = strlen(xuf) + 1;
      if (write(fd, xuf, xlen) != xlen)
      {
      }
    }
  }

  /* Dump data to player. */
  for (d = descriptor_list; d; d = d->next)
  {
    if (d->connected && d->player == player)
    {
      while ((red = read(fd, rbuf, sizeof(rbuf))) > 0)
      {
	rbuf[red] = '\0';
	queue_string(d, rbuf);
      }
    }
  }
  close(fd);
  alarm(1);
  return;
}

/* Updates the RWHO server with our descriptor_list. */
void rwho_update()
{
  struct descriptor_data *d;

  rwhocli_pingalive();
  for (d = descriptor_list; d; d = d->next)
    if (d->connected && d->player > 0)
      if (!(db[d->player].flags & PLAYER_HIDE))
	rwhocli_userlogin(tprintf("%d@%s", d->player, RWHO_muse_name),
			  db[d->player].name, d->connected_at);
}
