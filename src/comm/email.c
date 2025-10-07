/* email.c - Modern SMTP email functionality using libcurl
 * Simplified and secure email sending for TinyMUSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include "db.h"
#include "externs.h"
#include "config.h"

/* Configuration - these should probably go in config.h */
#ifndef SMTP_SERVER
#define SMTP_SERVER "smtp.gmail.com"  /* Your SMTP server */
#endif

#ifndef SMTP_PORT
#define SMTP_PORT 587  /* 587 for STARTTLS, 465 for SMTPS, 25 for plain */
#endif

#ifndef SMTP_USE_SSL
#define SMTP_USE_SSL 1  /* Use STARTTLS */
#endif

#ifndef SMTP_USERNAME
#define SMTP_USERNAME "your-game@gmail.com"  /* SMTP username */
#endif

#ifndef SMTP_PASSWORD
#define SMTP_PASSWORD "your-app-password"  /* SMTP password or app password */
#endif

#ifndef SMTP_FROM
#define SMTP_FROM "noreply@yourmud.com"  /* From address */
#endif

/* Buffer sizes */
#define EMAIL_BUFFER_SIZE 8192
#define ADDRESS_BUFFER_SIZE 256

/* Email payload structure for libcurl */
struct email_data {
    char *payload;
    size_t bytes_read;
};

/* Function prototypes */
static size_t payload_reader(void *ptr, size_t size, size_t nmemb, void *userp);
static char *build_email_payload(const char *from, const char *to, 
                                 const char *subject, const char *body);
static int send_email_curl(const char *to, const char *subject, const char *body);

/* ===================================================================
 * Utility Functions
 * =================================================================== */

/**
 * Callback function for libcurl to read email data
 */
static size_t payload_reader(void *ptr, size_t size, size_t nmemb, void *userp)
{
    struct email_data *upload = (struct email_data *)userp;
    size_t room = size * nmemb;
    
    if (!upload->payload || upload->bytes_read >= strlen(upload->payload)) {
        return 0;  /* No more data */
    }
    
    size_t len = strlen(upload->payload) - upload->bytes_read;
    if (len > room) {
        len = room;
    }
    
    memcpy(ptr, upload->payload + upload->bytes_read, len);
    upload->bytes_read += len;
    
    return len;
}

/**
 * Build RFC-compliant email payload
 */
static char *build_email_payload(const char *from, const char *to, 
                                 const char *subject, const char *body)
{
    char *payload;
    char date_buffer[128];
    time_t now;
    struct tm *tm_info;
    size_t needed_size;
    
    /* Get current time for Date header */
    time(&now);
    tm_info = localtime(&now);
    strftime(date_buffer, sizeof(date_buffer), 
             "%a, %d %b %Y %H:%M:%S %z", tm_info);
    
    /* Calculate needed size */
    needed_size = strlen(from) + strlen(to) + strlen(subject) + strlen(body) + 512;
    payload = malloc(needed_size);
    if (!payload) {
        return NULL;
    }
    
    /* Build email with proper headers */
    snprintf(payload, needed_size,
        "Date: %s\r\n"
        "From: %s\r\n"
        "To: %s\r\n"
        "Subject: %s\r\n"
        "Content-Type: text/plain; charset=UTF-8\r\n"
        "MIME-Version: 1.0\r\n"
        "\r\n"
        "%s\r\n",
        date_buffer, from, to, subject, body);
    
    return payload;
}

/**
 * Send email using libcurl
 */
static int send_email_curl(const char *to, const char *subject, const char *body)
{
    CURL *curl;
    CURLcode res = CURLE_OK;
    struct curl_slist *recipients = NULL;
    struct email_data upload;
    char smtp_url[256];
    int success = 0;
    
    /* Build email payload */
    upload.payload = build_email_payload(SMTP_FROM, to, subject, body);
    if (!upload.payload) {
        return 0;
    }
    upload.bytes_read = 0;
    
    /* Initialize curl */
    curl = curl_easy_init();
    if (!curl) {
        free(upload.payload);
        return 0;
    }
    
    /* Set SMTP server URL */
    snprintf(smtp_url, sizeof(smtp_url), "smtp://%s:%d", SMTP_SERVER, SMTP_PORT);
    curl_easy_setopt(curl, CURLOPT_URL, smtp_url);
    
    /* Authentication */
    #ifdef SMTP_USERNAME
    curl_easy_setopt(curl, CURLOPT_USERNAME, SMTP_USERNAME);
    #endif
    #ifdef SMTP_PASSWORD
    curl_easy_setopt(curl, CURLOPT_PASSWORD, SMTP_PASSWORD);
    #endif
    
    /* Use TLS/SSL if configured */
    #if SMTP_USE_SSL
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    #endif
    
    /* Set sender */
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, SMTP_FROM);
    
    /* Set recipient */
    recipients = curl_slist_append(recipients, to);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    
    /* Set upload callback */
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_reader);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload);
    
    /* Timeout settings */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    /* Verbose output for debugging (disable in production) */
    /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); */
    
    /* Perform the send */
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        log_error(tprintf("Email send failed: %s", curl_easy_strerror(res)));
    } else {
        success = 1;
    }
    
    /* Cleanup */
    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);
    free(upload.payload);
    
    return success;
}

