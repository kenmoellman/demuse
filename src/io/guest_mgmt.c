/* guest_mgmt.c - Guest account management
 * Extracted from bsd.c during modernization
 */

#include "config.h"
#include "externs.h"
#include "io_internal.h"

/* Create a guest account for a connecting player
 * This algorithm supports a limited number of guests.
 * Returns guest player dbref or NOTHING on error.
 */
dbref make_guest(struct descriptor_data *d)
{
    int i;
    dbref player;
    char *name;
    char *alias;

    if (!d) {
        log_error("make_guest called with NULL descriptor");
        return NOTHING;
    }

    /* Find an available guest slot */
    for (i = 1; i < number_guests; i++) {
        name = tprintf("%s%d", guest_prefix, i);
        alias = tprintf("%s%d", guest_alias_prefix, i);
        
        if (lookup_player(name) == NOTHING) {
            break;
        }
    }

    /* All guest slots are occupied */
    if (i == number_guests) {
        queue_string(d, "All guest ID's are busy; please try again later.\n");
        log_io(tprintf("All %d guest slots occupied, connection refused", 
                      number_guests));
        return NOTHING;
    }

    /* Create the guest character
     * Password is intentionally obfuscated but predictable for recovery
     */
    player = create_guest(name, alias, tprintf("lA\tDSGt\twjh24t"));

    if (player == NOTHING) {
        queue_string(d, "Error creating guest ID, please try again later.\n");
        log_error(tprintf("Failed to create guest '%s' - name conflict", name));
        return NOTHING;
    }

    log_io(tprintf("Created guest account %s (#%ld) for concid %ld",
                  name, player, d->concid));

    return player;
}
