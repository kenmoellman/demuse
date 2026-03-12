/* mariadb_news.h - News system operations (wrappers around board table)
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * News articles are stored in the board table with board_room = NEWS_ROOM (-2).
 * This allows news to share all board infrastructure: position-based listing,
 * soft delete, undelete, purge, per-player read tracking via board_read.
 *
 * All functions are thin wrappers around mariadb_board_* with NEWS_ROOM.
 * The NEWS_RESULT struct has been eliminated — everything uses MAIL_RESULT.
 *
 * All code is conditionally compiled with #ifdef USE_MARIADB.
 * Stubs return failure values when MariaDB is not available.
 */

#ifndef _MARIADB_NEWS_H_
#define _MARIADB_NEWS_H_

#include "config.h"
#include "mariadb_board.h"  /* For MAIL_RESULT, mail_list_callback, board API */

#ifdef USE_MARIADB

/* ============================================================================
 * PUBLIC API - Thin wrappers around mariadb_board_* with NEWS_ROOM
 * ============================================================================ */

/*
 * mariadb_news_init - Create news and news_read tables if they don't exist
 *
 * Kept for backward compatibility during migration period.
 * The board table (used by news) is initialized by mariadb_board_init().
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_news_init(void);

/*
 * mariadb_news_post_article - Post a new news article
 *
 * Stores in board table with board_room = NEWS_ROOM.
 *
 * PARAMETERS:
 *   author - dbref of poster
 *   topic  - Article topic/title (stored as subject)
 *   body   - Article body text (stored as message)
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_news_post_article(dbref author, const char *topic, const char *body);

/*
 * mariadb_news_get_by_position - Get a news article by position number
 *
 * Position is 1-based, ordered by post_id (oldest first).
 * Only non-deleted articles are counted unless include_deleted is true.
 *
 * PARAMETERS:
 *   postnum         - 1-based article position
 *   include_deleted - Whether to include deleted articles in numbering
 *   out             - Output: filled with article data (caller frees message)
 *
 * RETURNS: 1 if found, 0 if not found or error
 */
int mariadb_news_get_by_position(long postnum, int include_deleted,
                                  MAIL_RESULT *out);

/*
 * mariadb_news_list_for_player - Iterate over articles with per-player read status
 *
 * PARAMETERS:
 *   player          - dbref of player (for read status lookup)
 *   include_deleted - Whether to include deleted articles
 *   unread_only     - If true, only return unread articles
 *   callback        - Function called for each article
 *   userdata        - Passed through to callback
 *
 * RETURNS: Number of articles iterated
 */
long mariadb_news_list_for_player(dbref player, int include_deleted,
                                   int unread_only, mail_list_callback callback,
                                   void *userdata);

/*
 * mariadb_news_delete_range - Mark/unmark articles as deleted by position
 *
 * PARAMETERS:
 *   start    - Start position (1-based)
 *   end      - End position (1-based), or 0 for single article
 *   undelete - If true, clear MF_DELETED instead
 *   player   - dbref of player performing the action
 *
 * RETURNS: Number of articles affected
 */
long mariadb_news_delete_range(long start, long end, int undelete, dbref player);

/*
 * mariadb_news_purge - Permanently remove deleted articles
 *
 * PARAMETERS:
 *   player - dbref of player performing purge (for permission check)
 *
 * RETURNS: Number of articles purged
 */
long mariadb_news_purge(dbref player);

/*
 * mariadb_news_count - Count total news articles
 *
 * PARAMETERS:
 *   include_deleted - Whether to count deleted articles
 *
 * RETURNS: Article count, or -1 on error
 */
long mariadb_news_count(int include_deleted);

/*
 * mariadb_news_count_unread - Count unread news articles for a player
 *
 * PARAMETERS:
 *   player - dbref of player
 *
 * RETURNS: Unread count, or 0 on error
 */
long mariadb_news_count_unread(dbref player);

/*
 * mariadb_news_mark_read - Mark a news article as read by a player
 *
 * PARAMETERS:
 *   player  - dbref of player
 *   post_id - Board post ID of the article
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_news_mark_read(dbref player, long post_id);

/*
 * mariadb_news_stats - Get aggregate statistics for @info mail
 *
 * PARAMETERS:
 *   total_out     - Output: total article count
 *   deleted_out   - Output: deleted article count
 *   text_size_out - Output: total article text bytes
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_news_stats(long *total_out, long *deleted_out,
                        long *text_size_out);

/*
 * mariadb_news_import_legacy - Import news topics from flat text file
 *
 * Parses the old-format newstext file (& topic_name markers) and inserts
 * each topic as a news article in the board table with NEWS_ROOM.
 *
 * PARAMETERS:
 *   filename - Path to the legacy newstext file
 *
 * RETURNS: Number of articles imported, or -1 on error
 */
int mariadb_news_import_legacy(const char *filename);

#else /* !USE_MARIADB */

/* ============================================================================
 * STUB FUNCTIONS
 * ============================================================================ */

static inline int mariadb_news_init(void) { return 0; }
static inline int mariadb_news_post_article(dbref a __attribute__((unused)),
    const char *t __attribute__((unused)),
    const char *b __attribute__((unused))) { return 0; }
static inline int mariadb_news_get_by_position(long n __attribute__((unused)),
    int d __attribute__((unused)),
    MAIL_RESULT *o __attribute__((unused))) { return 0; }
static inline long mariadb_news_list_for_player(dbref p __attribute__((unused)),
    int d __attribute__((unused)), int u __attribute__((unused)),
    mail_list_callback c __attribute__((unused)),
    void *ud __attribute__((unused))) { return 0; }
static inline long mariadb_news_delete_range(long s __attribute__((unused)),
    long e __attribute__((unused)), int u __attribute__((unused)),
    dbref p __attribute__((unused))) { return 0; }
static inline long mariadb_news_purge(dbref p __attribute__((unused)))
    { return 0; }
static inline long mariadb_news_count(int d __attribute__((unused)))
    { return -1; }
static inline long mariadb_news_count_unread(dbref p __attribute__((unused)))
    { return 0; }
static inline int mariadb_news_mark_read(dbref p __attribute__((unused)),
    long n __attribute__((unused))) { return 0; }
static inline int mariadb_news_stats(long *t __attribute__((unused)),
    long *d __attribute__((unused)), long *s __attribute__((unused)))
    { return 0; }
static inline int mariadb_news_import_legacy(const char *f __attribute__((unused)))
    { return -1; }

#endif /* USE_MARIADB */

#endif /* _MARIADB_NEWS_H_ */
