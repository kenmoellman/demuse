 
#include "config.h"
#include "externs.h"
 
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>

#include "sock.h"


int check_lockout P((struct descriptor_data *, char *, char *));
void get_ident(char *, int, int, struct sockaddr_in);


int reserved;
int sock;

void close_sockets()
{
  struct descriptor_data *d, *dnext;
  FILE *x = NULL;

  if (exit_status == 1)
  {
    unlink("logs/socket_table");
    x = fopen("logs/socket_table", "w");
    fprintf(x, "%ld\n", muse_up_time);
    fprintf(x, "%d\n", sock);
    fcntl(sock, F_SETFD, 0);
  }

/*shutdown_flag = 1;          * just in case. so announce_disconnect *
                              * doesn't reset the CONNECT flags.     */

  for (d = descriptor_list; d; d = dnext)
  {
    dnext = d->next;
    if (!(d->cstatus & C_REMOTE))
    {
      if (exit_status == 1)
	write(d->descriptor, tprintf("%s %s", muse_name, REBOOT_MESSAGE), (strlen(REBOOT_MESSAGE) + strlen(muse_name) + 1));
      else
	write(d->descriptor, tprintf("%s %s", muse_name, SHUTDOWN_MESSAGE), (strlen(SHUTDOWN_MESSAGE) + strlen(muse_name) + 1));
      process_output(d);
#ifndef BOOT_GUEST
      if (x && d->player >= 0 && d->state == CONNECTED )
#else
      if (x && d->player >= 0 && d->state == CONNECTED && !Guest(d->player))
#endif
      {
	fprintf(x, "%010d %010ld %010ld %010ld\n", d->descriptor, d->connected_at, d->last_time, d->player);
	fcntl(d->descriptor, F_SETFD, 0);
      }
      else
	shutdownsock(d);
    }
  }
  if (x)
    fclose(x);
}

void open_sockets()
{
  struct descriptor_data *d, *oldd, *nextd;
  FILE *x = NULL;
  char buf[1024];

  if (!(x = fopen("logs/socket_table", "r")))
    return;
  unlink("logs/socket_table");	/* so we don't try to use it again. */
  fgets(buf, 1024, x);
  muse_up_time = atol(buf);
  fgets(buf, 1024, x);
  sock = atoi(buf);
  fcntl(sock, F_SETFD, 1);
  close(sock);
  for (sock = 0; sock < 1000; sock++)
    fcntl(sock, F_SETFD, 1);
  while (fgets(buf, 1024, x))
  {
    int desc = atoi(buf);
    struct sockaddr_in in;
    extern char *inet_ntoa();
    int namelen = sizeof(in);

    fcntl(desc, F_SETFD, 1);
    getpeername(desc, (struct sockaddr *)&in, &namelen);
    {
      char buff[100];
#ifdef HOST_LOOKUPS
/* when the game comes back online after a reboot, these connections are reopened. */
      struct hostent *hent;

      hent = gethostbyaddr((char *)&(in.sin_addr.s_addr),
			   sizeof(in.sin_addr.s_addr), AF_INET);
      if (hent)
      {
	strcpy(buff, hent->h_name);
        log_io(tprintf("h_name: %s  h_addr_list[0]: %ld  h_length: %d", hent->h_name, (long *)hent->h_addr_list[0], hent->h_length));
      }
      else
      {
#endif /* HOST_LOOKUPS */
	extern char *inet_ntoa();

	strcpy(buff, inet_ntoa(in.sin_addr));
#ifdef HOST_LOOKUPS
      } 
#endif
      d = initializesock(desc, &in, buff, RELOADCONNECT);
    }
    d->connected_at = atol(buf + 11);
    d->last_time = atol(buf + 22);
    d->player = atol(buf + 33);
  }
  fclose(x);
  oldd = descriptor_list;
  descriptor_list = NULL;
  for (d = oldd; d; d = nextd)
  {
    nextd = d->next;
    d->next = descriptor_list;
    descriptor_list = d;
  }
  oldd = NULL;
  for (d = descriptor_list; d; d = d->next)
  {
    if (oldd == NULL)
      d->prev = &descriptor_list;
    else
      d->prev = &oldd->next;
    oldd = d;
  }
}

