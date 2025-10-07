#include "config.h"
#include "externs.h"
 
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>

#include "sock.h"

/* Null device for reserving file descriptors */
static const char *NullFile = "logs/null";

/* Function prototypes */
int check_lockout(struct descriptor_data *, char *, char *);
void get_ident(char *, int, int, struct sockaddr_in);

void close_sockets(void)
{
  struct descriptor_data *d, *dnext;
  FILE *x = NULL;

  if (exit_status == 1)
  {
    unlink("logs/socket_table");
    x = fopen("logs/socket_table", "w");
    if (x) {
      fprintf(x, "%ld\n", (long)muse_up_time);
      fprintf(x, "%d\n", sock);
      fcntl(sock, F_SETFD, 0);
    }
  }

  for (d = descriptor_list; d; d = dnext)
  {
    dnext = d->next;
    if (!(d->cstatus & C_REMOTE))
    {
      if (exit_status == 1)
        write(d->descriptor, tprintf("%s %s", muse_name, REBOOT_MESSAGE), 
              (strlen(REBOOT_MESSAGE) + strlen(muse_name) + 1));
      else
        write(d->descriptor, tprintf("%s %s", muse_name, SHUTDOWN_MESSAGE), 
              (strlen(SHUTDOWN_MESSAGE) + strlen(muse_name) + 1));
      process_output(d);
#ifndef BOOT_GUEST
      if (x && d->player >= 0 && d->state == CONNECTED)
#else
      if (x && d->player >= 0 && d->state == CONNECTED && !Guest(d->player))
#endif
      {
        fprintf(x, "%010d %010ld %010ld %010ld\n", d->descriptor, 
                (long)d->connected_at, (long)d->last_time, (long)d->player);
        fcntl(d->descriptor, F_SETFD, 0);
      }
      else
        shutdownsock(d);
    }
  }
  if (x)
    fclose(x);
}

void open_sockets(void)
{
  struct descriptor_data *d, *oldd, *nextd;
  FILE *x = NULL;
  char buf[1024];

  if (!(x = fopen("logs/socket_table", "r")))
    return;
  unlink("logs/socket_table");
  
  if (fgets(buf, 1024, x)) {
    muse_up_time = atol(buf);
  }
  if (fgets(buf, 1024, x)) {
    sock = atoi(buf);
  }
  
  fcntl(sock, F_SETFD, 1);
  close(sock);
  
  for (sock = 0; sock < 1000; sock++)
    fcntl(sock, F_SETFD, 1);
    
  while (fgets(buf, 1024, x))
  {
    int desc = atoi(buf);
    struct sockaddr_in in;
    socklen_t namelen = sizeof(in);
    char buff[INET_ADDRSTRLEN];

    fcntl(desc, F_SETFD, 1);
    
    if (getpeername(desc, (struct sockaddr *)&in, &namelen) < 0) {
      continue;
    }

#ifdef HOST_LOOKUPS
    struct hostent *hent;
    hent = gethostbyaddr((char *)&(in.sin_addr.s_addr),
                         sizeof(in.sin_addr.s_addr), AF_INET);
    if (hent)
    {
      strncpy(buff, hent->h_name, sizeof(buff) - 1);
      buff[sizeof(buff) - 1] = '\0';
      log_io(tprintf("h_name: %s", hent->h_name));
    }
    else
#endif
    {
      inet_ntop(AF_INET, &in.sin_addr, buff, sizeof(buff));
    }
    
    d = initializesock(desc, &in, buff, RELOADCONNECT);
    if (d) {
      d->connected_at = atol(buf + 11);
      d->last_time = atol(buf + 22);
      d->player = atol(buf + 33);
    }
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

int make_socket(int port)
{ 
  int s;
  struct sockaddr_in server;
  int opt;
  
  /* Create socket */
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0)
  {
    perror("socket creation failed");
    log_error(tprintf("Failed to create socket on port %d: %s", port, strerror(errno)));
#ifndef RESOCK
    exit_status = 1;
    shutdown_flag = 1;
#endif
    return -1;
  }
  
  /* Set socket options */
  opt = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
  {
    perror("setsockopt SO_REUSEADDR failed");
    log_error(tprintf("setsockopt failed: %s", strerror(errno)));
#ifndef RESOCK
    shutdown_flag = 1;
    exit_status = 1;
#endif
    close(s);
    return -1;
  }
  
  /* Initialize server address structure */
  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_port = htons(port);

#ifdef MULTIHOME
  /* Try to resolve the configured hostname */
  struct addrinfo hints, *res = NULL;
  int err;
  
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  
  err = getaddrinfo(HOSTNAME, NULL, &hints, &res);
  if (err != 0)
  {
    log_error(tprintf("getaddrinfo failed for '%s': %s", HOSTNAME, gai_strerror(err)));
    log_error("Falling back to INADDR_ANY (binding to all interfaces)");
    server.sin_addr.s_addr = INADDR_ANY;
  }
  else
  {
    struct sockaddr_in *addr_in = (struct sockaddr_in *)res->ai_addr;
    server.sin_addr = addr_in->sin_addr;
    log_io(tprintf("Binding to hostname: %s", HOSTNAME));
    freeaddrinfo(res);
  }
#else
  /* Bind to all interfaces */
  server.sin_addr.s_addr = INADDR_ANY;
  log_io("Binding to all interfaces (INADDR_ANY)");
#endif
  
  /* Bind socket */
  if (bind(s, (struct sockaddr *)&server, sizeof(server)) == -1)
  { 
    perror("bind failed");
    log_error(tprintf("Failed to bind to port %d: %s", port, strerror(errno)));
    close(s);
#ifndef RESOCK
    shutdown_flag = 1;
    exit_status = 1;
#endif
    return -1;
  }
  
  /* Listen on socket */
  if (listen(s, 5) == -1)
  {
    perror("listen failed");
    log_error(tprintf("Failed to listen on port %d: %s", port, strerror(errno)));
    close(s);
#ifndef RESOCK
    shutdown_flag = 1;
    exit_status = 1;
#endif
    return -1;
  }
  
  log_io(tprintf("Successfully opened socket on port %d", port));
  return s;
}

struct descriptor_data *initializesock(int s, struct sockaddr_in *a, 
                                       char *addr, enum descriptor_state state)
{ 
  struct descriptor_data *d;
  time_t tt;
  char *ct;

  ndescriptors++;
  MALLOC(d, struct descriptor_data, 1);
  if (!d) {
    log_error("Failed to allocate descriptor");
    return NULL;
  }

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
  d->addr[49] = '\0';
  d->address = *a;
  
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
                 d->concid, d->user, addr, ct ? ct : "unknown"));
   
  if (state == WAITCONNECT)
  { 
    if (check_lockout(d, welcome_lockout_file, welcome_msg_file))
    {
      process_output(d);
      shutdownsock(d);
      return NULL;
    }
  }
 
  if (nologins)
  {
    log_io(tprintf("Refused connection on concid %ld due to @nologins.", d->concid));
    write(d->descriptor, tprintf("%s %s", muse_name, NOLOGINS_MESSAGE), 
          (strlen(NOLOGINS_MESSAGE) + strlen(muse_name) + 1));
    process_output(d);
    shutdownsock(d);
    return NULL;
  }
  
  if (d->descriptor >= maxd)
    maxd = d->descriptor + 1;
  return d;
}

