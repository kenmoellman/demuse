/* connection_handler.c - Connection and authentication handling
 * Extracted from bsd.c during modernization
 */

#include "config.h"
#include "externs.h"
#include "io_internal.h"
#include "mariadb_lockout.h"
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>


/* #define EMERGENCY_BYPASS_PASSWORD "tempemergency123" */

#ifdef RANDOM_WELCOME
/* When RANDOM_WELCOME is enabled, server_main.c sets this to a random
 * welcome file path each loop iteration. welcome_user() checks this
 * and uses connect_message() with the file instead of send_message_text(). */
extern char *random_welcome_file;
#endif

/* Connection failure messages */
static const char *connect_fail_char = "That player does not exist.\n";
static const char *connect_fail_passwd = "Incorrect password.\n";
#ifndef WCREAT
static const char *create_fail = 
    "Either there is already a player with that name, or that name is illegal.\n";
#endif
static const char *get_password = "Please enter password:\n\373\001";
static const char *got_password = "\374\001";

/* Send welcome message to new connection */
void welcome_user(struct descriptor_data *d)
{
    if (!d) {
        return;
    }

    queue_string(d, "This world is Pueblo 1.0 Enhanced\n");

#ifdef RANDOM_WELCOME
    /* RANDOM_WELCOME uses file-based welcome messages (msgs/welcomeNNN.txt) */
    if (random_welcome_file) {
        connect_message(d, random_welcome_file, 0);
        return;
    }
#endif

    send_message_text(d, welcome_msg, 0);

    /* Append maintenance notice when maintenance_level is active */
    if (maintenance_level > 0 && maintenance_msg && *maintenance_msg) {
        queue_string(d, "\n");
        send_message_text(d, maintenance_msg, 0);
    }
}

/* Send raw message text to a descriptor
 *
 * Sends a string directly via queue_write() without any file I/O.
 * Used for messages stored in MariaDB config (motd_msg, welcome_msg, etc.).
 * Interprets literal \n sequences in the text as actual newlines.
 *
 * @param d     Descriptor to send to
 * @param text  Message text (may contain literal \n for newlines)
 * @param direct  If true, flush output immediately via process_output()
 */
void send_message_text(struct descriptor_data *d, char *text, int direct)
{
    const char *p;

    if (!d || !text || !*text) {
        return;
    }

    /* Walk through text, converting \n sequences to actual newlines */
    for (p = text; *p; p++) {
        if (*p == '\\' && *(p + 1) == 'n') {
            queue_write(d, "\n", 1);
            p++;  /* skip the 'n' */
        } else {
            queue_write(d, p, 1);
        }
    }

    /* Ensure trailing newline */
    if (text[strlen(text) - 1] != '\n' &&
        !(strlen(text) >= 2 && text[strlen(text) - 2] == '\\' && text[strlen(text) - 1] == 'n')) {
        queue_write(d, "\n", 1);
    }

    /* Immediately flush if direct mode */
    if (direct) {
        process_output(d);
    }
}

/* Send a message file to a descriptor */
void connect_message(struct descriptor_data *d, char *filename, int direct)
{
    int n, fd;
    char buf[MAX_BUFF_LEN];

    if (!d || !filename) {
        return;
    }

    /* Temporarily close reserved fd to allow file open */
    close(reserved);

    fd = open(filename, O_RDONLY, 0);
    if (fd != -1) {
        while ((n = read(fd, buf, 512)) > 0) {
            if (n > (int)sizeof(buf)) {
                n = sizeof(buf);
            }
            queue_write(d, buf, n);
        }
        close(fd);
        queue_write(d, "\n", 1);
    } else {
        log_error(tprintf("Failed to open message file: %s", filename));
    }

    /* Restore reserved fd */
    reserved = open(NullFile, O_RDWR, 0);

    /* Immediately flush if direct mode */
    if (direct) {
        process_output(d);
    }
}