int make_socket(port)
int port;
{ 
  int s;
  struct sockaddr_in server;
  int opt;
#ifdef MULTIHOME
  unsigned long int inaddr;
#endif
  
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0)
  {
    perror(tprintf("creating stream socket on port %d", port));
#ifndef RESOCK
    exit_status = 1;            /* try again. */
    shutdown_flag = 1;
#endif
    return -1;
  }
  opt = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                 (char *)&opt, sizeof(opt)) < 0)
  {
    perror("setsockopt");
#ifndef RESOCK
    shutdown_flag = 1;
    exit_status = 1;
#endif
    close(s);
    return -1;
  }
  
  server.sin_family = AF_INET;

#ifdef MULTIHOME
  inaddr = inet_addr(HOSTNAME); /* Try out #.#.#.# address format first */
  if (inaddr != INADDR_NONE)
  { 
    memcpy(&server.sin_addr, &inaddr, sizeof(inaddr));
  }
  else
  {

#ifndef CACHED_SERVER_HENT
    struct hostent *hent_server;
#else
    static struct hostent *hent_server;  /* okay, let's try caching the hent for the server. wm 05/08/2000 */

    if (!hent_server)
#endif
      hent_server = gethostbyname(HOSTNAME);     /* Try dns lookup of address, since # 
                                            format failed */
    if (hent_server == NULL)
      return -1;
  
    memcpy(&server.sin_addr, hent_server->h_addr_list[0], hent_server->h_length);
  }
  server.sin_port = htons(port);
  if (bind(s, (struct sockaddr *)&server, sizeof(server)) == -1)
  { 
    perror("binding stream socket");
    close(s);
#ifndef RESOCK
    shutdown_flag = 1;
    exit_status = 1;
#endif /* RESOCK */
    return -1;
  }
#else /* if not MULTIHOME */
  
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(port);
  
  if (bind(s, (struct sockaddr *)&server, sizeof(server)) == -1)
  { 
    perror("binding stream socket");
    close(s);
#ifndef RESOCK
    shutdown_flag = 1;
    exit_status = 1;
#endif /* RESOCK */
    return -1;
  }
#endif /* MULTIHOME */
  
  listen(s, 5);
  return s;
}


struct descriptor_data *initializesock(s, a, addr, state)
int s;
struct sockaddr_in *a;
char *addr;
enum descriptor_state state;
{ 
  struct descriptor_data *d;
  time_t tt;
  char *ct;

  /* fprintf(stderr,"3\n");fflush(stderr); */
  ndescriptors++;
  MALLOC(d, struct descriptor_data, 1);

  d->snag_input = 0;
  d->descriptor = s;
  d->concid = make_concid();
  d->cstatus = 0;
  d->parent = 0;
  d->state = state;
  make_nonblocking(s);
  d->output_prefix = 0;
  d->output_suffix = 0;
  d->output_size = 0;
  d->output.head = 0;
  d->output.tail = &d->output.head;
  d->input.head = 0;
  d->input.tail = &d->input.head;
  d->raw_input = 0;
  d->raw_input_at = 0;
  d->quota = command_burst_size;
  d->last_time = now;
  strncpy(d->addr, addr, 50);
  d->address = *a;              /* added 5/3/90 SCG */
  if (descriptor_list)
    descriptor_list->prev = &d->next;
  d->next = descriptor_list;
  d->prev = &descriptor_list;
  descriptor_list = d;
  get_ident(d->user, s, 3, *a);
  
  tt = now;
  ct = ctime(&tt);
  if (ct && *ct)
    ct[strlen(ct) - 1] = '\0';
  
  log_io(tprintf("|G+USER CONNECT|: concid: %ld host %s@%s time: %s",
                 d->concid, d->user, addr, ct));
   
