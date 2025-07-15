
/* $Id: clilib.c,v 1.2 1992/10/11 15:15:26 nils Exp $ */
/*
   Copyright (C) 1991, Marcus J. Ranum. All rights reserved.
 */

/*
   code to interface client MUDs with rwho server

   this is a standalone library.
 */
#include	<stdio.h>
#include	<ctype.h>
#include	<fcntl.h>
#include	<sys/file.h>
#include	<sys/time.h>
#include	<netdb.h>

#include "externs.h"



#define	DGRAMPORT		6888

#ifndef	NO_HUGE_RESOLVER_CODE
extern struct hostent *gethostbyname();

#endif

static int dgramfd = -1;
static char *password;
static char *localnam;
static char *lcomment;
static struct sockaddr_in addr;
time_t senttime;

extern int sendto();

/* enable RWHO and send the server a "we are up" message */
int rwhocli_setup(server, serverpw, myname, comment)
char *server;
char *serverpw;
char *myname;
char *comment;
{
  extern unsigned long inet_addr();

#ifndef	NO_HUGE_RESOLVER_CODE
  struct hostent *hp;

#endif
  char pbuf[512];
  char *p;

  if (dgramfd != -1)
    return (1);

  password = malloc(strlen(serverpw) + 1);
  localnam = malloc(strlen(myname) + 1);
  lcomment = malloc(strlen(comment) + 1);
  if (password == (char *)0 || localnam == (char *)0 || lcomment == (char *)0)
    return (1);
  strcpy(password, serverpw);
  strcpy(localnam, myname);
  strcpy(lcomment, comment);

  p = server;
  while (*p != '\0' && (*p == '.' || isdigit(*p)))
    p++;

  if (*p != '\0')
  {
#ifndef	NO_HUGE_RESOLVER_CODE
    if ((hp = gethostbyname(server)) == (struct hostent *)0)
      return (1);
/*    (void)bcopy(hp->h_addr_list[0], (char *)&addr.sin_addr, hp->h_length); */
    (void)memcpy((char *)&addr.sin_addr, hp->h_addr_list[0], hp->h_length);
#else
    return (1);
#endif
  }
  else
  {
    unsigned long f;

    if ((f = inet_addr(server)) == -1L)
      return (1);
/*    (void)bcopy((char *)&f, (char *)&addr.sin_addr, sizeof(f)); */
    (void)memcpy((char *)&addr.sin_addr, (char *)&f, sizeof(f));
  }

  addr.sin_port = htons(DGRAMPORT);
  addr.sin_family = AF_INET;

  if ((dgramfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    return (1);

  senttime = now;

  sprintf(pbuf, "U\t%.20s\t%.20s\t%.20s\t%.10d\t0\t%.25s",
	  localnam, password, localnam, senttime, comment);
  sendto(dgramfd, pbuf, strlen(pbuf), 0, (struct sockaddr *)&addr, sizeof(addr));
  return (0);
}

/* disable RWHO */
int rwhocli_shutdown()
{
  char pbuf[512];

  if (dgramfd != -1)
  {
    sprintf(pbuf, "D\t%.20s\t%.20s\t%.20s", localnam, password, localnam);
    sendto(dgramfd, pbuf, strlen(pbuf), 0, (struct sockaddr *)&addr, sizeof(addr));
    close(dgramfd);
    dgramfd = -1;
    free(password);
    free(localnam);
  }
  return (0);
}

/* send an update ping that we're alive */
int rwhocli_pingalive()
{
  char pbuf[512];

  if (dgramfd != -1)
  {
    sprintf(pbuf, "M\t%.20s\t%.20s\t%.20s\t%.10d\t0\t%.25s",
	    localnam, password, localnam, senttime, lcomment);
    sendto(dgramfd, pbuf, strlen(pbuf), 0, (struct sockaddr *)&addr, sizeof(addr));
  }
  return (0);
}

/* send a "so-and-so-logged in" message */
int rwhocli_userlogin(uid, name, tim)
char *uid;
char *name;
time_t tim;
{
  char pbuf[512];

  if (dgramfd != -1)
  {
    sprintf(pbuf, "A\t%.20s\t%.20s\t%.20s\t%.20s\t%.10d\t0\t%.20s",
	    localnam, password, localnam, uid, tim, name);
    sendto(dgramfd, pbuf, strlen(pbuf), 0, (struct sockaddr *)&addr, sizeof(addr));
  }
  return (0);
}

/* send a "so-and-so-logged out" message */
int rwhocli_userlogout(uid)
char *uid;
{
  char pbuf[512];

  if (dgramfd != -1)
  {
    sprintf(pbuf, "Z\t%.20s\t%.20s\t%.20s\t%.20s",
	    localnam, password, localnam, uid);
    sendto(dgramfd, pbuf, strlen(pbuf), 0, (struct sockaddr *)&addr, sizeof(addr));
  }
  return (0);
}
