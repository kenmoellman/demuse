
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <resolv.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "db.h"
#include "externs.h"

struct email_server_struct
{
  char *server;
  int pref;
};

struct email_struct
{
  int socket;
  OBJ *player;
  OBJ *victim;
  char *from;
  char *send_to;
  char *message;
  int num_servers;
  int cur_server;
  struct email_server_struct *email_servers;
  struct email_struct *next;
};

typedef struct email_server_struct EMAIL_SRVR;
typedef struct email_struct EMAIL;

EMAIL *pending_email_list = NULL;

/* Protypes */
static int try_next_server P((EMAIL *));
static void send_email P((EMAIL *));

static void fail_mail(OBJ *player, int fd, char *msg)
{
  if(msg)
    notify(player, tprintf("EMAIL ERROR: %s", msg));
  else
    notify(player, "There was an error while sending your email.");

  if(fd >= 0)
    close(fd);
}

static int get_reply(int fd)
{
  char buf[4096];

  if(read(fd, buf, sizeof(buf)) <= 0)
    return(-1);

  return(atoi(buf));
}

static int send_reply(OBJ *player, int fd, char *msg)
{
  if(write(fd, tprintf("%s\r\n", msg), strlen(msg)+2) <= 0)
  {
    fail_mail(player, fd, "Couldn't write to server.");
    return(0);
  }

  return(1);
}

static int is_valid_address(char *address)
{
  if(!*address || !strchr(address, '@') || strchr(address, ' '))
    return(0);
  return(1);
}

static EMAIL *is_sending_email(OBJ *player)
{
  EMAIL *e;

  for(e = pending_email_list;e;e = e->next)
    if(e->player == player)
      break;

  return(e);
}

static EMAIL *add_email_send(OBJ *player, OBJ *victim,
                             char *from, char *send_to, char *msg)
{
  EMAIL *new;

  new = (EMAIL *)malloc(sizeof(EMAIL));
  new->socket = 0;
  new->player = player;
  new->victim = victim;
  new->num_servers = 0;
  new->cur_server = -1;
  new->email_servers = NULL;
  SET(&new->message, msg);
  SET(&new->from, from);
  SET(&new->send_to, send_to);

  new->next = pending_email_list;
  pending_email_list = new;

  return(new);
}

static void del_email_send(EMAIL *email)
{
  EMAIL *e, *eprev = NULL;

  for(e = pending_email_list;e;e = e->next)
  {
    if(e == email)
      break;
    eprev = e;
  }

  if(!e)
    return;

  if(eprev)
    eprev->next = e->next;
  else
    pending_email_list = e->next;

  free(e->message);
  free(e->send_to);
  free(e->email_servers);
  free(e);
}

void poll_pending_email()
{
  EMAIL *e, *enext;
  fd_set set;
  int max_sock = 0;
  int rv;
  struct timeval tv;
  int blocking_flags;
  int err, len = sizeof(int);  /* For getsockopt() */

  FD_ZERO(&set);
  tv.tv_sec = 0;       /* Don't */
  tv.tv_usec = 0;      /* Wait  */

  for(e = pending_email_list;e;e = e->next)
  {
    FD_SET(e->socket, &set);
    if(e->socket > max_sock)
      max_sock = (e->socket)+1;
  }

  if((rv = select(max_sock, NULL, &set, NULL, &tv)) == -1)
  {
    log_error(tprintf("poll_pending_email() - select(): %s",
      strerror(errno)));
    return;
  }

  if(!rv)   /* Nothing ready */
    return;

  for(e = pending_email_list;e;e = enext)
  {
    enext = e->next;

    if(!FD_ISSET(e->socket, &set))
      continue;

    getsockopt(e->socket, SOL_SOCKET, SO_ERROR, (void *)&err, &len);
    if(err)    /* connect() failed */
    {
      while((rv = try_next_server(e)) == 0);   /* Whole loop */
      if(rv == -1)
      {
        notify(e->player, tprintf("I couldn't send email to %s.",
          (e->victim)?name(e->victim):e->send_to));
        del_email_send(e);
      }
      continue;
    }

    blocking_flags = fcntl(e->socket, F_GETFL);
    blocking_flags &= ~O_NONBLOCK;
    fcntl(e->socket, F_SETFL, blocking_flags);
    send_email(e);
    del_email_send(e);
  }
}