  if (state == WAITCONNECT)
  { 
    if (check_lockout(d, welcome_lockout_file, welcome_msg_file))
    {
      process_output(d);
      shutdownsock(d);
    }
  }
 
  if (nologins)
  {
    log_io(tprintf("Refused connection on concid %ld due to @nologins.",
                   d->concid));
    write(d->descriptor, tprintf("%s %s", muse_name, NOLOGINS_MESSAGE), (strlen(NOLOGINS_MESSAGE) + strlen(muse_name) + 1));
    process_output(d);
    shutdownsock(d);
    return 0;
  }
  if (d->descriptor >= maxd)
    maxd = d->descriptor + 1;
  return d;
}

void shutdownsock(d)
struct descriptor_data *d;
{ 
  int count;
  dbref guest_player;
  struct descriptor_data *sd;
  
  /* if this is a guest player, save his reference # */
  guest_player = NOTHING;
  if (d->state == CONNECTED)
    if (d->player > 0)
      if (Guest(d->player))
        guest_player = d->player;

  if (d->state == CONNECTED)
  {
    if (d->player > 0)
    { 
      time_t tt;
      char *ct;

      tt = now;
      ct = ctime(&tt);
      if (ct && *ct)
        ct[strlen(ct) - 1] = '\0';

      log_io(tprintf("|R+DISCONNECT| concid %ld player %s at %s",
                     d->concid, unparse_object_a(d->player, d->player), ct));
      com_send_as_hidden("pub_io",tprintf("|R+DISCONNECT| %s - %s",
                     unparse_object_a(d->player, d->player), ct), d->player);

      announce_disconnect(d->player);
    }
  }
  else
    log_io(tprintf("|R+DISCONNECT| concid %ld never connected",
                   d->concid));
  
  clearstrings(d);
  if (!(d->cstatus & C_REMOTE))
  { 
    shutdown(d->descriptor, 0);
    close(d->descriptor);
  }
  else
  {
    register struct descriptor_data *k;

    for (k = descriptor_list; k; k = k->next)
      if (k->parent == d)
        k->parent = 0;
  }
  freeqs(d);
  *d->prev = d->next;
  if (d->next)
    d->next->prev = d->prev;
  if (!(d->cstatus & C_REMOTE))
    ndescriptors--;
  
  FREE(d);
  
  /* if this is a guest account and the last to disconnect from it, kill it */
  if (guest_player != NOTHING)
  {
    count = 0;
    for (sd = descriptor_list; sd; sd = sd->next)
    {
      if (sd->state == CONNECTED && sd->player == guest_player)
        ++count;
    }
    if (count == 0)
      destroy_guest(guest_player);
  }
}


void make_nonblocking(s)
int s;
{ 
/*  if (fcntl(s, F_SETFL, FNDELAY) == -1) */
  if (fcntl(s, F_SETFL, O_NONBLOCK) == -1)
  {
    perror("make_nonblocking: fcntl");
    panic("FNDELAY fcntl failed");
  }
}

struct descriptor_data *new_connection(sock)
int sock;
{ 
  int newsock;
  struct sockaddr_in addr;
  int addr_len;
  
  addr_len = sizeof(addr);
  newsock = accept(sock, (struct sockaddr *)&addr, &addr_len);
  if (newsock < 0)
  { 
    if (errno == EALREADY)
    {                           /* screwy sysv buf. */
      static int k = 0;

      if (k++ > 50)
      {
        log_error("Killing EALREADY, restarting socket.");
        puts("Killing EALREADY, restarting socket.");
        close(sock);
        sock = make_socket(inet_port);
        k = 0;
      }
    }
    return 0;
  }
  else
  { 
    char buff[100];
#ifdef HOST_LOOKUPS
/* when the game has a new connection, reverse resolve it's host. */
    struct hostent *hent;

    hent = gethostbyaddr((char *)&(addr.sin_addr.s_addr),
                         sizeof(addr.sin_addr.s_addr), AF_INET);
    if (hent)
    {
      strcpy(buff, hent->h_name);
      log_io(tprintf("h_name: %s  h_addr_list[0]: %ld  h_length: %d", hent->h_name, (long *)hent->h_addr_list[0], hent->h_length));
    }
    else
    {
#endif /* HOST_LOOKUPS */
      extern char *inet_ntoa();

      strcpy(buff, inet_ntoa(addr.sin_addr));
#ifdef HOST_LOOKUPS
    } 
#endif /* HOST_LOOKUPS */

    return initializesock(newsock, &addr, buff, WAITCONNECT);
  }
}

