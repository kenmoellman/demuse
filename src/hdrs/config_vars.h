/* config_vars.h - Extern declarations for all runtime config variables
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * All runtime configuration variables are defined in config_vars.c with
 * zero/NULL initialization. Actual values are loaded from MariaDB at startup.
 *
 * This replaces the old conf.h macro trick in externs.h that generated
 * extern declarations. Variables are now explicitly declared here for
 * clarity and maintainability.
 *
 * The conf.h DO_NUM/DO_STR/DO_REF/DO_LNG macros are still used by:
 *   - conf.c (for @config command matching and @info display)
 *   - mariadb.c (for bulk load/save operations)
 */

#ifndef _CONFIG_VARS_H_
#define _CONFIG_VARS_H_

#include "config.h"

/* ============================================================================
 * STRING CONFIG VARIABLES
 * ============================================================================ */
extern char *muse_name;
extern char *dbinfo_chan;
extern char *dc_chan;
extern char *start_quota;
extern char *guest_prefix;
extern char *guest_alias_prefix;
extern char *guest_description;
extern char *bad_object_doomsday;
extern char *default_doomsday;
extern char *def_db_in;
extern char *def_db_out;
extern char *stdout_logfile;
extern char *wd_logfile;
extern char *muse_pid_file;
extern char *wd_pid_file;
extern char *create_msg_file;
extern char *motd_msg_file;
extern char *welcome_msg_file;
extern char *guest_msg_file;
extern char *register_msg_file;
extern char *leave_msg_file;
extern char *guest_lockout_file;
extern char *welcome_lockout_file;

/* ============================================================================
 * NUMERIC (int) CONFIG VARIABLES
 * ============================================================================ */
extern int allow_create;
extern int initial_credits;
extern int allowance;
extern int number_guests;
extern int announce_guests;
extern int announce_connects;
extern int inet_port;
extern int fixup_interval;
extern int dump_interval;
extern int garbage_chunk;
extern int max_output;
extern int max_output_pueblo;
extern int max_input;
extern int command_time_msec;
extern int command_burst_size;
extern int commands_per_time;
extern int warning_chunk;
extern int warning_bonus;
extern int enable_lockout;
extern int thing_cost;
extern int exit_cost;
extern int room_cost;
extern int robot_cost;
extern int channel_cost;
extern int univ_cost;
extern int link_cost;
extern int find_cost;
extern int search_cost;
extern int page_cost;
extern int announce_cost;
extern int queue_cost;
extern int queue_loss;
extern int max_queue;
extern int channel_name_limit;
extern int player_name_limit;
extern int player_reference_limit;

/* ============================================================================
 * DBREF (long) CONFIG VARIABLES
 * ============================================================================ */
extern long player_start;
extern long guest_start;
extern long default_room;
extern long root;
#ifdef USE_COMBAT
extern long graveyard;
#endif

/* ============================================================================
 * LONG CONFIG VARIABLES
 * ============================================================================ */
extern long default_idletime;
extern long guest_boot_time;
extern long max_pennies;

/* ============================================================================
 * PERMISSION DENIED MESSAGES
 * ============================================================================
 * Dynamic array of permission denied messages, loaded from MariaDB.
 * perm_denied() cycles through them round-robin style.
 */
extern char **perm_messages;
extern int perm_messages_count;
extern char *perm_denied(void);

/* ============================================================================
 * COMBAT VARIABLES (ifdef USE_COMBAT)
 * ============================================================================ */
#ifdef USE_COMBAT
extern dbref paradox[];
#endif

#endif /* _CONFIG_VARS_H_ */
