/* descriptor_mgmt.c - Descriptor lifecycle management
 * Extracted from bsd.c during modernization
 */

#include "config.h"
#include "externs.h"
#include "io_internal.h"

/* Internal structure for connection tracing */
struct ctrace_int {
    struct descriptor_data *des;
    struct ctrace_int **children;
};

///* Clear output prefix/suffix strings for a descriptor */
//void clearstrings(struct descriptor_data *d)
//{
//    if (!d) {
//        return;
//    }
//
//    if (d->output_prefix) {
//        SMART_FREE(d->output_prefix);
//        d->output_prefix = NULL;
//    }
//    
//    if (d->output_suffix) {
//        SMART_FREE(d->output_suffix);
//        d->output_suffix = NULL;
//    }
//}

/* Announce a player connection to the game world */
void announce_connect(dbref player)
{
    dbref loc;
    char buf[BUFFER_LEN];
    extern dbref speaker;
    char *s;
    time_t tt;
    int connect_again;

    if (player < 0 || player >= db_top) {
        log_error(tprintf("announce_connect called with invalid player %" DBREF_FMT,
                         player));
        return;
    }

    loc = getloc(player);
    if (loc == NOTHING) {
        log_error(tprintf("announce_connect: player %" DBREF_FMT " has no location",
                         player));
        return;
    }

    connect_again = (db[player].flags & CONNECT) ? 1 : 0;

    /* Build connection message */
    if (connect_again) {
        check_for_connect_unidlers(player);
        snprintf(buf, sizeof(buf), "%s has reconnected.", db[player].cname);
    } else {
        snprintf(buf, sizeof(buf), "%s has connected.", db[player].cname);
    }
    buf[sizeof(buf) - 1] = '\0';

    /* Clear idle flag on connect */
    db[player].flags &= ~PLAYER_IDLE;

    /* Notify player's inventory and room */
    speaker = player;
    notify_in(player, player, buf);
    if (!IS(loc, TYPE_ROOM, ROOM_AUDITORIUM)) {
        notify_in(loc, player, buf);
    }

    /* Set connected flag */
    db[player].flags |= CONNECT;
    if (Typeof(player) == TYPE_PLAYER) {
        db[player].flags &= ~HAVEN;
    }

    /* Handle login messages for non-guests */
    if (!Guest(player)) {
        char time_buf[30];
        
        tt = now;
        strncpy(time_buf, ctime(&tt), sizeof(time_buf) - 1);
        time_buf[sizeof(time_buf) - 1] = '\0';
        if (strlen(time_buf) > 0) {
            time_buf[strlen(time_buf) - 1] = '\0';  /* Remove newline */
        }

        tt = atol(atr_get(player, A_LASTDISC));
        if (tt == 0L) {
            s = FIRST_LOGIN;
        } else {
            s = ctime(&tt);
            if (s && strlen(s) > 0) {
                s[strlen(s) - 1] = '\0';  /* Remove newline */
            }
            
            /* Pay allowance if it's a new day and first connection today */
            if (s && strncmp(time_buf, s, 10) != 0 &&
                power(player, POW_MEMBER) && 
                db[player].owner == player &&
                !connect_again) {
                giveto(player, allowance);
                notify(player, tprintf("You collect %d credits.", allowance));
            }
        }
        
        notify(player, tprintf("Last login: %s", s ? s : "unknown"));
        
        /* Update last connection time */
        atr_add(player, A_LASTCONN, tprintf("%ld", now));
        
        /* Check for new mail */
        check_mail(player, db[player].name);
    }

    /* Trigger connection attributes */
    if (!connect_again) {
        dbref thing, zone;
        int depth, find = 0;

        /* Player's own @aconnect */
        did_it(player, player, NULL, NULL, A_OCONN, NULL, A_ACONN);
        
        /* Location's @aconnect */
        did_it(player, db[player].location, NULL, NULL, NULL, NULL, A_ACONN);

        /* Determine zone */
        zone = db[0].zone;
        if (Typeof(db[player].location) == TYPE_ROOM) {
            zone = db[db[player].location].zone;
        } else {
            thing = db[player].location;
            for (depth = 10; depth && !find; depth--, thing = db[thing].location) {
                if (Typeof(thing) == TYPE_ROOM) {
                    zone = db[thing].zone;
                    find = 1;
                }
            }
        }

        /* Zone @aconnect */
        if (db[0].zone != zone && Typeof(db[0].zone) != TYPE_PLAYER) {
            did_it(player, db[0].zone, NULL, NULL, NULL, NULL, A_ACONN);
        }
        if (Typeof(zone) != TYPE_PLAYER) {
            did_it(player, zone, NULL, NULL, NULL, NULL, A_ACONN);
        }

        /* Inventory @aconnect */
        thing = db[player].contents;
        if (thing != NOTHING) {
            DOLIST(thing, thing) {
                if (Typeof(thing) != TYPE_PLAYER) {
                    did_it(player, thing, NULL, NULL, NULL, NULL, A_ACONN);
                }
            }
        }

        /* Room contents @aconnect */
        thing = db[db[player].location].contents;
        if (thing != NOTHING) {
            DOLIST(thing, thing) {
                if (Typeof(thing) != TYPE_PLAYER) {
                    did_it(player, thing, NULL, NULL, NULL, NULL, A_ACONN);
                }
            }
        }
    }
}