static unsigned short getshort(unsigned char *ptr)
{
  unsigned short out;

  out = (ptr[0] << 8) | ptr[1];
  return(out);
}

static void add_server(EMAIL *email, char *server, int pref, int *ctr)
{
  email->email_servers = (EMAIL_SRVR *)realloc(email->email_servers,
    sizeof(EMAIL_SRVR)*((*ctr)+1));
  SET(&email->email_servers[*ctr].server, server);
  email->email_servers[*ctr].pref = pref;
  (*ctr)++;
}

static void sort_mail_servers(EMAIL *email)
{
  EMAIL_SRVR *ei, *ej;
  char stemp[4096];
  int itemp;
  int i, j;

  for(i = 0;i < email->num_servers;++i)
    for(j = i+1;j < email->num_servers;++j)
      if(email->email_servers[j].pref < email->email_servers[i].pref)
      {
        ei = &email->email_servers[i];
        ej = &email->email_servers[j];

        strcpy(stemp, ei->server);
        free(ei->server);
        SET(&ei->server, ej->server);
        free(ej->server);
        SET(&ej->server, stemp);
        itemp = ei->pref;
        ei->pref = ej->pref;
        ej->pref = itemp;
      }
}

static int get_email_servers(EMAIL *email, char *hostname)
{
  union
  {
    HEADER hdr;
    unsigned char buf[1024];
  } response;
  int response_len;
  unsigned char *response_end, *response_pos;
  char name[1024];
  unsigned short rrtype, rrdlen;
  int pref;
  int n, i;

  response_len =
    res_query(hostname, C_IN, T_MX, response.buf, sizeof(response));
  if(response_len < 0)
  {
    add_server(email, hostname, 0, &email->num_servers);
    return(email->num_servers);
  }
  if(response_len >= sizeof(response))
    return(0);

  response_end = response.buf+response_len;
  response_pos = response.buf+sizeof(HEADER);

  n = ntohs(response.hdr.qdcount);
  while(n-- > 0)
  {
    if((i =
      dn_expand(response.buf, response_end, response_pos, name, MAXDNAME)) < 0)
    {
      return(0);
    }

    response_pos += i;
    i = response_end - response_pos;
    if(i < QFIXEDSZ)
      return(0);
    response_pos += QFIXEDSZ;
  }

  n = ntohs(response.hdr.ancount);
  while(n-- > 0)
  {
    if((i =
      dn_expand(response.buf, response_end, response_pos, name, MAXDNAME)) < 0)
    {
      return(0);
    }

    response_pos += i;
    rrtype = getshort(response_pos);
    rrdlen = getshort(response_pos+8);
    response_pos += 10;

    if(rrtype == T_MX)
    {
      if(rrdlen < 3)
        return(0);

      pref = getshort(response_pos);

      if((i =
        dn_expand(response.buf, response_end, response_pos+2, name, MAXDNAME)) < 0)
      {
        return(0);
      }

      add_server(email, name, pref, &email->num_servers);
    }

    response_pos += rrdlen;
  }

  sort_mail_servers(email);
  return(email->num_servers);
}

void do_email(OBJ *player, char *arg1, char *msg)
{
  EMAIL *e;
  OBJ *victim = NULL;
  char address[4096], from[4096];
  char *hostname;

  if((e = is_sending_email(player)))
  {
    notify(player, tprintf("You're already sending email to %s.",
      (e->victim)?name(e->victim):e->send_to));
    notify(player, "You may send another email when it is done.");
    return;
  }

  strcpy(from, atr_get(player, A_EMAIL));
  strcpy(address, arg1);

  if(!is_valid_address(from))
  {
    notify(player,
      "Your EMAIL attribute must be set to a valid reply-to address.");
    return;
  }

  if(!is_valid_address(address))
  {
    if(!(victim = match_player(NULL, address)))
    {
      notify(player, tprintf("Invalid recipient: %s", address));
      return;
    }
    strcpy(address, atr_get(victim, A_EMAIL));
    if(!is_valid_address(address))
    {
      notify(player, tprintf("%s EMAIL attribute is not set.",
        poss(victim)));
      return;
    }
  }

  hostname = strchr(address, '@');
  *hostname++ = '\0';

/* A period (.) will interrupt the message DATA */
  if(!*msg || *msg == '.')
  {
    notify(player, "You must specify a message.");
    return;
  }

  e = add_email_send(player, victim, from,
    tprintf("%s@%s", address, hostname), msg);

  if(!get_email_servers(e, hostname))
  {
    notify(player, tprintf("I can't send email to %s.",
      (victim)?name(victim):tprintf("%s@%s", address, hostname)));
    del_email_send(e);
    return;
  }

  notify(player,
    tprintf("Attempting to send email to %s...Please wait.",
    (victim)?name(victim):tprintf("%s@%s", address, hostname)));

  try_next_server(e); /* Ignore return value. There's at least one server */
}

