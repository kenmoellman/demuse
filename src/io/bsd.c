/* bsd.c */
/* $Id: bsd.c,v 1.35 1993/12/19 17:59:49 nils Exp $ */
  
#include <string.h>
#include <values.h>

#include "config.h"
#include "externs.h"
#include "sock.h"

#define  mklinebuf(fp)	setvbuf(fp, NULL, _IOLBF, 0)
#define _USE_BSD
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sys/errno.h>
#include "interface.h"
#include <netdb.h>
#include <ctype.h>

extern int errno;
extern long spot;
static void init_args P((int, char **));
static void init_io P((void));
static void shovechars P((int));
static void raw_notify_internal P((dbref, char *, int));
static void check_for_idlers P((void));
void check_for_disconnect_idlers P((dbref));
void check_for_connect_unidlers P((dbref));
int des_idle P((struct descriptor_data *));
const char *NullFile = "logs/null";

#define  DEF_MODE	0644


static char *connect_fail_char = "That player does not exist.\n";
static char *connect_fail_passwd = "Incorrect password.\n";

#ifndef WCREAT
static char *create_fail = "Either there is already a player with that name, or that name is illegal.\n";
#endif

static char *get_password = "Please enter password:\n\373\001";
static char *got_password = "\374\001";

struct descriptor_data *descriptor_list = 0;

static void check_connect P((struct descriptor_data *, char *));
static void parse_connect P((char *, char *, char *, char *));
static void set_userstring P((char **, char *));
static int do_command P((struct descriptor_data *, char *));
static char *strsave P((char *));
static struct text_block *make_text_block P((char *, int));
static void add_to_queue P((struct text_queue *, char *, int));
static int flush_queue P((struct text_queue *, int));
#ifdef PUEBLO_CLIENT
static int flush_queue_pueblo(struct text_queue *, int);
#endif
static int flush_queue_int(struct text_queue *, int, int);
static void process_commands P((void));
dbref make_guest P((struct descriptor_data *));
static int process_input P((struct descriptor_data *));

char *my_cb_parse(dbref, int, char *, int);
void check_for_idlers_int(dbref, char *);

int main(argc, argv)
int argc;
char *argv[];
{
  init_io_globals();
  init_args(argc, argv);
  init_io();
  printf("--------------------------------\n");
  printf("MUSE online (pid=%d)\n", getpid());

  init_attributes();
  init_mail();

  /* Need to do this first, so open_sockets can override it if needed. */
  time(&muse_up_time);
  time(&muse_reboot_time);

  open_sockets();

  if (init_game(def_db_in, def_db_out) < 0)
  {
    log_error(tprintf("Couldn't load %s!", def_db_in));
    exit_nicely(136);
  }

  set_signals();

  /* main engine call */
  shovechars(inet_port);

  log_important("|G+Shutting down normally.|");
  close_sockets();
  do_haltall(1);
  dump_database();
  free_database();
  free_mail();
  free_hash();
  if (exit_status == 1)
  {
    fcntl(sock, F_SETFD, 1);
  }
  else
  {
    close(sock);
  }

  if (sig_caught > 0)
    log_important(tprintf("Shutting down due to signal %d", sig_caught));
  if (exit_status == 1)		/* reboot */
  {
    close_logs();
    remove_temp_dbs();
#ifdef MALLOCDEBUG
    mnem_writestats();
#endif
    if (fork() == 0)
      exit(0);			/* safe exit stuff, like profiling data, *
				   stdio flushing. */

    alarm(0);  /* cancel impending SIG_ALRM */
    wait(0);   /* added 01/14/1999. dunno if this helps or not.  but when I
                  intentionally fragged the muse from @rebooting as soon as
                  the muse came up, the db survived with this here. dunno.  */
    execv(argv[0], argv);
    execv("../bin/netmuse", argv);
    execvp("netmuse", argv);
    unlink("logs/socket_table");
    _exit(exit_status);
  }
  shutdown_stack();
  exit_nicely(exit_status);
  exit(exit_status);
}

static void init_args(argc, argv)
int argc;
char *argv[];
{
  /* change default input database? */
  if (argc > 1)
    --argc, def_db_in = *++argv;

  /* change default dump database? */
  if (argc > 1)
    --argc, def_db_out = *++argv;

  /* change default log file? */
  if (argc > 1)
    --argc, stdout_logfile = *++argv;

  /* change port number? */
  if (argc > 1)
    --argc, inet_port = atoi(*++argv);
}

static void init_io()
{
#ifndef fileno
  /* sometimes it's #defined in stdio.h */
  extern int fileno P((FILE *));

#endif
  int fd;

  /* close standard input */
  fclose(stdin);

  /* open a link to the log file */
  fd = open(stdout_logfile, O_WRONLY | O_CREAT | O_APPEND, DEF_MODE);
  if (fd < 0)
  {
    perror("open()");
    log_error(tprintf("Error opening %s for writing.", stdout_logfile));
    exit_nicely(136);
  }

  /* attempt to convert standard output to logfile */
  close(fileno(stdout));
  if (dup2(fd, fileno(stdout)) == -1)
  {
    perror("dup2()");
    log_error("Error converting standard output to logfile.");
  }
  mklinebuf(stdout);

  /* attempt to convert standard error to logfile */
  close(fileno(stderr));
  if (dup2(fd, fileno(stderr)) == -1)
  {
    perror("dup2()");
    printf("Error converting standard error to logfile.\n");
  }
  mklinebuf(stderr);

  /* this logfile reference is no longer needed */
  close(fd);

  /* save a file descriptor */
/*   reserved = open("/dev/null",O_RDWR, 0); */
  reserved = open(NullFile, O_RDWR, 0);

}

char *short_name(obj)
dbref obj;
{
  int a, b;

  if (obj < 0 || obj >= db_top)
    return "?";
  a = strlen(atr_get(obj, A_ALIAS));
  b = strlen(db[obj].name);
  if (a && a < b)
    return atr_get(obj, A_ALIAS);
  else
    return (db[obj].name);
}

void raw_notify(player, msg)
dbref player;
char *msg;
{
  raw_notify_internal(player, msg, 1);
}

void raw_notify_noc(player, msg)
dbref player;
char *msg;
{
  raw_notify_internal(player, msg, 0);
}