/* Announce a player disconnection */
void announce_disconnect(dbref player)
{
    dbref loc;
    int num;
    char buf[BUFFER_LEN];
    struct descriptor_data *d;
    extern dbref speaker;
    int partial_disconnect;

    if (player < 0 || player >= db_top) {
        return;
    }

    /* Remove any paste in progress */
    if (is_pasting(player)) {
        remove_paste(player);
    }

    /* Check if this is a partial disconnect (player has other connections) */
    num = 0;
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED && d->player > 0 && d->player == player) {
            num++;
        }
    }

    partial_disconnect = (num >= 2 && !shutdown_flag);

    /* Update connection tracking */
    atr_add(player, A_LASTDISC, tprintf("%ld", now));
    
    /* Update total time connected */
    {
        long prev_time = atol(atr_get(player, A_PREVTIME));
        long last_conn = atol(atr_get(player, A_LASTCONN));
        long session_time = (now > last_conn) ? (now - last_conn) : 0;
        
        atr_add(player, A_PREVTIME, tprintf("%ld", prev_time + session_time));
    }

    if (!partial_disconnect) {
        db[player].flags &= ~CONNECT;
        atr_add(player, A_IT, "");
    }

    /* Announce the disconnection */
    loc = getloc(player);
    if (loc != NOTHING) {
        if (partial_disconnect) {
            snprintf(buf, sizeof(buf), "%s has partially disconnected.", 
                    db[player].cname);
            check_for_disconnect_idlers(player);
        } else {
            snprintf(buf, sizeof(buf), "%s has disconnected.", 
                    db[player].cname);
        }
        buf[sizeof(buf) - 1] = '\0';

        speaker = player;
        notify_in(player, player, buf);
        if (!IS(loc, TYPE_ROOM, ROOM_AUDITORIUM)) {
            notify_in(loc, player, buf);
        }

        /* Trigger disconnection attributes */
        if (!partial_disconnect) {
            dbref zone, thing;
            int depth, find = 0;

            /* Player's own @adisconnect */
            did_it(player, player, NULL, NULL, A_ODISC, NULL, A_ADISC);
            
            /* Location's @adisconnect */
            did_it(player, db[player].location, NULL, NULL, NULL, NULL, A_ADISC);

            /* Determine zone */
            zone = db[0].zone;
            if (Typeof(db[player].location) == TYPE_ROOM) {
                zone = db[db[player].location].zone;
            } else {
                thing = db[player].location;
                for (depth = 10; depth && !find; depth--, 
                     thing = db[thing].location) {
                    if (Typeof(thing) == TYPE_ROOM) {
                        zone = db[thing].zone;
                        find = 1;
                    }
                }
            }

            /* Zone @adisconnect */
            if (db[0].zone != zone && Typeof(db[0].zone) != TYPE_PLAYER) {
                did_it(player, db[0].zone, NULL, NULL, NULL, NULL, A_ADISC);
            }
            if (Typeof(zone) != TYPE_PLAYER) {
                did_it(player, zone, NULL, NULL, NULL, NULL, A_ADISC);
            }

            /* Inventory @adisconnect */
            thing = db[player].contents;
            if (thing != NOTHING) {
                DOLIST(thing, thing) {
                    if (Typeof(thing) != TYPE_PLAYER) {
                        did_it(player, thing, NULL, NULL, NULL, NULL, A_ADISC);
                    }
                }
            }

            /* Room contents @adisconnect */
            thing = db[db[player].location].contents;
            if (thing != NOTHING) {
                DOLIST(thing, thing) {
                    if (Typeof(thing) != TYPE_PLAYER) {
                        did_it(player, thing, NULL, NULL, NULL, NULL, A_ADISC);
                    }
                }
            }
        }
    }
}