/* Return values: (-1) - Every server exhausted
                  ( 0) - Current server failed
                  ( 1) - Connecting to valid server
*/
static int try_next_server(EMAIL *e)
{
  struct sockaddr_in addr;
  struct hostent *hent;
  char *email_server;

  e->cur_server++;

  if(e->cur_server >= e->num_servers)
    return(-1);

  email_server = e->email_servers[e->cur_server].server;

  if((e->socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    notify(e->player, "I can't send email at this time.");
    return(0);
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(25);  /* SMTP server is on port 25 */

  if(!(hent = gethostbyname(email_server)))
  {
    if(!inet_aton(email_server, &addr.sin_addr))
      return(0);
  }
  else
    addr.sin_addr.s_addr = *(long *)hent->h_addr_list[0];

  fcntl(e->socket, F_SETFL, O_NONBLOCK);
  connect(e->socket, (struct sockaddr *)&addr, sizeof(addr));
  return(1);
}

void send_email(EMAIL *e)
{
  const char *serverid = "demuse@mu-net.org";
  const char *admin_email = "newmark@mu-net.org";
  int replies[] = { 220, 250, 250, 250, 354, 250 };
  int *reply = replies;

  if(get_reply(e->socket) != *reply++)
  {
    fail_mail(e->player, e->socket, NULL);
    return;
  }

  if(!send_reply(e->player, e->socket, "HELO master"))
    return;
  if(get_reply(e->socket) != *reply++)
  {
    fail_mail(e->player, e->socket, "Introduction failed.");
    return;
  }
  if(!send_reply(e->player, e->socket, tprintf("MAIL FROM:<%s>", serverid)))
    return;
  if(get_reply(e->socket) != *reply++)
  {
    fail_mail(e->player, e->socket, "Invalid sender.");
    return;
  }
  if(!send_reply(e->player, e->socket, tprintf("RCPT TO:<%s>", e->send_to)))
    return;
  if(get_reply(e->socket) != *reply++)
  {
    fail_mail(e->player, e->socket, "Invalid recipient.");
    return;
  }

  if(!send_reply(e->player, e->socket, "DATA"))
    return;
  if(get_reply(e->socket) != *reply++)
  {
    fail_mail(e->player, e->socket, "Attempt for DATA send denied.");
    return;
  }

  if(!send_reply(e->player, e->socket, tprintf("Reply-To:%s", e->from)))
    return;
  if(!send_reply(e->player, e->socket,
    tprintf("Subject:A message from %s on %s",
    strip_color(unparse_object(e->player, e->player)), maze_name)))
  {
    return;
  }
  if(!send_reply(e->player, e->socket, "---------- Message ----------"))
    return;
  if(!send_reply(e->player, e->socket, e->message))
    return;
  if(!send_reply(e->player, e->socket, "-------- End Message --------"))
    return;
  if(!send_reply(e->player, e->socket,
    tprintf("\r\nThis message was automatically delivered by the %s server.",
    maze_name)))
  {
    return;
  }
  if(!send_reply(e->player, e->socket,
    tprintf("If the sender of this email (%s - %s) is harassing you,"
    " send email to %s and the player will be disallowed from sending"
    " emails to your address.",
    strip_color(unparse_object(e->player, e->player)),
    e->from, admin_email)))
  {
    return;
  }
  if(!send_reply(e->player, e->socket, "."))
    return;
  if(get_reply(e->socket) != *reply++)
  {
    fail_mail(e->player, e->socket, "DATA message was refused.");
    return;
  }

  if(e->victim)
    notify(e->player, tprintf("Your email to %s was successfully sent.",
      name(e->victim)));
  else
    notify(e->player, "Your email was successfully sent.");

  close(e->socket);
}
