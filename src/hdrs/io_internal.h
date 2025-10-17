/* io_internal.h - Internal header for I/O subsystem modules
 * This file is NOT included in externs.h and is only for use
 * within the io/ directory modules.
 */

#ifndef IO_INTERNAL_H
#define IO_INTERNAL_H

#include "config.h"
#include "externs.h"
#include "net.h"
#include <sys/time.h>

/* ===================================================================
 * Constants
 * =================================================================== */

#define IO_BUFFER_SIZE 2048
#define ANSI_BUFFER_SIZE 4096
#define HTML_BUFFER_SIZE 65536
#define MAX_COMMAND_LEN 1000

/* ===================================================================
 * Internal String Utilities
 * =================================================================== */

/**
 * Safely copy a string with bounds checking
 * @param dest Destination buffer
 * @param src Source string
 * @param dest_size Size of destination buffer
 */
void safe_string_copy(char *dest, const char *src, size_t dest_size);

/**
 * Safely concatenate a string with bounds checking
 * @param dest Destination buffer
 * @param src Source string
 * @param dest_size Size of destination buffer
 * @return 0 on success, -1 if truncation occurred
 */
int safe_string_cat(char *dest, const char *src, size_t dest_size);

/* ===================================================================
 * Text Queue Management (text_queue.c)
 * =================================================================== */

/**
 * Create a text block containing the given data
 * @param s Data to store
 * @param n Length of data
 * @return New text block or NULL on error
 */
struct text_block *make_text_block(const char *s, int n);

/**
 * Free a text block
 * @param t Text block to free
 */
void free_text_block(struct text_block *t);

///**
// * Free all queued text for a descriptor
// * @param d Descriptor whose queues should be freed
// */
//void freeqs(struct descriptor_data *d);

/* ===================================================================
 * Descriptor Management (descriptor_mgmt.c)
 * =================================================================== */

///**
// * Clear output prefix/suffix strings for a descriptor
// * @param d Descriptor to clear
// */
//void clearstrings(struct descriptor_data *d);

/**
 * Get short name for display (uses alias if shorter)
 * @param obj Object to get name for
 * @return Short name string (static buffer)
 */
char *short_name(dbref obj);

/* ===================================================================
 * Output Handling (output_handler.c)
 * =================================================================== */

/**
 * Format player output with color parsing and prefix/suffix
 * @param player Player receiving output
 * @param color 1 to parse color codes, 0 to strip them
 * @param msg Message to format
 * @param pueblo 1 for Pueblo HTML output, 0 for ANSI
 * @return Formatted message (allocated via stralloc)
 */
char *format_player_output(dbref player, int color, const char *msg, int pueblo);

/**
 * Internal notification function (used by raw_notify variants)
 * @param player Player to notify
 * @param msg Message to send
 * @param color 1 to parse colors, 0 to strip
 */
void raw_notify_internal(dbref player, char *msg, int color);

/* ===================================================================
 * Input Processing (input_handler.c)
 * =================================================================== */

/**
 * Process input from a descriptor's socket
 * @param d Descriptor to read from
 * @return 1 on success, 0 on error/disconnect
 */
int process_input(struct descriptor_data *d);

/**
 * Process all queued commands for all descriptors
 * Called from main server loop
 */
void process_commands(void);

/**
 * Execute a command from a descriptor
 * @param d Descriptor issuing command
 * @param command Command string to execute
 * @return 1 to continue, 0 to disconnect
 */
int do_command(struct descriptor_data *d, char *command);

/**
 * Save a command to a descriptor's input queue
 * @param d Descriptor
 * @param command Command string to save
 */
void save_command(struct descriptor_data *d, const char *command);

/**
 * Set a user string (prefix/suffix) for a descriptor
 * @param userstring Pointer to string pointer to set
 * @param command New value (will be copied)
 */
void set_userstring(char **userstring, const char *command);

/* ===================================================================
 * Connection Handling (connection_handler.c)
 * =================================================================== */

/**
 * Parse a connection command into components
 * @param msg Input message
 * @param command Output: command (connect/create)
 * @param user Output: username
 * @param pass Output: password
 */
void parse_connect(const char *msg, char *command, char *user, char *pass);

/**
 * Check and process a connection attempt
 * @param d Descriptor attempting connection
 * @param msg Connection message
 */
void check_connect(struct descriptor_data *d, char *msg);

/* ===================================================================
 * Idle Management (idle_monitor.c)
 * =================================================================== */

/**
 * Check if a descriptor is idle
 * @param d Descriptor to check
 * @return 1 if idle, 0 if not
 */
int des_idle(struct descriptor_data *d);

/**
 * Check all descriptors for idle timeout (internal version)
 * @param player Specific player to check, or -1 for all
 * @param msg Message to log with idle state
 */
void check_for_idlers_int(dbref player, char *msg);

/**
 * Check for players who should be un-idled due to reconnection
 * @param player Player who reconnected
 */
void check_for_connect_unidlers(dbref player);

/**
 * Check if player should be marked idle due to disconnect
 * @param player Player who disconnected
 */
void check_for_disconnect_idlers(dbref player);

/* ===================================================================
 * Timing Utilities (server_main.c)
 * =================================================================== */

/**
 * Subtract two timevals
 * @param now Current time
 * @param then Earlier time
 * @return Difference as timeval
 */
struct timeval timeval_sub(struct timeval now, struct timeval then);

/**
 * Get millisecond difference between two timevals
 * @param now Current time
 * @param then Earlier time
 * @return Difference in milliseconds
 */
int msec_diff(struct timeval now, struct timeval then);

/**
 * Add milliseconds to a timeval
 * @param t Base time
 * @param x Milliseconds to add
 * @return New timeval
 */
struct timeval msec_add(struct timeval t, int x);

/**
 * Update command quotas for all descriptors
 * @param last Last update time
 * @param current Current time
 * @return New last update time
 */
struct timeval update_quotas(struct timeval last, struct timeval current);

/* ===================================================================
 * Global State (shared across modules)
 * =================================================================== */

/* Need more processing flag for main loop */
extern int need_more_proc;

/* Null device for file descriptor reservation */
extern const char *NullFile;

/* ===================================================================
 * External Functions (from other subsystems)
 * These should be in externs.h but are declared here for completeness
 * =================================================================== */

/* Mail system functions */
void init_mail(void);
void free_mail(void);

/* Idle monitoring public interface */
void check_for_idlers(void);



void add_to_queue(struct text_queue *, const char *, int);

#endif /* IO_INTERNAL_H */