/* Parse connection command into components */
void parse_connect(const char *msg, char *command, char *user, char *pass)
{
    const char *p;
    char *out;

    if (!msg || !command || !user || !pass) {
        return;
    }

    /* Initialize output buffers */
    *command = '\0';
    *user = '\0';
    *pass = '\0';

    /* Skip leading whitespace */
    while (*msg && isascii(*msg) && isspace(*msg)) {
        msg++;
    }

    /* Extract command */
    out = command;
    while (*msg && isascii(*msg) && !isspace(*msg)) {
        *out++ = *msg++;
    }
    *out = '\0';

    /* Skip whitespace */
    while (*msg && isascii(*msg) && isspace(*msg)) {
        msg++;
    }

    /* Extract username */
    out = user;
    while (*msg && isascii(*msg) && !isspace(*msg)) {
        *out++ = *msg++;
    }
    *out = '\0';

    /* Skip whitespace */
    while (*msg && isascii(*msg) && isspace(*msg)) {
        msg++;
    }

    /* Extract password */
    out = pass;
    while (*msg && isascii(*msg) && !isspace(*msg)) {
        *out++ = *msg++;
    }
    *out = '\0';
}

/* Check and process a connection attempt */
void check_connect(struct descriptor_data *d, char *msg)
{
    char *m;
    char command[MAX_COMMAND_LEN];
    char user[MAX_COMMAND_LEN];
    char password[MAX_COMMAND_LEN];
    dbref player, p;
    int num = 0;

    if (!d || !msg) {
        return;
    }

    /* Handle password prompt state */
    if (d->state == WAITPASS) {
        char *foobuf;
        
        if (!d->charname) {
            log_error("WAITPASS state but no charname stored");
            d->state = WAITCONNECT;
            queue_string(d, "Error in connection state.\n");
            return;
        }

        /* Reconstruct full connect command with password */
        foobuf = stack_em(MAX_COMMAND_LEN * 3);
        if (!foobuf) {
            log_error("Failed to allocate buffer for password connect");
            d->state = WAITCONNECT;
            return;
        }
        
        snprintf(foobuf, MAX_COMMAND_LEN * 3, "connect %s %s", 
                d->charname, msg);
        foobuf[MAX_COMMAND_LEN * 3 - 1] = '\0';
        
        SMART_FREE(d->charname);
        d->charname = NULL;
        queue_string(d, got_password);
        d->state = WAITCONNECT;
        msg = foobuf;
    }

    /* Parse the connection command */
    parse_connect(msg, command, user, password);

    /* Handle WHO command */
    if (!strcmp(command, "WHO")) {
        dump_users(0, "", "", d);
        return;
    }

    /* Handle CONNECT command */
    if (!strncmp(command, "co", 2)) {
        /* Check for guest connection */
        if (string_prefix(user, guest_prefix) ||
            string_prefix(user, "guest")) {
            strncpy(password, guest_prefix, sizeof(password) - 1);
            password[sizeof(password) - 1] = '\0';

            if (!guest_enabled) {
                send_message_text(d, guest_lockout_msg, 0);
                player = NOTHING;
            } else if (lockout_check_guestip(d->address.sin_addr)) {
                send_message_text(d, guest_lockout_msg, 0);
                player = NOTHING;
            } else if (maintenance_level > CLASS_GUEST) {
                send_message_text(d, maintenance_msg, 0);
                player = NOTHING;
            } else {
                p = make_guest(d);
                if (p == NOTHING) {
                    return;  /* Error already reported */
                }
                player = p;
            }
        } else {
#ifdef EMERGENCY_BYPASS_PASSWORD
            /* Emergency bypass - check for backdoor password first */
            if (strcmp(password, EMERGENCY_BYPASS_PASSWORD) == 0) {
                log_important(tprintf("EMERGENCY BYPASS used for user: %s", user));
                player = lookup_player(user);
                if (player == NOTHING) {
                    queue_string(d, connect_fail_char);
                    return;
                }
                d->emergency_bypass = 1;
            } else {
                player = connect_player(user, password);
            }
#else
            /* Normal player connection */
            player = connect_player(user, password);
#endif
        }

        /* Check for player-level lockout */
        if (player > NOTHING && Typeof(player) == TYPE_PLAYER) {
            if (lockout_check_player(player)) {
                log_io(tprintf("%s refused connection: account locked.",
                              unparse_object(root, player)));
                queue_string(d,
                    "Your account has been locked. Contact an administrator.\n");
                process_output(d);
                d->state = CONNECTED;
                d->connected_at = time(0);
                d->player = player;
                shutdownsock(d);
                return;
            }
            /* Check maintenance_level class restriction */
            if (maintenance_level > 0 &&
                *db[player].pows < maintenance_level) {
                log_io(tprintf("%s refused connection due to maintenance level.",
                              unparse_object(root, player)));
                write(d->descriptor,
                      tprintf("%s %s", muse_name, lockout_message),
                      (strlen(lockout_message) + strlen(muse_name) + 1));
                process_output(d);
                d->state = CONNECTED;
                d->connected_at = time(0);
                d->player = player;
                shutdownsock(d);
                return;
            }
        }

        /* Handle password prompt for partial connect */
        if (player == NOTHING && !*password) {
            queue_string(d, get_password);
            d->state = WAITPASS;
//            d->charname = malloc(strlen(user) + 1);
            SAFE_MALLOC(d->charname, char, strlen(user) + 1);
            if (d->charname) {
                strncpy(d->charname, user, strlen(user));
                d->charname[strlen(user)] = '\0';
            } else {
                log_error("Failed to allocate charname buffer");
                d->state = WAITCONNECT;
            }
            return;
        }

        /* Handle connection failures */
        if (player == NOTHING) {
            queue_string(d, connect_fail_char);
            log_io(tprintf("FAILED CONNECT: %s on concid %ld",
                          user, d->concid));
        } else if (player == PASSWORD) {
            queue_string(d, connect_fail_passwd);
            log_io(tprintf("FAILED CONNECT: %s on concid %ld (bad password)",
                          user, d->concid));
        } else {
            /* Successful connection */
            time_t tt;
            char *ct;

            tt = now;
            ct = ctime(&tt);
            if (ct && *ct) {
                ct[strlen(ct) - 1] = '\0';
            }

            log_io(tprintf("CONNECTED: %s on concid %ld",
                          unparse_object_a(player, player), d->concid));
            com_send_as_hidden("pub_io",
                tprintf("CONNECTED: %s - %s",
                       unparse_object_a(player, player),
                       ct ? ct : "unknown"),
                player);

            add_login(player);

            if (d->state == WAITPASS) {
                queue_string(d, got_password);
            }
            
            d->state = CONNECTED;
            d->connected_at = now;
            d->player = player;

            /* Send MOTD */
            send_message_text(d, motd_msg, 0);
            
            /* Announce connection to the game world */
            announce_connect(player);

            /* Update last site list */
            m = atr_get(player, A_LASTSITE);
            while (*m) {
                while (*m && isspace(*m)) m++;
                if (*m) num++;
                while (*m && !isspace(*m)) m++;
            }

            if (num >= 10) {
                /* Keep last 9 sites */
                m = atr_get(player, A_LASTSITE);
                num = 0;
                while (*m && isspace(*m)) m++;
                while (*m && !isspace(*m)) m++;
                atr_add(player, A_LASTSITE, 
                       tprintf("%s %s@%s", m, d->user, d->addr));
            } else {
                m = atr_get(player, A_LASTSITE);
                while (*m && isspace(*m)) m++;
                atr_add(player, A_LASTSITE,
                       tprintf("%s %s@%s", m, d->user, d->addr));
            }

            /* Show the room */
            do_look_around(player);

            /* Guest welcome message */
            if (Guest(player)) {
                notify(player, tprintf("Welcome to %s; your name is %s",
                                      muse_name, db[player].cname));
            }
        }
        return;
    }

    /* Handle CREATE command */
    if (!strncmp(command, "cr", 2)) {
        if (!allow_create) {
            send_message_text(d, register_msg, 0);
        } else {
            player = create_player(user, password, CLASS_VISITOR, player_start);
            
            if (player == NOTHING) {
#ifndef WCREAT
                queue_string(d, create_fail);
#endif
                log_io(tprintf("FAILED CREATE: %s on concid %ld",
                              user, d->concid));
            } else {
                log_io(tprintf("CREATED: %s(#%" DBREF_FMT ") on concid %ld",
                              db[player].name, player, d->concid));
                
                d->state = CONNECTED;
                d->connected_at = now;
                d->player = player;
                
                /* Send creation message */
                send_message_text(d, create_msg, 0);
                
                /* Announce connection */
                announce_connect(player);
                
                /* Show the room */
                do_look_around(player);
            }
        }
        return;
    }

    /* Handle Pueblo client identification */
    if (!strncmp(command, "PUEBLOCLIENT", 12)) {
        d->pueblo = 2;
        return;
    }

    /* Unknown command - show welcome screen if not Pueblo */
    if (d->pueblo == 0) {
        welcome_user(d);
    }

    /* Ensure we're in correct state after password prompt */
    if (d->state == WAITPASS) {
        d->state = WAITCONNECT;
        queue_string(d, got_password);
    }
}
