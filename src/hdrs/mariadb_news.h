/* mariadb_news.h - MariaDB-backed news system operations
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Provides SQL operations for the news article system.
 * News articles have author, date, topic, and body.
 * Read tracking is per-player via the news_read table.
 *
 * All code is conditionally compiled with #ifdef USE_MARIADB.
 * Stubs return failure values when MariaDB is not available.
 */

#ifndef _MARIADB_NEWS_H_
#define _MARIADB_NEWS_H_

#include "config.h"

/* News article result struct */
typedef struct news_result {
    long news_id;
    char *topic;       /* Caller must SMART_FREE */
    char *body;        /* Caller must SMART_FREE */
    dbref author;
    long created_at;   /* Unix timestamp */
} NEWS_RESULT;

#ifdef USE_MARIADB

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/*
 * mariadb_news_init - Create news and news_read tables if they don't exist
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_news_init(void);

/*
 * mariadb_news_post - Post a new news article
 *
 * PARAMETERS:
 *   author - dbref of poster
 *   topic  - Article topic/title
 *   body   - Article body text
 *
 * RETURNS: news_id on success, 0 on failure
 */
long mariadb_news_post(dbref author, const char *topic, const char *body);

/*
 * mariadb_news_get - Get a news article by ID
 *
 * PARAMETERS:
 *   news_id - Article ID
 *   out     - Output: filled with article data (caller frees topic and body)
 *
 * RETURNS: 1 if found, 0 if not found or error
 */
int mariadb_news_get(long news_id, NEWS_RESULT *out);

/*
 * mariadb_news_delete - Remove a news article
 *
 * Also removes all news_read entries for this article (via CASCADE).
 *
 * PARAMETERS:
 *   news_id - Article ID to delete
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_news_delete(long news_id);

/*
 * mariadb_news_count - Count total news articles
 *
 * RETURNS: Total count, or -1 on error
 */
long mariadb_news_count(void);

/*
 * mariadb_news_count_unread - Count unread news articles for a player
 *
 * PARAMETERS:
 *   player - dbref of player
 *
 * RETURNS: Unread count, or -1 on error
 */
long mariadb_news_count_unread(dbref player);

/*
 * mariadb_news_list - List news articles (all or unread only)
 *
 * Builds a formatted list string showing news_id, date, author, topic,
 * and whether the article has been read by the given player.
 *
 * PARAMETERS:
 *   player    - dbref of player (for read status)
 *   unread_only - If true, only show unread articles
 *   list_out  - Output: formatted list (caller must SMART_FREE)
 *   count     - Output: number of articles listed
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_news_list(dbref player, int unread_only, char **list_out,
                      int *count);

/*
 * mariadb_news_mark_read - Mark a news article as read by a player
 *
 * PARAMETERS:
 *   player  - dbref of player
 *   news_id - Article ID
 *
 * RETURNS: 1 on success, 0 on failure
 */
int mariadb_news_mark_read(dbref player, long news_id);

/*
 * mariadb_news_import_legacy - Import news topics from flat text file
 *
 * Parses the old-format newstext file (& topic_name markers) and inserts
 * each topic as a news article. Author is set to player #1.
 * This is a one-time migration function.
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
static inline long mariadb_news_post(dbref a __attribute__((unused)),
    const char *t __attribute__((unused)),
    const char *b __attribute__((unused))) { return 0; }
static inline int mariadb_news_get(long n __attribute__((unused)),
    NEWS_RESULT *o __attribute__((unused))) { return 0; }
static inline int mariadb_news_delete(long n __attribute__((unused)))
    { return 0; }
static inline long mariadb_news_count(void) { return -1; }
static inline long mariadb_news_count_unread(dbref p __attribute__((unused)))
    { return -1; }
static inline int mariadb_news_list(dbref p __attribute__((unused)),
    int u __attribute__((unused)),
    char **l __attribute__((unused)),
    int *c __attribute__((unused))) { return 0; }
static inline int mariadb_news_mark_read(dbref p __attribute__((unused)),
    long n __attribute__((unused))) { return 0; }
static inline int mariadb_news_import_legacy(const char *f __attribute__((unused)))
    { return -1; }

#endif /* USE_MARIADB */

#endif /* _MARIADB_NEWS_H_ */