/* Internal function to build connection trace tree */
static struct ctrace_int *internal_ctrace(struct descriptor_data *parent)
{
    struct descriptor_data *k;
    struct ctrace_int *op;
    int nchild;

    op = stack_em(sizeof(struct ctrace_int));
    if (!op) {
        return NULL;
    }

    op->des = parent;

    /* If not a concentrator control, no children */
    if (parent && !(parent->cstatus & C_CCONTROL)) {
        op->children = stack_em(sizeof(struct ctrace_int *));
        if (op->children) {
            op->children[0] = NULL;
        }
    } else {
        /* Count children */
        nchild = 0;
        for (k = descriptor_list; k; k = k->next) {
            if (k->parent == parent) {
                nchild++;
            }
        }

        /* Allocate child array */
        op->children = stack_em(sizeof(struct ctrace_int *) * (nchild + 1));
        if (!op->children) {
            return op;
        }

        /* Build child trees recursively */
        nchild = 0;
        for (k = descriptor_list; k; k = k->next) {
            if (k->parent == parent) {
                op->children[nchild] = internal_ctrace(k);
                nchild++;
            }
        }
        op->children[nchild] = NULL;
    }

    return op;
}

/* Internal function to notify connection trace */
static void ctrace_notify_internal(dbref player, struct ctrace_int *d, int dep)
{
    char buf[2000];
    int j, k;

    if (!d) {
        return;
    }

    /* Build indentation */
    for (j = 0; j < dep && j < (int)sizeof(buf) - 1; j++) {
        buf[j] = '.';
    }

    /* Format descriptor info */
    if (d->des && dep) {
        snprintf(buf + j, sizeof(buf) - j,
                "%s descriptor: %d, concid: %ld, host: %s@%s",
                (d->des->state == CONNECTED)
                    ? tprintf("\"%s\"", unparse_object(player, d->des->player))
                    : ((d->des->cstatus & C_CCONTROL)
                        ? "<Concentrator Control>"
                        : "<Unconnected>"),
                d->des->descriptor, d->des->concid, 
                d->des->user, d->des->addr);
        buf[sizeof(buf) - 1] = '\0';
        notify(player, buf);
    }

    /* Recursively display children */
    if (d->children) {
        for (k = 0; d->children[k]; k++) {
            ctrace_notify_internal(player, d->children[k], dep + 1);
        }
    }
}

/* Display connection trace for WHO debugging */
void do_ctrace(dbref player)
{
    struct ctrace_int *dscs;

    if (!power(player, POW_WHO)) {
        notify(player, perm_denied());
        return;
    }

    dscs = internal_ctrace(NULL);
    if (dscs) {
        ctrace_notify_internal(player, dscs, 0);
    } else {
        notify(player, "Failed to build connection trace.");
    }
}