static void raw_notify_internal(player, msg, color)
dbref player;
char *msg;
int color;
{
  struct descriptor_data *d;
  extern dbref speaker, as_from, as_to;
  char buf0[2048];
  char ansi[4096];
#ifdef PUEBLO_CLIENT
  char html[65536];
#endif
  char *temp;


  if (db[player].flags & PLAYER_WHEN)
  {
    db[player].flags &= ~PLAYER_WHEN;
    notify(player, "The WHEN flag is now obsolete. It has been removed. See \"help WHEN\" for more information.");
  }


  if (IS(player, TYPE_PLAYER, PUPPET))
  {

    if (speaker != player)
      temp = tprintf(" [#%ld/%s]", speaker, short_name(real_owner(db[speaker].owner)));
    else
      temp = tprintf("");

    strncpy(buf0, msg, (2046 - strlen(temp)));
    strcat(buf0, temp);
  }
  else
  {
    strncpy(buf0, msg, 2047);
  }
  buf0[2047]='\0';


  strncpy(ansi, add_pre_suf(player, color, msg, 0), 2047);
  ansi[2047]='\0';

#ifdef PUEBLO_CLIENT
  strncpy(html, add_pre_suf(player, color, msg, 1), 65535);
  html[65535]='\0';
#endif /* PUEBLO_CLIENT */

  if (player == as_from)
    player = as_to;
  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED && d->player == player)
    {
#ifdef USE_BLACKLIST
      if ( ((!strlen(atr_get(real_owner(d->player), A_BLACKLIST))) && (!strlen(atr_get(real_owner(player), A_BLACKLIST)))) ||
           !((could_doit(real_owner(player), real_owner(d->player), A_BLACKLIST)) && (could_doit(real_owner(d->player), real_owner(player), A_BLACKLIST))) )
#endif /* USE_BLACKLIST */
      {
#ifdef PUEBLO_CLIENT
        if (!d->pueblo)  /* ansi */
#endif
        {
          queue_string(d, ansi);
          queue_write(d, "\n", 1);
        }
#ifdef PUEBLO_CLIENT
        else   /* pueblo! */
        {
          queue_string(d, html);
          queue_write(d, "\n", 1);
        }
#endif /* PUEBLO CLIENT */
      }
    }
  }
}

char *add_pre_suf(dbref player, int color, char *msg, int pueblo)
{
  extern dbref as_from;
  char buf[65535], buf1[65535], buf2[65535], buf0[65535];
  char *prefix=buf1; 
  char *suffix=buf2;

  pronoun_substitute(buf1, player, stralloc(atr_get(player, A_PREFIX)), player);
  buf1[2047+(strlen(db[player].name))]='\0';
  prefix += strlen(db[player].name) + 1;

  pronoun_substitute(buf2, player, stralloc(atr_get(player, A_SUFFIX)), player);
  suffix += strlen(db[player].name) + 1;
  buf2[2047+(strlen(db[player].name))]='\0';

  if (!(db[player].flags & CONNECT) && player != as_from)
    return(stralloc(msg));

  strncpy(buf0, my_cb_parse(player, color, msg, pueblo), 2047);

  if (prefix && strlen(prefix))
  {
    strncpy(buf, my_cb_parse(player, color, prefix, pueblo), 2047);
    strncat(buf, " ", 2047);
    strncat(buf, buf0, 2047);
    strncpy(buf0, buf, (2047 - strlen(buf0)));
    buf0[2047]='\0';
  }

  if (suffix && strlen(suffix))
  {
    strncat(buf0, " ", (2047 - strlen(buf0)));
    strncat(buf0, my_cb_parse(player, color, suffix, pueblo), (2047 - strlen(buf0))); 

    buf0[2047]='\0';
  }

  return(stralloc(buf0));
}


char *my_cb_parse(dbref player, int color, char *string, int pueblo)
{
  char buffer[2048];
  char buffer0[65535];

  strncpy(buffer, string, 2047);
  strncpy(buffer0, string, 2047);
  if (color)
  {
    if (db[player].flags & PLAYER_NOBEEP)
    {
      if (db[player].flags & PLAYER_ANSI)
      {
        strncpy(buffer, buffer0, 2048);
	strncpy(buffer0, parse_color_nobeep(buffer, pueblo), 2047); 

      }
      else
      {
        strncpy(buffer, buffer0, 2048);
	strncpy(buffer0, strip_color_nobeep(buffer), 2047); 
      }
    }
    else
    {
      if (db[player].flags & PLAYER_ANSI)
      {
        strncpy(buffer, buffer0, 2048);
	strncpy(buffer0, parse_color(buffer, pueblo), 2047); 
      }
      else
      {
        strncpy(buffer, buffer0, 2048);
	strncpy(buffer0, strip_color(buffer), 2047); 
      }
    }

    strncpy(buffer, buffer0, 2048);
  }

  buffer[2047]='\0';
  return(stralloc(buffer));
}


static struct timeval timeval_sub(now, then)
struct timeval now;
struct timeval then;
{
  now.tv_sec -= then.tv_sec;
  now.tv_usec -= then.tv_usec;
  while (now.tv_usec < 0)
  {
    now.tv_usec += 1000000;
    now.tv_sec--;
  }
  if (now.tv_sec < 0)
    now.tv_sec = 0;		/* aack! */
  return now;
}

static int msec_diff(now, then)
struct timeval now;
struct timeval then;
{
  return ((now.tv_sec - then.tv_sec) * 1000
	  + (now.tv_usec - then.tv_usec) / 1000);
}

static struct timeval msec_add(t, x)
struct timeval t;
int x;
{
  t.tv_sec += x / 1000;
  t.tv_usec += (x % 1000) * 1000;
  if (t.tv_usec >= 1000000)
  {
    t.tv_sec += t.tv_usec / 1000000;
    t.tv_usec = t.tv_usec % 1000000;
  }
  return t;
}

static struct timeval update_quotas(last, current)
struct timeval last;
struct timeval current;
{
  int nslices;
  struct descriptor_data *d;

  nslices = msec_diff(current, last) / command_time_msec;

  if (nslices > 0)
  {
    for (d = descriptor_list; d; d = d->next)
    {
      d->quota += commands_per_time * nslices;
      if (d->quota > command_burst_size)
	d->quota = command_burst_size;
    }
  }
  return msec_add(last, nslices * command_time_msec);
}

int need_more_proc;