/* ===================================================================
 * Public Commands
 * =================================================================== */

/**
 * EMAIL command - send email to player or address
 * Simplified version using libcurl
 */
void do_email(dbref player, const char *arg1, const char *msg)
{
    dbref victim = NOTHING;
    char to_address[ADDRESS_BUFFER_SIZE];
    char from_address[ADDRESS_BUFFER_SIZE];
    char subject[ADDRESS_BUFFER_SIZE];
    char body[EMAIL_BUFFER_SIZE];
    const char *player_email;
    const char *player_name;
    
    /* Check message */
    if (!msg || !*msg) {
        notify(player, "You must specify a message.");
        return;
    }
    
    /* Get sender's email */
    player_email = atr_get(player, A_EMAIL);
    if (!player_email || !*player_email || !strchr(player_email, '@')) {
        notify(player, 
            "Your EMAIL attribute must be set to a valid email address.");
        return;
    }
    strlcpy(from_address, player_email, sizeof(from_address));
    
    /* Determine recipient */
    if (strchr(arg1, '@')) {
        /* Direct email address */
        strlcpy(to_address, arg1, sizeof(to_address));
    } else {
        /* Player name - look up their email */
        victim = match_player(player, arg1);
        if (victim == NOTHING) {
            notify(player, tprintf("No such player: %s", arg1));
            return;
        }
        
        const char *victim_email = atr_get(victim, A_EMAIL);
        if (!victim_email || !*victim_email || !strchr(victim_email, '@')) {
            notify(player, tprintf("%s has no valid email address set.",
                                  name(victim)));
            return;
        }
        strlcpy(to_address, victim_email, sizeof(to_address));
    }
    
    /* Build subject and body */
    player_name = strip_color(unparse_object(player, player));
    snprintf(subject, sizeof(subject), 
            "Message from %s on %s", player_name, muse_name);
    
    snprintf(body, sizeof(body),
        "You have received a message from %s (%s)\n"
        "====================================\n\n"
        "%s\n\n"
        "====================================\n"
        "This message was sent from the %s game server.\n"
        "To reply, use the email command in-game or reply to: %s\n",
        player_name, from_address, msg, muse_name, from_address);
    
    /* Send the email */
    notify(player, tprintf("Sending email to %s...",
                          victim != NOTHING ? name(victim) : to_address));
    
    if (send_email_curl(to_address, subject, body)) {
        notify(player, tprintf("Email successfully sent to %s!",
                              victim != NOTHING ? name(victim) : to_address));
        
        /* Log the email */
        log_io(tprintf("EMAIL: %s (#%d) to %s",
                          name(player), player, to_address));
    } else {
        notify(player, tprintf("Failed to send email to %s.",
                              victim != NOTHING ? name(victim) : to_address));
    }
}

/**
 * Initialize email system
 * Call this once at startup
 */
