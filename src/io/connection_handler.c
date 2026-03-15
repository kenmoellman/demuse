/* connection_handler.c - Connection and authentication handling
 * Extracted from bsd.c during modernization
 */

#include "config.h"
#include "externs.h"
#include "io_internal.h"
#include "mariadb.h"
#include "mariadb_lockout.h"
#include "mariadb_auth.h"
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

    /* Fetch welcome_msg live from database so changes take effect immediately */
    {
        char *live_msg = mariadb_config_get_str("welcome_msg");
        if (live_msg) {
            send_message_text(d, live_msg, 0);
            SMART_FREE(live_msg);
        } else {
            /* Fall back to cached global if DB query fails */
            send_message_text(d, welcome_msg, 0);
        }
    }

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

    /* ================================================================
     * TOKEN-BASED AUTHENTICATION (web login)
     * ================================================================
     * When the PHP web layer authenticates a user, it generates a
     * one-time token stored in auth_tokens. The web client sends
     * "connect token:<hex>" which we intercept here. On success we
     * look up (or auto-create) the player object and connect them.
     */
    if (!strncmp(command, "co", 2) && strncmp(user, "token:", 6) == 0) {
        char *token_str = user + 6;
        long acct_id;
        char username[51];

        acct_id = mariadb_auth_validate_token(token_str);
        if (acct_id <= 0) {
            queue_string(d, "Invalid or expired token.\n");
            return;
        }

        d->account_id = acct_id;

        /* Session takeover: disconnect any existing session for this account */
        {
            struct descriptor_data *sd, *sd_next;
            for (sd = descriptor_list; sd; sd = sd_next) {
                sd_next = sd->next;
                if (sd != d && sd->account_id == acct_id &&
                    sd->state == CONNECTED) {
                    queue_string(sd,
                        "Connection superseded by new login.\n");
                    flush_all_output();
                    shutdownsock(sd);
                }
            }
        }

        /* Look up existing player for this account (universe 0) */
        player = mariadb_auth_find_player(acct_id, 0);

        if (player == NOTHING) {
            /* No player yet — auto-create one */
            if (!mariadb_auth_get_username(acct_id, username,
                                            sizeof(username))) {
                queue_string(d,
                    "Account error. Please contact an administrator.\n");
                return;
            }

            /* Check if player name already taken (pre-migration collision) */
            if (lookup_player(username) != NOTHING) {
                queue_string(d,
                    "That player name is already taken.\n"
                    "Please contact an administrator to link your account.\n");
                return;
            }

            player = create_player(username, "!token-auth!",
                                    CLASS_VISITOR, player_start);
            if (player == NOTHING) {
                queue_string(d,
                    "Failed to create player. "
                    "Please contact an administrator.\n");
                return;
            }

            mariadb_auth_link_player(acct_id, 0, player);

            log_io(tprintf("TOKEN CREATE: account %ld created player "
                           "%s(#%" DBREF_FMT ")",
                           acct_id, db[player].name, player));
        }

        /* Verify the player object is still valid */
        if (!GoodObject(player) || Typeof(player) != TYPE_PLAYER) {
            queue_string(d,
                "Your player object is invalid. "
                "Please contact an administrator.\n");
            return;
        }

        /* Check for player-level lockout */
        if (lockout_check_player(player)) {
            log_io(tprintf("%s refused token connection: account locked.",
                          unparse_object(root, player)));
            queue_string(d,
                "Your account has been locked. Contact an administrator.\n");
            return;
        }

        /* Successful token connection */
        d->state = CONNECTED;
        d->connected_at = now;
        d->player = player;

        log_io(tprintf("TOKEN CONNECT: account %ld concid %ld player "
                       "%s(#%" DBREF_FMT ")",
                       acct_id, d->concid, db[player].name, player));

        com_send_as_hidden("pub_io",
            tprintf("CONNECTED: %s - %s",
                    unparse_object_a(player, player),
                    d->addr),
            player);

        add_login(player);
        send_message_text(d, motd_msg, 0);
        announce_connect(player);
        do_look_around(player);
        return;
    }

    /* Handle WHO command */
    if (!strcmp(command, "WHO")) {
        dump_users(0, "", "", d);
        return;
    }

    /* Handle CONNECT command (non-token) */
    if (!strncmp(command, "co", 2)) {
        /* Guest connections still allowed via traditional connect */
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

            if (player > NOTHING && Typeof(player) == TYPE_PLAYER) {
                d->state = CONNECTED;
                d->connected_at = now;
                d->player = player;

                send_message_text(d, motd_msg, 0);
                announce_connect(player);
                do_look_around(player);

                notify(player, tprintf("Welcome to %s; your name is %s",
                                      muse_name, db[player].cname));
            }
        } else {
            /* Non-guest plaintext login disabled — token auth required */
            queue_string(d,
                "Plaintext password login is disabled.\n"
                "Please log in via the web interface to get a "
                "connection token.\n");
            log_io(tprintf("REFUSED LEGACY CONNECT: %s on concid %ld",
                          user, d->concid));
        }
        return;
    }

    /* Handle CREATE command — disabled, use web registration */
    if (!strncmp(command, "cr", 2)) {
        queue_string(d,
            "Character creation at the login screen is disabled.\n"
            "Please register via the web interface.\n");
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