void clearstrings(d)
struct descriptor_data *d;
{
  if (d->output_prefix)
  {
    FREE(d->output_prefix);
    d->output_prefix = 0;
  }
  if (d->output_suffix)
  {
    FREE(d->output_suffix);
    d->output_suffix = 0;
  }
}

void freeqs(d)
struct descriptor_data *d;
{
  struct text_block *cur, *next;
  
  cur = d->output.head;
  while (cur)
  {
    next = cur->nxt;
    free_text_block(cur);
    cur = next;
  }
  d->output.head = 0;
  d->output.tail = &d->output.head;
  
  cur = d->input.head;
  while (cur)
  {
    next = cur->nxt;
    free_text_block(cur);
    cur = next;
  }
  d->input.head = 0;
  d->input.tail = &d->input.head;
  
  if (d->raw_input)
    FREE(d->raw_input);
  d->raw_input = 0;
  d->raw_input_at = 0;
}

int check_lockout(d, file, default_msg)
struct descriptor_data *d;
char *file;
char *default_msg;
{ 
  FILE *f;
  char *lock_host, *lock_enable, *msg_file, *ptr;
  char buf[1024];
  struct hostent *hent;
  
  close(reserved);
  
  f = fopen(file, "r");
  if (!f)
  { 
    queue_string(d, "Error opening lockout file.\n");
    return 1;
  }
  while (fgets(buf, 1024, f))
  { 
    if (*buf)
    {
      buf[strlen(buf) - 1] = '\0';
    }
    else
      continue;  /* added else continue to skip checking other stuff, if buf is empty. duh.  wm 05/08/2000 */
    ptr = buf;
    if (!(*ptr = '#'))
    {
      if (!(lock_host = parse_up(&ptr, ' ')))
        continue;
      if (!(lock_enable = parse_up(&ptr, ' ')))
        continue;
      if (!(msg_file = parse_up(&ptr, ' ')))
        continue;
      if (parse_up(&ptr, ' '))
        continue;
      hent = gethostbyname(lock_host);
      if (!hent)
        continue;
      log_io(tprintf("h_name: %s  h_addr_list[0]: %ld  h_length: %d", hent->h_name, (long *)hent->h_addr_list[0], hent->h_length));
      if (*(long *)hent->h_addr_list[0] == d->address.sin_addr.s_addr)
      { 
        /* bingo. */
        fclose(f);
        connect_message(d, msg_file, 0);
        return *lock_enable == 'l' || *lock_enable == 'L';
      }
    }
  }
  fclose(f);
  connect_message(d, default_msg, 0);
  return 0;
}

#ifdef RESOCK
void resock()
{ 
  log_io("Resocking...");
  close(sock);
  sock = make_socket(port);
  log_io("Resocking done");
}
#endif


#ifdef USE_OUTGOING
static struct descriptor_data *open_outbound(player, host, port)
dbref player;
char *host;
int port;
{
  struct descriptor_data *d;
  int sock;
  struct sockaddr_in addr;
  struct hostent *hent;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return 0;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (!(hent = gethostbyname(host)))
    addr.sin_addr.s_addr = inet_addr(host);
  else
    addr.sin_addr.s_addr = *(long *)hent->h_addr_list[0];

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
  {
    close(sock);
    return 0;
  }
  d = initializesock(sock, &addr, host, CONNECTED);
  d->player = player;
  d->last_time = d->connected_at = now;
  db[d->player].flags |= CONNECT;
  return d;
}