static void shovechars(port)
int port;
{
  fd_set input_set, output_set;
  struct timeval last_slice, current_time;
  struct timeval next_slice;
  struct timeval timeout, slice_timeout;
  struct timezone tz;
  int found;
  struct descriptor_data *d, *dnext;
  struct descriptor_data *newd;
  int avail_descriptors;

  time(&now);
  log_io(tprintf("Starting up on port %d", port));


  sock = make_socket(port);
  if (maxd <= sock)
    maxd = sock + 1;

  gettimeofday(&last_slice, &tz);

  avail_descriptors = getdtablesize() - 5;

  while (!shutdown_flag && !loading_db)
  {
    time(&now);
    load_more_db();

    FD_ZERO(&input_set);
    FD_ZERO(&output_set);
    if (ndescriptors < avail_descriptors && sock >= 0)
      FD_SET(sock, &input_set);

#ifdef USE_CID_PLAY
    for (d = descriptor_list; d; d = dnext)
    {
      dnext = d->next;
      if (d->cstatus & C_REMOTE && d->output.head)
      {
	if (!process_output(d))
	  shutdownsock(d);
	need_more_proc = 1;
      }
    }
#endif /*USE_CID_PLAY */

    for (d = descriptor_list; d; d = d->next)
      if (!(d->cstatus & C_REMOTE))
      {
	if (d->input.head)
	  timeout = slice_timeout;
	else
	  FD_SET(d->descriptor, &input_set);
	if (d->output.head && (d->state != CONNECTED || d->player > 0))
	  FD_SET(d->descriptor, &output_set);
      }

#ifdef USE_CID_PLAY
    for (d = descriptor_list; d; d = dnext)
    {
      dnext = d->next;
      if (d->cstatus & C_REMOTE)
	process_output(d);
    }
#endif /* USE_CID_PLAY */

    for (d = descriptor_list; d; d = dnext)
    {
      dnext = d->next;

      if (!(d->cstatus & C_REMOTE)
	  && (FD_ISSET(d->descriptor, &output_set)))
	if (!process_output(d))
	  shutdownsock(d);
    }
  }

  while (shutdown_flag == 0)
  {
    gettimeofday(&current_time, (struct timezone *)0);
    time(&now);
    last_slice = update_quotas(last_slice, current_time);

#ifdef RANDOM_WELCOME
    sprintf(welcome_msg_file, "msgs/welcome%03d.txt", rand() % num_welcome_msgs);
#endif

    clear_stack();
    process_commands();

    check_for_idlers();
#ifdef USE_RLPAGE
    rlpage_tick();
#endif

    if (shutdown_flag)
      break;

    /* test for events */
    dispatch();

    /* any queued robot commands waiting? */
    timeout.tv_sec = (need_more_proc || test_top())? 0 : 100;
    need_more_proc = 0;
    timeout.tv_usec = 5;
    next_slice = msec_add(last_slice, command_time_msec);
    slice_timeout = timeval_sub(next_slice, current_time);

    FD_ZERO(&input_set);
    FD_ZERO(&output_set);
    if (ndescriptors < avail_descriptors && sock >= 0)
      FD_SET(sock, &input_set);

#ifdef USE_CID_PLAY 
    for (d = descriptor_list; d; d = dnext)
    {
      dnext = d->next;
      if (d->cstatus & C_REMOTE && d->output.head)
      {
	if (!process_output(d))
	  shutdownsock(d);
	need_more_proc = 1;
      }
    }
#endif /* USE_CID_PLAY */

    for (d = descriptor_list; d; d = d->next)
      if (!(d->cstatus & C_REMOTE))
      {
	if (d->input.head)
	  timeout = slice_timeout;
	else
	  FD_SET(d->descriptor, &input_set);
	if (d->output.head && (d->state != CONNECTED || d->player > 0))
	  FD_SET(d->descriptor, &output_set);
      }

    if ((found = select(maxd, &input_set, &output_set,
			(fd_set *) 0, &timeout)) < 0)
    {
      if (errno != EINTR)
      {
	perror("select");
	/* return; *//* naw.. stay up. */
      }
    }
    else
    {
      /* if !found then time for robot commands */
      time(&now);
      if (loading_db && !found)
      {
	if (do_top() && do_top())
	  do_top();
	continue;
      }
      (void)time(&now);
      if (sock >= 0 && FD_ISSET(sock, &input_set))
      {
	if (!(newd = new_connection(sock)))
	{
	  if (errno
	      && errno != EINTR
	      && errno != EMFILE
	      && errno != ENFILE)
	  {
	    perror("new_connection");
	    /* return; *//* naw.. stay up. */
	  }
	}
	else
	{
	  if (newd->descriptor >= maxd)
	    maxd = newd->descriptor + 1;
	}
      }
      for (d = descriptor_list; d; d = dnext)
      {
	dnext = d->next;
	if (FD_ISSET(d->descriptor, &input_set) && !(d->cstatus & C_REMOTE))
	  if (!process_input(d))
	    shutdownsock(d);
      }
#ifdef USE_CID_PLAY 
      for (d = descriptor_list; d; d = dnext)
      {
	dnext = d->next;
	if (d->cstatus & C_REMOTE)
	  process_output(d);
      }
#endif /* USE_CID_PLAY */
      for (d = descriptor_list; d; d = dnext)
      {
	dnext = d->next;

	if (!(d->cstatus & C_REMOTE)
	    && (FD_ISSET(d->descriptor, &output_set)))
	  if (!process_output(d))
	    shutdownsock(d);
      }
#ifdef USE_CID_PLAY 
      for (d = descriptor_list; d; d = dnext)
      {
	dnext = d->next;
	if (d->cstatus & C_REMOTE &&
	    !d->parent)
	  shutdownsock(d);
      }
#endif /* USE_CID_PLAY */
    }
  }
}

void outgoing_setupfd(player, fd)
dbref player;
int fd;
{
  struct descriptor_data *d;

  ndescriptors++;
  MALLOC(d, struct descriptor_data, 1);

  d->descriptor = fd;
  d->concid = make_concid();
  d->cstatus = 0;
  d->parent = 0;
  d->state = CONNECTED;
  make_nonblocking(fd);
  d->player = -player;
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
  d->last_time = 0;
  /* d->address=inet_addr("127.0.0.1"); * localhost. cuz. */
  strcpy(d->addr, "RWHO");
  if (descriptor_list)
    descriptor_list->prev = &d->next;
  d->next = descriptor_list;
  d->prev = &descriptor_list;
  descriptor_list = d;
  if (fd >= maxd)
    maxd = fd + 1;
}


size_t text_block_size = 0;
size_t text_block_num  = 0;

static struct text_block *make_text_block(s, n)
char *s;
int n;
{
  struct text_block *p;

  MALLOC(p, struct text_block, 1);
  MALLOC(p->buf, char, n);

/*  bcopy(s, p->buf, n); * memcpy is preferred */
  memcpy(p->buf, s, n);
  p->nchars = n;
  p->start = p->buf;
  p->nxt = 0;
  text_block_size += n;
  text_block_num++;
  return p;
}

void free_text_block(t)
struct text_block *t;
{
  FREE(t->buf);
  text_block_size -= t->nchars;
  FREE((char *)t);
  text_block_num--;
}

