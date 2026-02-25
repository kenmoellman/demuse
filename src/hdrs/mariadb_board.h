/* mariadb_board.h - MariaDB-backed public board operations
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Provides SQL operations for the public board posting system.
 * All queries go directly to MariaDB - no in-memory cache.
 *
 * Uses the MAIL_RESULT struct from mariadb_mail.h (recipient field
 * holds board_room for board posts).
 *
 * All code is conditionally compiled with #ifdef USE_MARIADB.
 * Stubs return failure values when MariaDB is not available.
 */

#ifndef _MARIADB_BOARD_H_
#define _MARIADB_BOARD_H_

#include "config.h"
#include "mariadb_mail.h"  /* For MAIL_RESULT and mail_list_callback */

#ifdef USE_MARIADB

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/*
 * mariadb_board_init - Create the board table if it doesn't exist
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_board_init(void);

/*
 * mariadb_board_post - Insert a new board post
 *
 * PARAMETERS:
 *   author     - dbref of poster
 *   board_room - dbref of board room (typically default_room)
 *   message    - Message text
 *   flags      - Initial flags (typically MF_READ for board posts)
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_board_post(dbref author, dbref board_room, const char *message,
                       int flags);

/*
 * mariadb_board_get_by_position - Get a board post by position number
 *
 * Position is 1-based, ordered by post_id (oldest first).
 * Only non-deleted posts are counted unless include_deleted is true.
 *
 * PARAMETERS:
 *   board_room      - dbref of board room
 *   postnum         - 1-based post position
 *   include_deleted - Whether to include deleted posts in numbering
 *   out             - Output: filled with post data (caller frees message)
 *
 * RETURNS: 1 if found, 0 if not found or error
 */
int mariadb_board_get_by_position(dbref board_room, long postnum,
                                  int include_deleted, MAIL_RESULT *out);

/*
 * mariadb_board_update_flags - Update flags for a specific post by ID
 *
 * PARAMETERS:
 *   post_id - Primary key of the post
 *   flags   - New flags value
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_board_update_flags(long post_id, int flags);

/*
 * mariadb_board_delete_range - Mark/unmark posts as deleted by position
 *
 * PARAMETERS:
 *   board_room - dbref of board room
 *   start      - Start position (1-based)
 *   end        - End position (1-based), or 0 for single post
 *   undelete   - If true, clear MF_DELETED instead
 *   player     - dbref of player performing the action (for permission check)
 *
 * RETURNS: Number of posts affected
 */
long mariadb_board_delete_range(dbref board_room, long start, long end,
                                int undelete, dbref player);

/*
 * mariadb_board_purge - Permanently remove deleted posts
 *
 * PARAMETERS:
 *   board_room - dbref of board room
 *   player     - dbref of player performing purge (for permission check)
 *
 * RETURNS: Number of posts purged
 */
long mariadb_board_purge(dbref board_room, dbref player);

/*
 * mariadb_board_list - Iterate over posts with callback
 *
 * PARAMETERS:
 *   board_room      - dbref of board room
 *   include_deleted - Whether to include deleted posts
 *   callback        - Function called for each post
 *   userdata        - Passed through to callback
 *
 * RETURNS: Number of posts iterated
 */
long mariadb_board_list(dbref board_room, int include_deleted,
                        mail_list_callback callback, void *userdata);

/*
 * mariadb_board_count - Count posts on a board
 *
 * PARAMETERS:
 *   board_room      - dbref of board room
 *   include_deleted - Whether to count deleted posts
 *
 * RETURNS: Post count, or -1 on error
 */
long mariadb_board_count(dbref board_room, int include_deleted);

/*
 * mariadb_board_remove_all - Truncate the entire board table
 *
 * WARNING: Destroys all board posts!
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_board_remove_all(void);

/*
 * mariadb_board_stats - Get aggregate statistics for @info mail
 *
 * PARAMETERS:
 *   total_out     - Output: total post count
 *   deleted_out   - Output: deleted post count
 *   text_size_out - Output: total post text bytes
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_board_stats(long *total_out, long *deleted_out,
                        long *text_size_out);

#else /* !USE_MARIADB */

/* ============================================================================
 * STUB FUNCTIONS
 * ============================================================================ */

static inline int mariadb_board_init(void) { return 0; }
static inline int mariadb_board_post(dbref a __attribute__((unused)),
    dbref b __attribute__((unused)), const char *m __attribute__((unused)),
    int f __attribute__((unused))) { return 0; }
static inline int mariadb_board_get_by_position(dbref b __attribute__((unused)),
    long n __attribute__((unused)), int d __attribute__((unused)),
    MAIL_RESULT *o __attribute__((unused))) { return 0; }
static inline int mariadb_board_update_flags(long id __attribute__((unused)),
    int f __attribute__((unused))) { return 0; }
static inline long mariadb_board_delete_range(dbref b __attribute__((unused)),
    long s __attribute__((unused)), long e __attribute__((unused)),
    int u __attribute__((unused)), dbref p __attribute__((unused)))
    { return 0; }
static inline long mariadb_board_purge(dbref b __attribute__((unused)),
    dbref p __attribute__((unused))) { return 0; }
static inline long mariadb_board_list(dbref b __attribute__((unused)),
    int d __attribute__((unused)),
    mail_list_callback c __attribute__((unused)),
    void *u __attribute__((unused))) { return 0; }
static inline long mariadb_board_count(dbref b __attribute__((unused)),
    int d __attribute__((unused))) { return -1; }
static inline int mariadb_board_remove_all(void) { return 0; }
static inline int mariadb_board_stats(long *t __attribute__((unused)),
    long *d __attribute__((unused)), long *s __attribute__((unused)))
    { return 0; }

#endif /* USE_MARIADB */

#endif /* _MARIADB_BOARD_H_ */