void do_outgoing(player, arg1, arg2)
dbref player;
char *arg1, *arg2;
{
  dbref thing1;
  char host[1024];
  int port;
  struct descriptor_data *d;

  if (!power(player, POW_OUTGOING))
  {
    notify(player, perm_denied());
    return;
  }
  if ((thing1 = match_controlled(player, arg1, POW_BOOT)) == NOTHING)
    return;
  if (!*atr_get(thing1, A_INCOMING))
  {
    notify(player, "You need to set your @incoming attribute.");
    return;
  }
  if (!*arg2 || !strchr(arg2, ' '))
  {
    notify(player, "You must specify a port number.");
    return;
  }
  strcpy(host, arg2);
  *strchr(host, ' ') = '\0';
  port = atoi(strchr(arg2, ' ') + 1);
  if (port <= 0)
  {
    notify(player, "Bad port.");
    return;
  }
  if ((d = open_outbound(thing1, host, port)))
  {
    did_it(player, thing1, NULL, NULL, NULL, NULL, A_ACONN);
    log_io(tprintf("%s opened outbound connection to %s, concid %d, attached to %s", unparse_object_a(root, player), arg2, d->concid, unparse_object_a(root, thing1)));
  }
  else
    notify(player, tprintf("Problems opening connection. errno %d.", errno));
}
#endif


/*
 * This code was taken from the TinyMAZE project, who took it from the TinyMARE
 * project, and then modified.  Rights go to Byron Stanoszek (Gandalf) 
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/errno.h>
#include "externs.h"

void get_ident(char *rslt, int sock, int timout, struct sockaddr_in remoteaddr)  
{
  struct sockaddr_in localaddr, sin_addr; 
  struct timeval timeout;
  fd_set filedes;
  char buf[128], *r, *s;
  int err, fd, len;

  strcpy(rslt, "???");

  len = sizeof(localaddr);
  if(getsockname(sock, (struct sockaddr *)&localaddr, &len))
    return;

/* Open socket connection to remote address */
  if((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    return;
  fcntl(fd, F_SETFL, O_NDELAY);
  sin_addr.sin_family = AF_INET;
  sin_addr.sin_addr = localaddr.sin_addr;
  sin_addr.sin_port = 0;
  if(bind(fd, (struct sockaddr *)&sin_addr, sizeof(sin_addr)))
  {
    close(fd);
    return;
  }

/* Attempt to do socket connection in progress, timeout is timout seconds */
  sin_addr.sin_family = AF_INET;
  sin_addr.sin_addr = remoteaddr.sin_addr;
  sin_addr.sin_port = htons(113); /* identd port */
  connect(fd, (struct sockaddr *)&sin_addr, sizeof(sin_addr));

  FD_ZERO(&filedes);
  FD_SET(fd, &filedes);
  timeout.tv_sec = timout;
  timeout.tv_usec = 0;
  if(select(fd+1, 0, &filedes, 0, &timeout) < 1)
  {
    close(fd);
    return;
  }
  len = sizeof(err);
  getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
  if(err)
  {
    close(fd);
    return;
  }

/* Send identification request */
  sprintf(buf, "%d,%d\r\n", ntohs(remoteaddr.sin_port),
          ntohs(localaddr.sin_port));
  write(fd, buf, strlen(buf));

/* Wait for return input */
  timeout.tv_sec = 2;
  timeout.tv_usec = 0;
  if(select(fd+1, &filedes, 0, 0, &timeout) < 1)
  {
    close(fd);
    return;
  }
/* Read input */
  len = read(fd, buf, 127);
  close(fd);
  if(len <= 2)  /* read error */
    return;
  buf[len] = '\0';

/* Parse input string and search for userid */
  for(r = buf, len = 0;len < 3;len++, r++)
    if(!(r = strchr(r, ':')))
      return;

  if((s = strchr(r, '\n')))
    *s = '\0';
  if((s = strchr(r, '\r')))
    *s = '\0';

  while(*r && isspace(*r))
    r++;
  strncpy(rslt, r, 31);
  rslt[31]='\0';
}