static void add_to_queue(q, b, n)
struct text_queue *q;
char *b;
int n;
{
  struct text_block *p;

  if (n == 0)
    return;

  p = make_text_block(b, n);
  p->nxt = 0;
  *q->tail = p;
  q->tail = &p->nxt;
}

static int flush_queue(struct text_queue *q, int n)
{
  return (flush_queue_int(q, n, 0));
}

#ifdef PUEBLO_CLIENT
static int flush_queue_pueblo(struct text_queue *q, int n)
{
  return (flush_queue_int(q, n, 1));
}
#endif

static int flush_queue_int(struct text_queue *q, int n, int pueb)
{
  struct text_block *p;
  int really_flushed = 0;

/* need to make the game flush html a bit better here. make sure we're not flushing in the middle of a metatag and if so we need o truncate the tag and go one with life. */

  n += strlen(FLUSHED_MESSAGE);

  while (n > 0 && (p = q->head))
  {
    n -= p->nchars;
    really_flushed += p->nchars;
    q->head = p->nxt;
    free_text_block(p);
  }
  p = make_text_block(FLUSHED_MESSAGE, strlen(FLUSHED_MESSAGE));
  p->nxt = q->head;
  q->head = p;
  if (!p->nxt)
    q->tail = &p->nxt;
  really_flushed -= p->nchars;
  return really_flushed;
}

int queue_write(d, b, n)
struct descriptor_data *d;
char *b;
int n;
{
  int space;

#ifdef USE_CID_PLAY
  if (d->cstatus & C_REMOTE)
    need_more_proc = 1;
#endif /* USE_CID_PLAY */
#ifdef PUEBLO_CLIENT
  if(d->pueblo == 0)
#endif /* PUEBLO_CLIENT */
  {
    space = max_output - d->output_size - n;
    if (space < 0)
      d->output_size -= flush_queue(&d->output, -space);
  }
#ifdef PUEBLO_CLIENT
  else
  {
    space = max_output_pueblo - d->output_size - n;
    if (space < 0)
      d->output_size -= flush_queue_pueblo(&d->output, -space);
  }
#endif /* PUEBLO_CLIENT */
  add_to_queue(&d->output, b, n);
  d->output_size += n;
  return n;
}

int queue_string(d, s)
struct descriptor_data *d;
char *s;
{
  return queue_write(d, s, strlen(s));
}

int process_output(d)
struct descriptor_data *d;
{
  struct text_block **qp, *cur;
  int cnt;

#ifdef USE_CID_PLAY
  if (d->cstatus & C_REMOTE)
  {
    char buf[10];
    char obuf[2048];
    int buflen;
    int k, j;

    sprintf(buf, "%d ", d->concid);
    buflen = strlen(buf);

/*     bcopy(buf, obuf, buflen); */
    memcpy(obuf, buf, buflen);
    j = buflen;

    for (qp = &d->output.head; (cur = *qp);)
    {
      need_more_proc = 1;
      for (k = 0; k < cur->nchars; k++)
      {
	obuf[j++] = cur->start[k];
	if (cur->start[k] == '\n')
	{
	  if (d->parent)
	    queue_write(d->parent, obuf, j);
/*	  bcopy(buf, obuf, buflen); */
	  memcpy(obuf, buf, buflen);
	  j = buflen;
	}
      }
      d->output_size -= cur->nchars;
      if (!cur->nxt)
	d->output.tail = qp;
      *qp = cur->nxt;
      free_text_block(cur);
    }
    if (j > buflen)
      queue_write(d, obuf + buflen, j - buflen);
    return 1;
  }
  else
  {
#endif /* USE_CID_PLAY */
    for (qp = &d->output.head; (cur = *qp);)
    {
      cnt = write(d->descriptor, cur->start, cur->nchars);
      if (cnt < 0)
      {
	if (errno == EWOULDBLOCK)
	  return 1;
	return 0;
      }
      d->output_size -= cnt;
      if (cnt == cur->nchars)
      {
	if (!cur->nxt)
	  d->output.tail = qp;
	*qp = cur->nxt;
	free_text_block(cur);
	continue;		/* do not adv ptr */
      }
      cur->nchars -= cnt;
      cur->start += cnt;
      break;
    }
#ifdef USE_CID_PLAY
  }
#endif /* USE_CID_PLAY */
  return 1;
}


void welcome_user(d)
struct descriptor_data *d;
{
#ifdef PUEBLO_CLIENT
  queue_string(d, "This world is Pueblo 1.0 Enhanced\n");
#endif
  connect_message(d, welcome_msg_file, 0);
}

void connect_message(d, filename, direct)
struct descriptor_data *d;
char *filename;
int direct;
{
  int n, fd;
  char buf[MAX_BUFF_LEN];

  close(reserved);

  if ((fd = open(filename, O_RDONLY, 0)) != -1)
  {
    while ((n = read(fd, buf, 512)) > 0)
      queue_write(d, buf, n);
    close(fd);
    queue_write(d, "\n", 1);
  }
/*   reserved=open("/dev/null", O_RDWR, 0); */
  reserved = open(NullFile, O_RDWR, 0);

  if (direct)
  {
    process_output(d);
  }
}

static char *strsave(s)
char *s;
{
  static char *p;

  MALLOC(p, char, strlen(s) + 1);

  if (p)
    strcpy(p, s);
  return p;
}

static void save_command(d, command)
struct descriptor_data *d;
char *command;
{
  add_to_queue(&d->input, command, strlen(command) + 1);
}

static int process_input(d)
struct descriptor_data *d;
{
  char buf[1024];
  int got;
  char *p, *pend, *q, *qend;

  got = read(d->descriptor, buf, sizeof buf);
  if (got <= 0)
    return 0;
  if (!d->raw_input)
  {
    MALLOC(d->raw_input, char, MAX_COMMAND_LEN);

    d->raw_input_at = d->raw_input;
  }
  p = d->raw_input_at;
  pend = d->raw_input + MAX_COMMAND_LEN - 1;
  for (q = buf, qend = buf + got; q < qend; q++)
  {
    if (*q == '\n')
    {
      *p = '\0';
      if (p >= d->raw_input)
	save_command(d, d->raw_input);
      p = d->raw_input;
    }
    else if (p < pend && isascii(*q) && isprint(*q))
    {
      *p++ = *q;
    }
  }
  if (p > d->raw_input)
  {
    d->raw_input_at = p;
  }
  else
  {
    FREE(d->raw_input);
    d->raw_input = 0;
    d->raw_input_at = 0;
  }
  return 1;
}

