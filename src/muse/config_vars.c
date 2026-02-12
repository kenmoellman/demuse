/* config_vars.c - Runtime configuration variable definitions
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * All runtime configuration variables are defined here with zero/NULL
 * initialization. Actual default values are stored in MariaDB (seeded from
 * config/defaults.sql) and loaded at startup via mariadb_config_load().
 *
 * This replaces the old config/config.c which was #include'd into conf.c
 * and wd.c to provide global variable definitions with hardcoded defaults.
 *
 * IMPORTANT: These variables MUST NOT have compiled-in default values.
 * All defaults come from the database. If the database is not populated,
 * the server will not start.
 */

#include "config.h"
#include "externs.h"

/* ============================================================================
 * STRING CONFIG VARIABLES
 * ============================================================================ */
char *muse_name = NULL;
char *dbinfo_chan = NULL;
char *dc_chan = NULL;
char *start_quota = NULL;
char *guest_prefix = NULL;
char *guest_alias_prefix = NULL;
char *guest_description = NULL;
char *bad_object_doomsday = NULL;
char *default_doomsday = NULL;
char *def_db_in = NULL;
char *def_db_out = NULL;
char *stdout_logfile = NULL;
char *wd_logfile = NULL;
char *muse_pid_file = NULL;
char *wd_pid_file = NULL;
char *create_msg_file = NULL;
char *motd_msg_file = NULL;
char *welcome_msg_file = NULL;
char *guest_msg_file = NULL;
char *register_msg_file = NULL;
char *leave_msg_file = NULL;
char *guest_lockout_file = NULL;
char *welcome_lockout_file = NULL;

/* ============================================================================
 * NUMERIC (int) CONFIG VARIABLES
 * ============================================================================ */
int allow_create = 0;
int initial_credits = 0;
int allowance = 0;
int number_guests = 0;
int announce_guests = 0;
int announce_connects = 0;
int inet_port = 0;
int fixup_interval = 0;
int dump_interval = 0;
int garbage_chunk = 0;
int max_output = 0;
int max_output_pueblo = 0;
int max_input = 0;
int command_time_msec = 0;
int command_burst_size = 0;
int commands_per_time = 0;
int warning_chunk = 0;
int warning_bonus = 0;
int enable_lockout = 0;
int thing_cost = 0;
int exit_cost = 0;
int room_cost = 0;
int robot_cost = 0;
int channel_cost = 0;
int univ_cost = 0;
int link_cost = 0;
int find_cost = 0;
int search_cost = 0;
int page_cost = 0;
int announce_cost = 0;
int queue_cost = 0;
int queue_loss = 0;
int max_queue = 0;
int channel_name_limit = 0;
int player_name_limit = 0;
int player_reference_limit = 0;

/* ============================================================================
 * DBREF (long) CONFIG VARIABLES
 * ============================================================================ */
long player_start = 0;
long guest_start = 0;
long default_room = 0;
long root = 0;
#ifdef USE_COMBAT
long graveyard = 0;
#endif

/* ============================================================================
 * LONG CONFIG VARIABLES
 * ============================================================================ */
long default_idletime = 0;
long guest_boot_time = 0;
long max_pennies = 0;

/* ============================================================================
 * PERMISSION DENIED MESSAGES
 * ============================================================================
 * Dynamic array loaded from MariaDB (perm_messages-1, perm_messages-2, ...).
 * perm_denied() cycles through the array round-robin style.
 */
char **perm_messages = NULL;
int perm_messages_count = 0;

/*
 * perm_denied - Return a cycling permission denied message
 *
 * Rotates through the perm_messages array. Returns a generic fallback
 * if no messages are loaded (should not happen in normal operation).
 *
 * RETURNS: Pointer to a permission denied message string
 */
char *perm_denied(void)
{
    static int messageno = -1;

    if (!perm_messages || perm_messages_count <= 0) {
        return "Permission denied.";
    }

    messageno++;
    if (messageno >= perm_messages_count) {
        messageno = 0;
    }

    return perm_messages[messageno];
}

/* ============================================================================
 * COMBAT VARIABLES (ifdef USE_COMBAT)
 * ============================================================================ */
#ifdef USE_COMBAT
dbref paradox[] = { 0, 59, 1140, 1152, 1136, 55, 1164, 1169, 1173, 1177, -1};
static int combat = 3;
#endif
