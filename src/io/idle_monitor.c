/* idle_monitor.c - Idle detection and management
 * Extracted from bsd.c during modernization
 */

#include "config.h"
#include "externs.h"
#include "io_internal.h"
#include <limits.h>

/* Check if a descriptor should be considered idle */
int des_idle(struct descriptor_data *d)
{
    long realidle;

    if (!d || d->player < 0) {
        return 0;
    }

    realidle = atol(atr_get(d->player, A_IDLETIME));
    
    /* Clamp to reasonable range */
    if (realidle > MAX_IDLE) {
        realidle = MAX_IDLE;
    }
    if (realidle < MIN_IDLE) {
        realidle = MIN_IDLE;
    }

    return (realidle <= (now - d->last_time)) ? 1 : 0;
}

/* Internal implementation of idle checking */
void check_for_idlers_int(dbref player, char *msg)
{
    struct descriptor_data *d;
    int mybreak = 0;

    for (d = descriptor_list; d && (mybreak < 50); d = d->next) {
        mybreak++;
        
        /* Skip invalid descriptors */
        if (d->last_time <= 0 || d->player <= 0 || d->state != CONNECTED) {
            continue;
        }

        /* Skip if checking specific player and this isn't them */
        if (player >= 0 && d->player != player) {
            continue;
        }

        /* Check if player is currently idle but should be marked as such */
        if (!(db[d->player].flags & PLAYER_IDLE)) {
            long idle_time = now - d->last_time;
            long idle_limit = atol(atr_get(d->player, A_IDLETIME));
            
            /* Determine if they've exceeded their idle threshold */
            int should_idle = 0;
            
            if (idle_time > MAX_IDLE) {
                should_idle = 1;
            } else if (idle_limit > 0 && idle_limit < MIN_IDLE && 
                       idle_time > MIN_IDLE) {
                should_idle = 1;
            } else if (idle_limit >= MIN_IDLE && idle_time > idle_limit) {
                should_idle = 1;
            }

            if (should_idle) {
                struct descriptor_data *e;
                int num_idle = 0;
                int total_conn = 0;
                long shortest_idle = idle_time;

                /* Check all connections for this player */
                for (e = descriptor_list; e; e = e->next) {
                    if (e->state != CONNECTED || e->player != d->player) {
                        continue;
                    }
                    
                    total_conn++;
                    
                    long this_idle = now - e->last_time;
                    idle_limit = atol(atr_get(e->player, A_IDLETIME));
                    
                    /* Check if this connection should be idle */
                    if (this_idle > MAX_IDLE ||
                        (idle_limit > 0 && idle_limit < MIN_IDLE && 
                         this_idle > MIN_IDLE) ||
                        (idle_limit >= MIN_IDLE && this_idle > idle_limit)) {
                        num_idle++;
                        
                        /* Track shortest idle time */
                        if (this_idle < shortest_idle) {
                            shortest_idle = this_idle;
                        }
                    }
                }

                /* Only mark idle if ALL connections are idle */
                if (num_idle == total_conn && total_conn > 0) {
                    set_idle(d->player, -1, (time_t)(shortest_idle / 60), msg);
                }
            }
        }
    }
}

/* Public interface - check all players for idle timeout */
void check_for_idlers(void)
{
    check_for_idlers_int(-1, NULL);
}

/* Check if a player should be re-marked as idle after disconnect */
void check_for_disconnect_idlers(dbref player)
{
    char msg[512];
    const char *current_msg;

    if (player < 0) {
        return;
    }

    /* Build message about why they're being re-idled */
    current_msg = atr_get(player, A_IDLE_CUR);
    if (current_msg && *current_msg) {
        snprintf(msg, sizeof(msg), "%.400s - disconnect re-idle", current_msg);
    } else {
        snprintf(msg, sizeof(msg), "disconnect re-idle");
    }
    msg[sizeof(msg) - 1] = '\0';

    check_for_idlers_int(player, msg);
}

/* Check if a player should be un-idled due to reconnection */
void check_for_connect_unidlers(dbref player)
{
    struct descriptor_data *d;
    int conn = 0;
    int found = 0;

    if (player < 0) {
        return;
    }

    /* Only process if player is currently marked idle */
    if (!(db[player].flags & PLAYER_IDLE)) {
        return;
    }

    /* Count active connections for this player */
    for (d = descriptor_list; !found && d; d = d->next) {
        if (d->state == CONNECTED && d->player == player) {
            conn++;
            
            /* If they have multiple connections, un-idle them */
            if (conn > 1) {
                log_io(tprintf("%s unidled due to reconnect.", 
                              db[player].cname));
                com_send_as_hidden("pub_io", 
                    tprintf("%s unidled due to reconnect.", 
                           db[player].cname), 
                    player);
                set_unidle(player, INT_MAX);
                found = 1;
            }
        }
    }
}