static void set_userstring(userstring, command)
char **userstring;
char *command;
{
  if (*userstring)
  {
    FREE(*userstring);
    *userstring = 0;
  }
  while (*command && isascii(*command) && isspace(*command))
    command++;
  if (*command)
    *userstring = strsave(command);
}

/*
static void process_commands()
{
  int nprocessed;
  struct descriptor_data *d, *dnext;
  struct text_block *t;

  do
  {
    nprocessed = 0;
    for (d = descriptor_list; d; d = dnext)
    {
      dnext = d->next;
      if (d->quota > 0 && (t = d->input.head))
      {
	nprocessed++;
	if (!do_command(d, t->start))
	{
	  connect_message(d, leave_msg_file, 1);
	  shutdownsock(d);
	}
	else
	{
	  d->input.head = t->nxt;
	  if (!d->input.head)
	    d->input.tail = &d->input.head;
	  free_text_block(t);
	}
      }
    }
  }
  while (nprocessed > 0);
}
*/

static void process_commands()
{
  int nprocessed;
  struct descriptor_data *d, *dnext /* *sd */;
  struct text_block *t;
  char buf[2048];

  do 
  {
    nprocessed = 0;
    for (d = descriptor_list; d; d = dnext) 
    {
      dnext = d->next;
      if (d -> quota > 0 && (t = d -> input.head)) 
      {
        nprocessed++;
        strcpy(buf, t->start);
        d -> input.head = t -> nxt;
        if (!d -> input.head)
          d -> input.tail = &d -> input.head;
        free_text_block (t);
        if (!do_command(d, buf)) 
        {
          connect_message(d,leave_msg_file,1);
          shutdownsock(d);
        }
      }
    }
  } while (nprocessed > 0);
  clear_stack();
}


static int do_command(d, command)
struct descriptor_data *d;
char *command;
{
#ifdef CR_UNIDLE
  if (!(command && *command))
    return 1;
#endif
 
  if((d->state == CONNECTED) && (db[d->player].flags & PLAYER_IDLE))
    set_unidle(d->player,d->last_time);

  d->last_time = now;
  d->quota--;
  depth = 2;
  
  if (!(command && *command) && !(d->player < 0 && d->state == CONNECTED))
    return 1;

#ifdef WHO_BY_IDLE
  if (d->state == CONNECTED && d->player > 0)
  {
    /* pop player to top of who list. */
    *d->prev = d->next;
    if (d->next)
      d->next->prev = d->prev;

    d->next = descriptor_list;
    if (descriptor_list)
      descriptor_list->prev = &d->next;
    descriptor_list = d;
    d->prev = &descriptor_list;
  }
#endif
  if (!strcmp(command, QUIT_COMMAND))
  {
    return 0;
  }
#ifdef USE_CID_PLAY
  else if (!strncmp(command, "I wanna be a concentrator... my password is ",
		    sizeof("I wanna be a concentrator... my password is ")
		    - 1))
  {
    do_becomeconc(d, command + sizeof("I wanna be a concentrator... my password is ") - 1);
  }
#endif /* USE_CID_PLAY */
  else if (!strncmp(command, PREFIX_COMMAND, strlen(PREFIX_COMMAND)))
  {
    set_userstring(&d->output_prefix, command + strlen(PREFIX_COMMAND));
  }
  else if (!strncmp(command, SUFFIX_COMMAND, strlen(SUFFIX_COMMAND)))
  {
    set_userstring(&d->output_suffix, command + strlen(SUFFIX_COMMAND));
  }
  else
#ifdef USE_CID_PLAY
  if (d->cstatus & C_CCONTROL)
  {
    if (!strcmp(command, "Gimmie a new concid"))
      do_makeid(d);
    else if (!strncmp(command, "I wanna connect concid ",
		      sizeof("I wanna connect concid ") - 1))
    {
      char *m, *n;

      m = command + sizeof("I wanna connect concid ") - 1;
      n = strchr(m, ' ');
      if (!n)
	queue_string(d, "Usage: I wanna connect concid <id> <hostname>\n");
      else
	do_connectid(d, atoi
		     (command +
		      sizeof("I wanna connect concid ") - 1), n);
    }
    else if (!strncmp(command, "I wanna kill concid ",
		      sizeof("I wanna kill concid ") - 1))
      do_killid(d,
		atoi(command +
		     sizeof("I wanna kill concid ") - 1));
    else
    {
      char *k;

      k = strchr(command, ' ');
      if (!k)
	queue_string(d, "Huh???\r\n");
      else
      {
	struct descriptor_data *l;
	int j;

	*k = '\0';
	j = atoi(command);
	for (l = descriptor_list; l; l = l->next)
	{
	  if (l->concid == j)
	    break;
	}
	if (!l)
	  queue_string(d, "I don't know that concid.\r\n");
	else
	{
	  k++;
	  if (!do_command(l, k))
	  {
	    connect_message(l, leave_msg_file, 1);
	    shutdownsock(l);
	  }
	}
      }
    }
  }
  else
#endif
  {
    if (d->state == CONNECTED)
    {
      if (d->output_prefix)
      {
	queue_string(d, d->output_prefix);
	queue_write(d, "\n", 1);
      }
      cplr = d->player;
      if (d->player > 0)
      {
	strcpy(ccom, command);
	  process_command(d->player, command, NOTHING);
      }
      else
      {
	log_error(tprintf("|R+ERROR| Negative d->player %ld trying to execute %s!", d->player, command));
	notify(-d->player, command);
      }
      if (d->output_suffix)
      {
	queue_string(d, d->output_suffix);
	queue_write(d, "\n", 1);
      }
    }
    else
    {
      d->pueblo--;
      check_connect(d, command);
    }
  }
  return 1;
}