void init_email_system(void)
{
    /* Initialize libcurl globally */
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

/**
 * Cleanup email system
 * Call this at shutdown
 */
void cleanup_email_system(void)
{
    /* Cleanup libcurl */
    curl_global_cleanup();
}
















//
//#include <stdio.h>
//#include <string.h>
//#include <ctype.h>
//#include <resolv.h>
//#include <sys/types.h>
//#include <sys/socket.h>
//#include <netdb.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
//#include <unistd.h>
//#include <fcntl.h>
//#include <errno.h>
//#include "db.h"
//#include "externs.h"
//
//struct email_server_struct
//{
//  char *server;
//  int pref;
//};
//
//struct email_struct
//{
//  int socket;
//  OBJ *player;
//  OBJ *victim;
//  char *from;
//  char *send_to;
//  char *message;
//  int num_servers;
//  int cur_server;
//  struct email_server_struct *email_servers;
//  struct email_struct *next;
//};
//
//typedef struct email_server_struct EMAIL_SRVR;
//typedef struct email_struct EMAIL;
//
//EMAIL *pending_email_list = NULL;
//
///* Protypes */
//static int try_next_server P((EMAIL *));
//static void send_email P((EMAIL *));
//
//static void fail_mail(OBJ *player, int fd, char *msg)
//{
//  if(msg)
//    notify(player, tprintf("EMAIL ERROR: %s", msg));
//  else
//    notify(player, "There was an error while sending your email.");
//
//  if(fd >= 0)
//    close(fd);
//}
//
//static int get_reply(int fd)
//{
//  char buf[4096];
//
//  if(read(fd, buf, sizeof(buf)) <= 0)
//    return(-1);
//
//  return(atoi(buf));
//}
//
//static int send_reply(OBJ *player, int fd, char *msg)
//{
//  if(write(fd, tprintf("%s\r\n", msg), strlen(msg)+2) <= 0)
//  {
//    fail_mail(player, fd, "Couldn't write to server.");
//    return(0);
//  }
//
//  return(1);
//}
//
//static int is_valid_address(char *address)
//{
//  if(!*address || !strchr(address, '@') || strchr(address, ' '))
//    return(0);
//  return(1);
//}
//
//static EMAIL *is_sending_email(OBJ *player)
//{
//  EMAIL *e;
//
//  for(e = pending_email_list;e;e = e->next)
//    if(e->player == player)
//      break;
//
//  return(e);
//}
//
//static EMAIL *add_email_send(OBJ *player, OBJ *victim,
//                             char *from, char *send_to, char *msg)
//{
//  EMAIL *new;
//
//  new = (EMAIL *)malloc(sizeof(EMAIL));
//  new->socket = 0;
//  new->player = player;
//  new->victim = victim;
//  new->num_servers = 0;
//  new->cur_server = -1;
//  new->email_servers = NULL;
//  SET(&new->message, msg);
//  SET(&new->from, from);
//  SET(&new->send_to, send_to);
//
//  new->next = pending_email_list;
//  pending_email_list = new;
//
//  return(new);
//}
//
//static void del_email_send(EMAIL *email)
//{
//  EMAIL *e, *eprev = NULL;
//
//  for(e = pending_email_list;e;e = e->next)
//  {
//    if(e == email)
//      break;
//    eprev = e;
//  }
//
//  if(!e)
//    return;
//
//  if(eprev)
//    eprev->next = e->next;
//  else
//    pending_email_list = e->next;
//
//  free(e->message);
//  free(e->send_to);
//  free(e->email_servers);
//  free(e);
//}
//
//void poll_pending_email()
//{
//  EMAIL *e, *enext;
//  fd_set set;
//  int max_sock = 0;
//  int rv;
//  struct timeval tv;
//  int blocking_flags;
//  int err, len = sizeof(int);  /* For getsockopt() */
//
//  FD_ZERO(&set);
//  tv.tv_sec = 0;       /* Don't */
//  tv.tv_usec = 0;      /* Wait  */
//
//  for(e = pending_email_list;e;e = e->next)
//  {
//    FD_SET(e->socket, &set);
//    if(e->socket > max_sock)
//      max_sock = (e->socket)+1;
//  }
//
//  if((rv = select(max_sock, NULL, &set, NULL, &tv)) == -1)
//  {
//    log_error(tprintf("poll_pending_email() - select(): %s",
//      strerror(errno)));
//    return;
//  }
//
//  if(!rv)   /* Nothing ready */
//    return;
//
//  for(e = pending_email_list;e;e = enext)
//  {
//    enext = e->next;
//
//    if(!FD_ISSET(e->socket, &set))
//      continue;
//
//    getsockopt(e->socket, SOL_SOCKET, SO_ERROR, (void *)&err, &len);
//    if(err)    /* connect() failed */
//    {
//      while((rv = try_next_server(e)) == 0);   /* Whole loop */
//      if(rv == -1)
//      {
//        notify(e->player, tprintf("I couldn't send email to %s.",
//          (e->victim)?name(e->victim):e->send_to));
//        del_email_send(e);
//      }
//      continue;
//    }
//
//    blocking_flags = fcntl(e->socket, F_GETFL);
//    blocking_flags &= ~O_NONBLOCK;
//    fcntl(e->socket, F_SETFL, blocking_flags);
//    send_email(e);
//    del_email_send(e);
//  }
//}
//
//static unsigned short getshort(unsigned char *ptr)
//{
//  unsigned short out;
//
//  out = (ptr[0] << 8) | ptr[1];
//  return(out);
//}
//
//static void add_server(EMAIL *email, char *server, int pref, int *ctr)
//{
//  email->email_servers = (EMAIL_SRVR *)realloc(email->email_servers,
//    sizeof(EMAIL_SRVR)*((*ctr)+1));
//  SET(&email->email_servers[*ctr].server, server);
//  email->email_servers[*ctr].pref = pref;
//  (*ctr)++;
//}
//
//static void sort_mail_servers(EMAIL *email)
//{
//  EMAIL_SRVR *ei, *ej;
//  char stemp[4096];
//  int itemp;
//  int i, j;
//
//  for(i = 0;i < email->num_servers;++i)
//    for(j = i+1;j < email->num_servers;++j)
//      if(email->email_servers[j].pref < email->email_servers[i].pref)
//      {
//        ei = &email->email_servers[i];
//        ej = &email->email_servers[j];
//
//        strcpy(stemp, ei->server);
//        free(ei->server);
//        SET(&ei->server, ej->server);
//        free(ej->server);
//        SET(&ej->server, stemp);
//        itemp = ei->pref;
//        ei->pref = ej->pref;
//        ej->pref = itemp;
//      }
//}
//
//static int get_email_servers(EMAIL *email, char *hostname)
//{
//  union
//  {
//    HEADER hdr;
//    unsigned char buf[1024];
//  } response;
//  int response_len;
//  unsigned char *response_end, *response_pos;
//  char name[1024];
//  unsigned short rrtype, rrdlen;
//  int pref;
//  int n, i;
//
//  response_len =
//    res_query(hostname, C_IN, T_MX, response.buf, sizeof(response));
//  if(response_len < 0)
//  {
//    add_server(email, hostname, 0, &email->num_servers);
//    return(email->num_servers);
//  }
//  if(response_len >= sizeof(response))
//    return(0);
//
//  response_end = response.buf+response_len;
//  response_pos = response.buf+sizeof(HEADER);
//
//  n = ntohs(response.hdr.qdcount);
//  while(n-- > 0)
//  {
//    if((i =
//      dn_expand(response.buf, response_end, response_pos, name, MAXDNAME)) < 0)
//    {
//      return(0);
//    }
//
//    response_pos += i;
//    i = response_end - response_pos;
//    if(i < QFIXEDSZ)
//      return(0);
//    response_pos += QFIXEDSZ;
//  }
//
//  n = ntohs(response.hdr.ancount);
//  while(n-- > 0)
//  {
//    if((i =
//      dn_expand(response.buf, response_end, response_pos, name, MAXDNAME)) < 0)
//    {
//      return(0);
//    }
//
//    response_pos += i;
//    rrtype = getshort(response_pos);
//    rrdlen = getshort(response_pos+8);
//    response_pos += 10;
//
//    if(rrtype == T_MX)
//    {
//      if(rrdlen < 3)
//        return(0);
//
//      pref = getshort(response_pos);
//
//      if((i =
//        dn_expand(response.buf, response_end, response_pos+2, name, MAXDNAME)) < 0)
//      {
//        return(0);
//      }
//
//      add_server(email, name, pref, &email->num_servers);
//    }
//
//    response_pos += rrdlen;
//  }
//
//  sort_mail_servers(email);
//  return(email->num_servers);
//}
//
//void do_email(OBJ *player, char *arg1, char *msg)
//{
//  EMAIL *e;
//  OBJ *victim = NULL;
//  char address[4096], from[4096];
//  char *hostname;
//
//  if((e = is_sending_email(player)))
//  {
//    notify(player, tprintf("You're already sending email to %s.",
//      (e->victim)?name(e->victim):e->send_to));
//    notify(player, "You may send another email when it is done.");
//    return;
//  }
//
//  strcpy(from, atr_get(player, A_EMAIL));
//  strcpy(address, arg1);
//
//  if(!is_valid_address(from))
//  {
//    notify(player,
//      "Your EMAIL attribute must be set to a valid reply-to address.");
//    return;
//  }
//
//  if(!is_valid_address(address))
//  {
//    if(!(victim = match_player(NULL, address)))
//    {
//      notify(player, tprintf("Invalid recipient: %s", address));
//      return;
//    }
//    strcpy(address, atr_get(victim, A_EMAIL));
//    if(!is_valid_address(address))
//    {
//      notify(player, tprintf("%s EMAIL attribute is not set.",
//        poss(victim)));
//      return;
//    }
//  }
//
//  hostname = strchr(address, '@');
//  *hostname++ = '\0';
//
///* A period (.) will interrupt the message DATA */
//  if(!*msg || *msg == '.')
//  {
//    notify(player, "You must specify a message.");
//    return;
//  }
//
//  e = add_email_send(player, victim, from,
//    tprintf("%s@%s", address, hostname), msg);
//
//  if(!get_email_servers(e, hostname))
//  {
//    notify(player, tprintf("I can't send email to %s.",
//      (victim)?name(victim):tprintf("%s@%s", address, hostname)));
//    del_email_send(e);
//    return;
//  }
//
//  notify(player,
//    tprintf("Attempting to send email to %s...Please wait.",
//    (victim)?name(victim):tprintf("%s@%s", address, hostname)));
//
//  try_next_server(e); /* Ignore return value. There's at least one server */
//}
//
///* Return values: (-1) - Every server exhausted
//                  ( 0) - Current server failed
//                  ( 1) - Connecting to valid server
//*/
//static int try_next_server(EMAIL *e)
//{
//  struct sockaddr_in addr;
//  struct hostent *hent;
//  char *email_server;
//
//  e->cur_server++;
//
//  if(e->cur_server >= e->num_servers)
//    return(-1);
//
//  email_server = e->email_servers[e->cur_server].server;
//
//  if((e->socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
//  {
//    notify(e->player, "I can't send email at this time.");
//    return(0);
//  }
//
//  addr.sin_family = AF_INET;
//  addr.sin_port = htons(25);  /* SMTP server is on port 25 */
//
//  if(!(hent = gethostbyname(email_server)))
//  {
//    if(!inet_aton(email_server, &addr.sin_addr))
//      return(0);
//  }
//  else
//    addr.sin_addr.s_addr = *(long *)hent->h_addr_list[0];
//
//  fcntl(e->socket, F_SETFL, O_NONBLOCK);
//  connect(e->socket, (struct sockaddr *)&addr, sizeof(addr));
//  return(1);
//}
//
//void send_email(EMAIL *e)
//{
//  const char *serverid = "demuse@mu-net.org";
//  const char *admin_email = "newmark@mu-net.org";
//  int replies[] = { 220, 250, 250, 250, 354, 250 };
//  int *reply = replies;
//
//  if(get_reply(e->socket) != *reply++)
//  {
//    fail_mail(e->player, e->socket, NULL);
//    return;
//  }
//
//  if(!send_reply(e->player, e->socket, "HELO master"))
//    return;
//  if(get_reply(e->socket) != *reply++)
//  {
//    fail_mail(e->player, e->socket, "Introduction failed.");
//    return;
//  }
//  if(!send_reply(e->player, e->socket, tprintf("MAIL FROM:<%s>", serverid)))
//    return;
//  if(get_reply(e->socket) != *reply++)
//  {
//    fail_mail(e->player, e->socket, "Invalid sender.");
//    return;
//  }
//  if(!send_reply(e->player, e->socket, tprintf("RCPT TO:<%s>", e->send_to)))
//    return;
//  if(get_reply(e->socket) != *reply++)
//  {
//    fail_mail(e->player, e->socket, "Invalid recipient.");
//    return;
//  }
//
//  if(!send_reply(e->player, e->socket, "DATA"))
//    return;
//  if(get_reply(e->socket) != *reply++)
//  {
//    fail_mail(e->player, e->socket, "Attempt for DATA send denied.");
//    return;
//  }
//
//  if(!send_reply(e->player, e->socket, tprintf("Reply-To:%s", e->from)))
//    return;
//  if(!send_reply(e->player, e->socket,
//    tprintf("Subject:A message from %s on %s",
//    strip_color(unparse_object(e->player, e->player)), muse_name)))
//  {
//    return;
//  }
//  if(!send_reply(e->player, e->socket, "---------- Message ----------"))
//    return;
//  if(!send_reply(e->player, e->socket, e->message))
//    return;
//  if(!send_reply(e->player, e->socket, "-------- End Message --------"))
//    return;
//  if(!send_reply(e->player, e->socket,
//    tprintf("\r\nThis message was automatically delivered by the %s server.",
//    muse_name)))
//  {
//    return;
//  }
//  if(!send_reply(e->player, e->socket,
//    tprintf("If the sender of this email (%s - %s) is harassing you,"
//    " send email to %s and the player will be disallowed from sending"
//    " emails to your address.",
//    strip_color(unparse_object(e->player, e->player)),
//    e->from, admin_email)))
//  {
//    return;
//  }
//  if(!send_reply(e->player, e->socket, "."))
//    return;
//  if(get_reply(e->socket) != *reply++)
//  {
//    fail_mail(e->player, e->socket, "DATA message was refused.");
//    return;
//  }
//
//  if(e->victim)
//    notify(e->player, tprintf("Your email to %s was successfully sent.",
//      name(e->victim)));
//  else
//    notify(e->player, "Your email was successfully sent.");
//
//  close(e->socket);
//}
