/* input_handler.c - Input processing and command handling
 * Extracted from bsd.c during modernization
 */

#include "config.h"
#include "externs.h"
#include "io_internal.h"
#include <ctype.h>
#include <unistd.h>


/* Allocate and save a string (internal helper) */
static char *strsave(const char *s)
{
    char *p;

    if (!s) {
        return NULL;
    }

//    p = malloc(strlen(s) + 1);
    SAFE_MALLOC(p, char, strlen(s) + 1);
    if (p) {
        strcpy(p, s);
    } else {
        log_error("Failed to allocate memory in strsave");
    }
    
    return p;
}

/* Save a command to the descriptor's input queue */
void save_command(struct descriptor_data *d, const char *command)
{
    if (!d || !command) {
        return;
    }
    
    add_to_queue(&d->input, command, strlen(command) + 1);
}

/* Set a user string (output prefix/suffix) */
void set_userstring(char **userstring, const char *command)
{
    if (!userstring) {
        return;
    }

    /* Free existing string */
    if (*userstring) {
        SMART_FREE(*userstring);
        *userstring = NULL;
    }

    if (!command) {
        return;
    }

    /* Skip leading whitespace */
    while (*command && isascii(*command) && isspace(*command)) {
        command++;
    }

    /* Save new string if non-empty */
    if (*command) {
        *userstring = strsave(command);
    }
}

/* Process input from a descriptor */
int process_input(struct descriptor_data *d)
{
    char buf[1024];
    int got;
    char *p, *pend, *q, *qend;

    if (!d) {
        return 0;
    }

    got = read(d->descriptor, buf, sizeof(buf));
    if (got <= 0) {
        return 0;
    }

    /* Allocate raw input buffer if needed */
    if (!d->raw_input) {
//        d->raw_input = malloc(MAX_COMMAND_LEN);
        SAFE_MALLOC(d->raw_input, char, MAX_COMMAND_LEN);
        if (!d->raw_input) {
            log_error("Failed to allocate raw input buffer");
            return 0;
        }
        d->raw_input_at = d->raw_input;
    }

    p = d->raw_input_at;
    pend = d->raw_input + MAX_COMMAND_LEN - 1;

    /* Process received data */
    for (q = buf, qend = buf + got; q < qend; q++) {
        if (*q == '\n') {
            /* End of line - save command */
            *p = '\0';
            if (p >= d->raw_input) {
                save_command(d, d->raw_input);
            }
            p = d->raw_input;
        } else if (p < pend && isascii(*q) && isprint(*q)) {
            /* Printable character - add to buffer */
            *p++ = *q;
        }
        /* Non-printable characters are silently dropped */
    }

    /* Update buffer state */
    if (p > d->raw_input) {
        d->raw_input_at = p;
    } else {
        SMART_FREE(d->raw_input);
        d->raw_input = NULL;
        d->raw_input_at = NULL;
    }

    return 1;
}

/* Process all queued commands */
void process_commands(void)
{
    int nprocessed;
    struct descriptor_data *d, *dnext;
    struct text_block *t;
    char buf[IO_BUFFER_SIZE];

    do {
        nprocessed = 0;
        
        for (d = descriptor_list; d; d = dnext) {
            dnext = d->next;
            
            /* Check if descriptor has available quota and pending input */
            if (d->quota > 0 && (t = d->input.head)) {
                nprocessed++;
                
                /* Copy command to local buffer */
                safe_string_copy(buf, t->start, sizeof(buf));
                
                /* Remove from queue */
                d->input.head = t->nxt;
                if (!d->input.head) {
                    d->input.tail = &d->input.head;
                }
                free_text_block(t);
                
                /* Process the command */
                if (!do_command(d, buf)) {
                    connect_message(d, leave_msg_file, 1);
                    shutdownsock(d);
                }
            }
        }
    } while (nprocessed > 0);
    
    clear_stack();
}