static void check_connect(d, msg)
struct descriptor_data *d;
char *msg;
{
  char *m;
  char command[MAX_COMMAND_LEN];
  char user[MAX_COMMAND_LEN];
  char password[MAX_COMMAND_LEN];
  dbref player, p;
  int num = 0;

  if (d->state == WAITPASS)
  {
    /* msg contains the password. */
    char *foobuf = stack_em(MAX_COMMAND_LEN * 3);

    sprintf(foobuf, "connect %s %s", d->charname, msg);
    free(d->charname);
    queue_string(d, got_password);
    d->state = WAITCONNECT;
    msg = foobuf;
  }

  parse_connect(msg, command, user, password);

  if (!strcmp(command, "WHO"))
    dump_users(0, "", "", d);
  else if (!strncmp(command, "co", 2))
  {
    if (string_prefix(user, guest_prefix) || string_prefix(user, "guest"))
    {
      strcpy(password, guest_prefix);
      if (check_lockout(d, guest_lockout_file, guest_msg_file))
	player = NOTHING;
      else
      {
	if ((p = make_guest(d)) == NOTHING)
	  return;
	player = p;
      }
    }
    else
      player = connect_player(user, password);

    if (player > NOTHING && Typeof(player) == TYPE_PLAYER)
    {
      if (*db[player].pows < restrict_connect_class)
      {
	log_io(tprintf("%s refused connection due to class restriction.",
		       unparse_object(root, player)));
	write(d->descriptor, tprintf("%s %s", muse_name, LOCKOUT_MESSAGE), (strlen(LOCKOUT_MESSAGE) + strlen(muse_name) + 1));
	process_output(d);
	d->state = CONNECTED;
	d->connected_at = time(0);
	d->player = player;
	shutdownsock(d);
	return;
      }
    }

    if (player == NOTHING && !*password)
    {
      /* hmm. bet they want to type it in seperately. */
      queue_string(d, get_password);
      d->state = WAITPASS;
      d->charname = malloc(strlen(user) + 1);
      strcpy(d->charname, user);
      return;
    }
    else if (player == NOTHING)
    {
      queue_string(d, connect_fail_char);
      log_io(tprintf("|Y!+FAILED CONNECT| %s on concid %ld",
		     user, d->concid));
    }
    else if (player == PASSWORD)
    {
      queue_string(d, connect_fail_passwd);
      log_io(tprintf("|Y!+FAILED CONNECT| %s on concid %ld",
		     user, d->concid));
    }
    else
    {
      time_t tt;
      char *ct;

      tt = now;
      ct = ctime(&tt);
      if (ct && *ct) 
        ct[strlen(ct) - 1] = '\0';

      log_io(tprintf("|G+CONNECTED| %s on concid %ld",
		     unparse_object_a(player, player), d->concid));
      com_send_as_hidden("pub_io", 
          tprintf("|G+CONNECTED| %s - %s", unparse_object_a(player, player), ct), player);

      add_login(player);


      if (d->state == WAITPASS)
	queue_string(d, got_password);
      d->state = CONNECTED;
      d->connected_at = now;
      d->player = player;
      /* give players a message on connection */
      connect_message(d, motd_msg_file, 0);
      announce_connect(player);

      m = atr_get(player, A_LASTSITE);
      while (*m)
      {
	while (*m && isspace(*m))
	  m++;
	if (*m)
	  num++;
	while (*m && !isspace(*m))
	  m++;
      }

      if (num >= 10)
      {
	m = atr_get(player, A_LASTSITE);
	num = 0;
	while (*m && isspace(*m))
	  m++;
	while (*m && !isspace(*m))
	  m++;
	atr_add(player, A_LASTSITE, tprintf("%s %s@%s", m, d->user, d->addr));
      }
      else
      {
	m = atr_get(player, A_LASTSITE);
	while (*m && isspace(*m))
	  m++;
	atr_add(player, A_LASTSITE, tprintf("%s %s@%s", m, d->user, d->addr));
      }

      do_look_around(player);
      /* XXX might want to add check for haven here. */

      if (Guest(player))
	notify(player, tprintf(
				"Welcome to %s; your name is %s",
				muse_name, db[player].cname));
    }
  }
  else if (!strncmp(command, "cr", 2))
  {
    if (!allow_create)
    {
      connect_message(d, register_msg_file, 0);
    }
    else
    {
      player = create_player(user, password, CLASS_VISITOR, player_start);
      if (player == NOTHING)
      {
	queue_string(d, create_fail);
	log_io(tprintf("FAILED CREATE %s on concid %ld",
		       user, d->concid));
      }
      else
      {
	log_io(tprintf("CREATED %s(%ld) on concid %ld",
		       db[player].name, player, d->concid));
	d->state = CONNECTED;
	d->connected_at = now;
	d->player = player;
	/* give new players a special message */
	connect_message(d, create_msg_file, 0);
	/* connect_message(d, connect_msg_file); */
	announce_connect(player);
	do_look_around(player);
      }
    }
  }
#ifdef PUEBLO_CLIENT
  else if (!strncmp(command, "PUEBLOCLIENT", 12))
  {
    d->pueblo = 2;
  }
#endif
  else
  {
    if (d->pueblo == 0)
    {
      check_lockout(d, welcome_lockout_file, welcome_msg_file);
    }
  }
  if (d->state == WAITPASS)
  {
    d->state = WAITCONNECT;
    queue_string(d, got_password);
  }
}

static void parse_connect(msg, command, user, pass)
char *msg;
char *command;
char *user;
char *pass;
{
  char *p;

  while (*msg && isascii(*msg) && isspace(*msg))
    msg++;
  p = command;
  while (*msg && isascii(*msg) && !isspace(*msg))
    *p++ = *msg++;
  *p = '\0';
  while (*msg && isascii(*msg) && isspace(*msg))
    msg++;
  p = user;
  while (*msg && isascii(*msg) && !isspace(*msg))
    *p++ = *msg++;
  *p = '\0';
  while (*msg && isascii(*msg) && isspace(*msg))
    msg++;
  p = pass;
  while (*msg && isascii(*msg) && !isspace(*msg))
    *p++ = *msg++;
  *p = '\0';
}

/* this algorithm can be changed later to accomodate an unlimited
   number of guests.  currently, it supports a limited number.    */
dbref make_guest(d)
struct descriptor_data *d;
{
  int i;
  dbref player;
  char *name;
  char *alias;

  for (i = 1; i < number_guests; i++)
  {
    name = tprintf("%s%d", guest_prefix, i);
    alias = tprintf("%s%d", guest_alias_prefix, i);
    if (lookup_player(name) == NOTHING)
      break;
  }
  if (i == number_guests)
  {
    queue_string(d, "All guest ID's are busy; please try again later.\n");
    return NOTHING;
  }

  /* This will work for now, need something better for final release. */
  player = create_guest(name, alias, tprintf("lA\tDSGt\twjh24t"));

  if (player == NOTHING)
  {
    queue_string(d, "Error creating guest ID, please try again later.\n");
    log_error(tprintf("Error creating guest ID.  '%s' already exists.",
		      name));
    return NOTHING;
  }

  return player;
}


void emergency_shutdown()
{
  log_error("Emergency shutdown.");
  shutdown_flag = 1;
  exit_status = 136;
  close_sockets();
}

int boot_off(player)
dbref player;
{
  struct descriptor_data *d;

  for (d = descriptor_list; d; d = d->next)
  {
    if (d->state == CONNECTED && d->player == player)
    {
      process_output(d);
      shutdownsock(d);
      return 1;
    }
  }
  return 0;
}