void shutdownsock(struct descriptor_data *d)
{ 
  int count;
  dbref guest_player;
  struct descriptor_data *sd;
  
  if (!d) return;
  
  guest_player = NOTHING;
  if (d->state == CONNECTED && d->player > 0 && Guest(d->player))
    guest_player = d->player;

  if (d->state == CONNECTED && d->player > 0)
  { 
    time_t tt;
    char *ct;

    tt = now;
    ct = ctime(&tt);
    if (ct && *ct)
      ct[strlen(ct) - 1] = '\0';

    log_io(tprintf("|R+DISCONNECT| concid %ld player %s at %s",
                   d->concid, unparse_object_a(d->player, d->player), 
                   ct ? ct : "unknown"));
    com_send_as_hidden("pub_io", 
                       tprintf("|R+DISCONNECT| %s - %s",
                               unparse_object_a(d->player, d->player), 
                               ct ? ct : "unknown"), 
                       d->player);

    announce_disconnect(d->player);
  }
  else
  {
    log_io(tprintf("|R+DISCONNECT| concid %ld never connected", d->concid));
  }
  
  clearstrings(d);
  
  if (!(d->cstatus & C_REMOTE))
  { 
    shutdown(d->descriptor, SHUT_RDWR);
    close(d->descriptor);
  }
  else
  {
    struct descriptor_data *k;
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
  
  /* Clean up guest accounts */
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

void make_nonblocking(int s)
{ 
  int flags = fcntl(s, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl F_GETFL failed");
    return;
  }
  
  if (fcntl(s, F_SETFL, flags | O_NONBLOCK) == -1)
  {
    perror("fcntl F_SETFL O_NONBLOCK failed");
    panic("O_NONBLOCK fcntl failed");
  }
}

struct descriptor_data *new_connection(int sock)
{ 
  int newsock;
  struct sockaddr_in addr;
  socklen_t addr_len;
  char buff[INET_ADDRSTRLEN];
  
  addr_len = sizeof(addr);
  newsock = accept(sock, (struct sockaddr *)&addr, &addr_len);
  
  if (newsock < 0)
  { 
    if (errno == EALREADY || errno == EINTR)
    {
      static int k = 0;
      if (k++ > 50)
      {
        log_error("Too many EALREADY errors, restarting socket");
        close(sock);
        sock = make_socket(inet_port);
        k = 0;
      }
    }
    else if (errno != EWOULDBLOCK && errno != EAGAIN)
    {
      log_error(tprintf("accept() failed: %s", strerror(errno)));
    }
    return NULL;
  }
  
#ifdef HOST_LOOKUPS
  struct hostent *hent;
  hent = gethostbyaddr((char *)&(addr.sin_addr.s_addr),
                       sizeof(addr.sin_addr.s_addr), AF_INET);
  if (hent)
  {
    strncpy(buff, hent->h_name, sizeof(buff) - 1);
    buff[sizeof(buff) - 1] = '\0';
    log_io(tprintf("Connection from: %s", hent->h_name));
  }
  else
#endif
  {
    inet_ntop(AF_INET, &addr.sin_addr, buff, sizeof(buff));
  }

  return initializesock(newsock, &addr, buff, WAITCONNECT);
}

void clearstrings(struct descriptor_data *d)
{
  if (!d) return;
  
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

void freeqs(struct descriptor_data *d)
{
  struct text_block *cur, *next;
  
  if (!d) return;
  
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

int check_lockout(struct descriptor_data *d, char *file, char *default_msg)
{ 
  FILE *f;
  char *lock_host, *lock_enable, *msg_file, *ptr;
  char buf[1024];
  struct hostent *hent;
  
  if (!d) return 1;
  
  close(reserved);
  
  f = fopen(file, "r");
  if (!f)
  { 
    queue_string(d, "Error opening lockout file.\n");
    reserved = open(NullFile, O_RDWR, 0);
    return 1;
  }
  
  while (fgets(buf, 1024, f))
  { 
    if (*buf == '#' || *buf == '\0' || *buf == '\n')
      continue;
      
    if (buf[strlen(buf) - 1] == '\n')
      buf[strlen(buf) - 1] = '\0';
      
    ptr = buf;
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
      
    if (*(long *)hent->h_addr_list[0] == d->address.sin_addr.s_addr)
    { 
      fclose(f);
      connect_message(d, msg_file, 0);
      reserved = open(NullFile, O_RDWR, 0);
      return (*lock_enable == 'l' || *lock_enable == 'L');
    }
  }
  
  fclose(f);
  connect_message(d, default_msg, 0);
  reserved = open(NullFile, O_RDWR, 0);
  return 0;
}

#ifdef RESOCK
void resock(void)
{ 
  log_io("Resocking...");
  close(sock);
  sock = make_socket(inet_port);
  log_io("Resocking done");
}
#endif

void get_ident(char *rslt, int sock, int timout, struct sockaddr_in remoteaddr)  
{
  struct sockaddr_in localaddr, sin_addr; 
  struct timeval timeout;
  fd_set filedes;
  char buf[128], *r, *s;
  int err, fd;
  socklen_t len;

  strcpy(rslt, "???");

  len = sizeof(localaddr);
  if (getsockname(sock, (struct sockaddr *)&localaddr, &len))
    return;

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    return;
    
  fcntl(fd, F_SETFL, O_NONBLOCK);
  
  memset(&sin_addr, 0, sizeof(sin_addr));
  sin_addr.sin_family = AF_INET;
  sin_addr.sin_addr = localaddr.sin_addr;
  sin_addr.sin_port = 0;
  
  if (bind(fd, (struct sockaddr *)&sin_addr, sizeof(sin_addr)))
  {
    close(fd);
    return;
  }

  sin_addr.sin_family = AF_INET;
  sin_addr.sin_addr = remoteaddr.sin_addr;
  sin_addr.sin_port = htons(113);
  connect(fd, (struct sockaddr *)&sin_addr, sizeof(sin_addr));

  FD_ZERO(&filedes);
  FD_SET(fd, &filedes);
  timeout.tv_sec = timout;
  timeout.tv_usec = 0;
  
  if (select(fd + 1, NULL, &filedes, NULL, &timeout) < 1)
  {
    close(fd);
    return;
  }
  
  len = sizeof(err);
  getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
  if (err)
  {
    close(fd);
    return;
  }

  sprintf(buf, "%d,%d\r\n", ntohs(remoteaddr.sin_port),
          ntohs(localaddr.sin_port));
  write(fd, buf, strlen(buf));

  FD_ZERO(&filedes);
  FD_SET(fd, &filedes);
  timeout.tv_sec = 2;
  timeout.tv_usec = 0;
  
  if (select(fd + 1, &filedes, NULL, NULL, &timeout) < 1)
  {
    close(fd);
    return;
  }
  
  len = read(fd, buf, 127);
  close(fd);
  
  if (len <= 2)
    return;
    
  buf[len] = '\0';

  for (r = buf, len = 0; len < 3; len++, r++)
    if (!(r = strchr(r, ':')))
      return;

  if ((s = strchr(r, '\n')))
    *s = '\0';
  if ((s = strchr(r, '\r')))
    *s = '\0';

  while (*r && isspace((unsigned char)*r))
    r++;
    
  strncpy(rslt, r, 31);
  rslt[31] = '\0';
}