/* Execute a command from a descriptor */
int do_command(struct descriptor_data *d, char *command)
{
    if (!d) {
        return 0;
    }

#ifdef CR_UNIDLE
    /* Empty command - just return (but don't unidle) */
    if (!command || !*command) {
        return 1;
    }
#endif

    /* Unidle player if they were idle */
    if ((d->state == CONNECTED) && (db[d->player].flags & PLAYER_IDLE)) {
        set_unidle(d->player, d->last_time);
    }

    /* Update timestamp and quota */
    d->last_time = now;
    d->quota--;
    depth = 2;

    /* Empty command from connected player */
    if (!(command && *command) && 
        !(d->player < 0 && d->state == CONNECTED)) {
        return 1;
    }

#ifdef WHO_BY_IDLE
    /* Move active player to top of WHO list */
    if (d->state == CONNECTED && d->player > 0) {
        *d->prev = d->next;
        if (d->next) {
            d->next->prev = d->prev;
        }

        d->next = descriptor_list;
        if (descriptor_list) {
            descriptor_list->prev = &d->next;
        }
        descriptor_list = d;
        d->prev = &descriptor_list;
    }
#endif

    /* Handle special commands */
    if (!strcmp(command, QUIT_COMMAND)) {
        return 0;
    }

#ifdef USE_CID_PLAY
    /* Concentrator control commands */
    if (!strncmp(command, "I wanna be a concentrator... my password is ",
                 sizeof("I wanna be a concentrator... my password is ") - 1)) {
        do_becomeconc(d, command + 
                     sizeof("I wanna be a concentrator... my password is ") - 1);
        return 1;
    }
#endif

    /* Output prefix/suffix commands */
    if (!strncmp(command, PREFIX_COMMAND, strlen(PREFIX_COMMAND))) {
        set_userstring(&d->output_prefix, command + strlen(PREFIX_COMMAND));
        return 1;
    }
    
    if (!strncmp(command, SUFFIX_COMMAND, strlen(SUFFIX_COMMAND))) {
        set_userstring(&d->output_suffix, command + strlen(SUFFIX_COMMAND));
        return 1;
    }

#ifdef USE_CID_PLAY
    /* Handle concentrator control commands */
    if (d->cstatus & C_CCONTROL) {
        if (!strcmp(command, "Gimmie a new concid")) {
            do_makeid(d);
        } else if (!strncmp(command, "I wanna connect concid ",
                           sizeof("I wanna connect concid ") - 1)) {
            char *m, *n;
            m = command + sizeof("I wanna connect concid ") - 1;
            n = strchr(m, ' ');
            if (!n) {
                queue_string(d, 
                    "Usage: I wanna connect concid <id> <hostname>\n");
            } else {
                do_connectid(d, atoi(command + 
                    sizeof("I wanna connect concid ") - 1), n);
            }
        } else if (!strncmp(command, "I wanna kill concid ",
                           sizeof("I wanna kill concid ") - 1)) {
            do_killid(d, atoi(command + 
                sizeof("I wanna kill concid ") - 1));
        } else {
            /* Forward to specific concid */
            char *k = strchr(command, ' ');
            if (!k) {
                queue_string(d, "Huh???\r\n");
            } else {
                struct descriptor_data *l;
                int j;
                
                *k = '\0';
                j = atoi(command);
                
                for (l = descriptor_list; l; l = l->next) {
                    if (l->concid == j) {
                        break;
                    }
                }
                
                if (!l) {
                    queue_string(d, "I don't know that concid.\r\n");
                } else {
                    k++;
                    if (!do_command(l, k)) {
                        connect_message(l, leave_msg_file, 1);
                        shutdownsock(l);
                    }
                }
            }
        }
        return 1;
    }
#endif /* USE_CID_PLAY */

    /* Handle regular game commands */
    if (d->state == CONNECTED) {
        /* Send output prefix if set */
        if (d->output_prefix) {
            queue_string(d, d->output_prefix);
            queue_write(d, "\n", 1);
        }

        /* Process game command */
        cplr = d->player;
        if (d->player > 0) {
            safe_string_copy(ccom, command, sizeof(ccom));
            process_command(d->player, command, NOTHING);
        } else {
            log_error(tprintf("ERROR: Negative player %ld trying to execute %s",
                            d->player, command));
            notify(-d->player, command);
        }

        /* Send output suffix if set */
        if (d->output_suffix) {
            queue_string(d, d->output_suffix);
            queue_write(d, "\n", 1);
        }
    } else {
        /* Not connected yet - handle login/creation */
        d->pueblo--;
        check_connect(d, command);
    }

    return 1;
}