void announce_connect(player)
dbref player;
{
  dbref loc;
  char buf[BUFFER_LEN];
  extern dbref speaker;
  char *s, t[30];		/* we put this here instead of in

				   connect_player cuz we gotta */
  time_t tt;			/* notify the player and the descriptor isn't 

				   set up yet then */
  int connect_again = db[player].flags & CONNECT;

  if ((loc = getloc(player)) == NOTHING)
    return;
      


  if (connect_again)
  {
    check_for_connect_unidlers(player);
    sprintf(buf, "%s has reconnected.", db[player].cname);
  }
  else
    sprintf(buf, "%s has connected.", db[player].cname);

  /* if the player logs in, set them unidle. */
  db[player].flags &= ~PLAYER_IDLE;

  /* added to allow player's inventory to hear a player connect */
  speaker = player;
  notify_in(player, player, buf);
  if (!IS(loc, TYPE_ROOM, ROOM_AUDITORIUM))
    notify_in(loc, player, buf);
  db[player].flags |= CONNECT;
  if (Typeof(player) == TYPE_PLAYER)
    db[player].flags &= ~HAVEN;
  if (!Guest(player))
  {
    tt = now;
    strcpy(t, ctime(&tt));
    t[strlen(t) - 1] = 0;
    tt = atol(atr_get(player, A_LASTDISC));
    if (tt == 0L)
      s = FIRST_LOGIN;
    else
    {
      s = ctime(&tt);
      s[strlen(s) - 1] = 0;
      if (((strncmp(t, s, 10) != 0) &&
	  power(player, POW_MEMBER) && db[player].owner == player)
          && !connect_again )
      {
	giveto(player, allowance);
	notify(player, tprintf("You collect %d credits.", allowance));
      }
    }
    notify(player, tprintf("Last login: %s", s));
    tt = now;
    sprintf(buf, "%ld", tt);
    atr_add(player, A_LASTCONN, buf);
    check_mail(player, db[player].name);

  }
  if (!connect_again)
  {
    dbref thing;
    dbref zone;
    dbref who = player;
    int depth;
    int find = 0;

    did_it(who, who, NULL, NULL, A_OCONN, NULL, A_ACONN);
    did_it(who, db[who].location, NULL, NULL, NULL, NULL, A_ACONN);

    zone = db[0].zone;

    if (Typeof(db[who].location) == TYPE_ROOM)
    {
      zone = db[db[who].location].zone;
    }
    else
    {
      thing = db[who].location;
      for (depth = 10; depth && (find != 1); depth--, thing = db[thing].location)
	if (Typeof(thing) == TYPE_ROOM)
	{
	  zone = db[thing].zone;
	  find = 1;
	}
    }

    if ((db[0].zone != zone) && (Typeof(db[0].zone) != TYPE_PLAYER))
      did_it(who, db[0].zone, NULL, NULL, NULL, NULL, A_ACONN);

    if (Typeof(zone) != TYPE_PLAYER)
      did_it(who, zone, NULL, NULL, NULL, NULL, A_ACONN);

    if ((thing = db[who].contents) != NOTHING)
    {
      DOLIST(thing, thing)
      {
	if (Typeof(thing) != TYPE_PLAYER)
	{
	  did_it(who, thing, NULL, NULL, NULL, NULL, A_ACONN);
	}
      }
    }
    if ((thing = db[db[who].location].contents) != NOTHING)
    {
      DOLIST(thing, thing)
      {
	if (Typeof(thing) != TYPE_PLAYER)
	{
	  did_it(who, thing, NULL, NULL, NULL, NULL, A_ACONN);
	}
      }
    }
  }
}

void announce_disconnect(player)
dbref player;
{
  dbref loc;
  int num;
  char buf[BUFFER_LEN];
  struct descriptor_data *d;
  extern dbref speaker;
  int partial_disconnect;

  if (is_pasting(player))
    remove_paste(player);

  if (player < 0)
    return;

  for (num = 0, d = descriptor_list; d; d = d->next)
    if (d->state == CONNECTED && (d->player > 0) && (d->player == player))
      num++;
  if (num < 2 && !shutdown_flag)
  {
    db[player].flags &= ~CONNECT;
    atr_add(player, A_IT, "");
    partial_disconnect = 0;
  }
  else
    partial_disconnect = 1;

  time(&now);

  atr_add(player, A_LASTDISC, tprintf("%ld", now));
  atr_add(player, A_PREVTIME, tprintf("%ld",
				      atol(atr_get(player, A_PREVTIME)) + now - atol(atr_get(player, A_LASTCONN))));

  if ((loc = getloc(player)) != NOTHING)
  {
    if (partial_disconnect)
    {
      sprintf(buf, "%s has partially disconnected.", db[player].cname);
      check_for_disconnect_idlers(player);
    }
    else
    {
      sprintf(buf, "%s has disconnected.", db[player].cname);
    }
    speaker = player;
    /* added to allow player's inventory to hear a player connect */
    notify_in(player, player, buf);
    if (!IS(loc, TYPE_ROOM, ROOM_AUDITORIUM))
      notify_in(loc, player, buf);
    if (!partial_disconnect)
    {
      dbref zone;
      dbref thing;
      dbref who = player;
      int depth;
      int find = 0;

      did_it(who, who, NULL, NULL, A_ODISC, NULL, A_ADISC);
      did_it(who, db[who].location, NULL, NULL, NULL, NULL, A_ADISC);

      zone = db[0].zone;

      if (Typeof(db[who].location) == TYPE_ROOM)
      {
	zone = db[db[who].location].zone;
      }
      else
      {
	thing = db[who].location;
	for (depth = 10; depth && (find != 1); depth--, thing = db[thing].location)
	  if (Typeof(thing) == TYPE_ROOM)
	  {
	    zone = db[thing].zone;
	    find = 1;
	  }
      }

      if ((db[0].zone != zone) && (Typeof(db[0].zone) != TYPE_PLAYER))
	did_it(who, db[0].zone, NULL, NULL, NULL, NULL, A_ADISC);

      if (Typeof(zone) != TYPE_PLAYER)
	did_it(who, zone, NULL, NULL, NULL, NULL, A_ADISC);

      if ((thing = db[who].contents) != NOTHING)
      {
	DOLIST(thing, thing)
	{
	  if (Typeof(thing) != TYPE_PLAYER)
	  {
	    did_it(who, thing, NULL, NULL, NULL, NULL, A_ADISC);
	  }
	}
      }
      if ((thing = db[db[who].location].contents) != NOTHING)
      {
	DOLIST(thing, thing)
	{
	  if (Typeof(thing) != TYPE_PLAYER)
	  {
	    did_it(who, thing, NULL, NULL, NULL, NULL, A_ADISC);
	  }
	}
      }
    }
  }
}

#if defined(HPUX) || defined(SYSV)
#include <unistd.h>
int getdtablesize()
{
  return (int)sysconf(_SC_OPEN_MAX);
}
#endif

#ifdef SYSV
void setlinebuf(x)
FILE *x;
{
  setbuf(x, NULL);
}
int vfork()
{
  return fork();
}
#endif
struct ctrace_int
{
  struct descriptor_data *des;
  struct ctrace_int **children;
};

static struct ctrace_int *internal_ctrace(parent)
struct descriptor_data *parent;
{
  struct descriptor_data *k;
  struct ctrace_int *op;
  int nchild;

  op = stack_em(sizeof(struct ctrace_int));

  op->des = parent;
  if (parent && !(parent->cstatus & C_CCONTROL))
  {
    op->children = stack_em(sizeof(struct ctrace_int *));

    op->children[0] = 0;
  }
  else
  {
    for (nchild = 0, k = descriptor_list; k; k = k->next)
      if (k->parent == parent)
	nchild++;
    op->children = stack_em(sizeof(struct ctrace_int *) * (nchild + 1));

    for (nchild = 0, k = descriptor_list; k; k = k->next)
      if (k->parent == parent)
      {
	op->children[nchild] = internal_ctrace(k);
	nchild++;
      }
    op->children[nchild] = 0;
  }
  return op;
}

static void ctrace_notify_internal(player, d, dep)
dbref player;
struct ctrace_int *d;
int dep;
{
  char buf[2000];
  int j, k;

  for (j = 0; j < dep; j++)
    buf[j] = '.';
  if (d->des && dep)
  {
    sprintf(buf + j, "%s descriptor: %d, concid: %ld, host: %s@%s",
	    (d->des->state == CONNECTED)
	    ? (tprintf("\"%s\"", unparse_object(player, d->des->player)))
	    : ((d->des->cstatus & C_CCONTROL)
	       ? "<Concentrator Control>"
	       : "<Unconnected>"),
	    d->des->descriptor, d->des->concid, d->des->user,
	    d->des->addr);
    notify(player, buf);
  }
  for (k = 0; d->children[k]; k++)
    ctrace_notify_internal(player, d->children[k], dep + 1);
}

void do_ctrace(player)
dbref player;
{
  struct ctrace_int *dscs;

  if (!power(player, POW_WHO))
  {
    notify(player, perm_denied());
    return;
  }

  dscs = internal_ctrace(0);
  ctrace_notify_internal(player, dscs, 0);
}


void check_for_idlers(void)
{
  check_for_idlers_int((-1), NULL);
}


void check_for_idlers_int(dbref player, char *msg)
{
  struct descriptor_data *d;
  int mybreak = 0;

  mybreak = 0;


  for (d = descriptor_list; d && (mybreak < 50) ; d = d->next) 
  {
    mybreak++;
/*    if (!(db[d->player].flags & PLAYER_IDLE) && d->last_time > 0 && d->player > 0 && d->state == CONNECTED) */
    if (d->last_time > 0 && d->player > 0 && d->state == CONNECTED)
    {
      if( 
          (
            !(db[d->player].flags & PLAYER_IDLE) &&
            ( 
              ( (now - d->last_time) > MAX_IDLE) ||
              ( ( (now - d->last_time) > MIN_IDLE) && ( MIN_IDLE > atol(atr_get(d->player,A_IDLETIME))) ) || 
              ( MIN_IDLE < atol(atr_get(d->player,A_IDLETIME)) && ((now - d->last_time) > atol(atr_get(d->player,A_IDLETIME)))) 
            )
          ) &&
          (
         
            ( player=-1 ) ||
            ( player=d->player )
          )
        )
      {
        struct descriptor_data *e;
        int num;
        int conn;
        long last;

        last = now - d->last_time;

        conn = 0;
        num = 0;

       /* check for multiple connections. ugh! */
       /* this could get messy on older boxes when there are lots of people connected */
       /* things would be easier if connections were limited to one per character     */
        for (num = 0, e = descriptor_list; e; e = e->next)
        {
/*          if ( e->state == CONNECTED && (e->player > 0) ) */
          if ( e->state == CONNECTED && (e->player == d->player) )
          {
            conn++;
            /* if this connection is over it's idle limit, let's keep track of it. */
            if ( 
                 ( (now - e->last_time) > MAX_IDLE) ||
                 ( ((now - e->last_time) > MIN_IDLE) && ( MIN_IDLE > atol(atr_get(e->player,A_IDLETIME))) )  ||
                 ( MIN_IDLE < atol(atr_get(e->player,A_IDLETIME)) && ((now - e->last_time) > atol(atr_get(e->player,A_IDLETIME)))) 
               ) 
            {
              num++;
              /* make sure we use the time from the least idle connection for the message! */
              if ((now - e->last_time) < last)
                last = now - e->last_time;
            }
          }
        }

        if (num == conn)
        {
          set_idle(d->player,-1,(time_t)(last/60),msg);
        }
      }
    }
  }
  return;
}


void check_for_disconnect_idlers(player)
dbref player;
{

  char msg[4096];

  strcpy(msg, atr_get(player,A_IDLE_CUR));
  if(strlen(atr_get(player,A_IDLE_CUR)))
    strcat(msg," - ");
  strcat(msg,"disconnect re-idle");
  if (strlen(msg) > 512)
    msg[512] = '\0';

  check_for_idlers_int(player, msg);
}



void check_for_connect_unidlers(player)
dbref player;
{
  struct descriptor_data *d;
  int conn, brk;

  if (db[player].flags & PLAYER_IDLE)
  {
    conn = 0;

    for (brk = 0, d = descriptor_list;!brk && d; d = d->next)
    {
      if ( (d->state == CONNECTED) && (d->player == player) )
      {
        conn++;
      }

    }
    if (conn > 1)
    {
      log_io(tprintf("%s unidled due to reconnect.",db[player].cname));
      com_send_as_hidden("pub_io", tprintf("%s unidled due to reconnect.",db[player].cname), player);
      set_unidle(player,MAXINT);
      brk = 1;
    }
  }
  return;
}

void flush_all_output ()
{  
   struct descriptor_data *d;

   for ( d = descriptor_list; d; d = d->next )
     process_output ( d );
}

int des_idle (struct descriptor_data *d)
{
  long realidle;

  realidle = atol(atr_get(d->player, A_IDLETIME));
  if (realidle > MAX_IDLE)
    realidle = MAX_IDLE;
  if (realidle < MIN_IDLE)
    realidle = MIN_IDLE;


  if ( realidle <= (now - d->last_time) )
    return 1;

  return 0;
}
