/* datetime.h - Consolidated date and time functions for MUSE
 * 
 * This header defines all date/time related functions that will
 * eventually be consolidated into datetime.c from across the codebase.
 * 
 * All functions return strings allocated via tprintf()->stralloc().
 * The MUD's memory management system handles cleanup of these strings.
 */

#ifndef __DATETIME_H__
#define __DATETIME_H__

/* Required system headers */
#include <sys/types.h>
#include <time.h>

/* ===================================================================
 * Time Formatting Functions (currently implemented)
 * =================================================================== */

/**
 * Get the day of the week as a string
 * @param day Time value
 * @return Day name string ("Monday", "Tuesday", etc.)
 */
char *get_day(time_t day);

/**
 * Convert military time to standard am/pm notation
 * @param day Time value  
 * @return "am" or "pm" string
 */
char *mil_to_stndrd(time_t day);

/**
 * Format time duration as weeks/days HH:MM
 * Examples: "2w 14:30", "3d 08:15", "14:30"
 * @param dt Time difference in seconds
 * @return Formatted duration string
 */
char *time_format_1(time_t dt);

/**
 * Format time as single largest unit
 * Examples: "2w", "3d", "5h", "30m", "45s"
 * @param dt Time difference in seconds
 * @return Single unit duration string
 */
char *time_format_2(time_t dt);

/**
 * Format time in human-readable form with proper singular/plural
 * Examples: "3 days", "an hour", "2 minutes"
 * @param dt Time difference in seconds
 * @return Human-readable duration string
 */
char *time_format_3(time_t dt);

/**
 * Format time with full detail
 * Example: "2 weeks, 3 days, 4 hours, and 30 minutes"
 * @param dt Time difference in seconds
 * @return Detailed duration string
 */
char *time_format_4(time_t dt);

/**
 * Format time as HH:MM:SS timestamp
 * @param dt Time value (not difference)
 * @return Time stamp string
 */
char *time_stamp(time_t dt);

/* ===================================================================
 * Date/Time Functions to be moved here (currently in other files)
 * =================================================================== */

/* From utils.c - to be moved here */
/*
char *mktm(time_t t, char *tz_name, dbref player);
long mkxtime(char *str, dbref player, char *tz_name);
*/

/* From bsd.c or similar - connection time tracking */
/*
char *format_idle_time(time_t idle_secs);
char *format_connection_time(time_t conn_time);
*/

/* From player.c or similar - last login tracking */
/*
char *format_last_login(dbref player);
time_t get_last_login(dbref player);
void set_last_login(dbref player, time_t when);
*/

/* ===================================================================
 * Future Enhancements (not yet implemented)
 * =================================================================== */

/* ISO 8601 date formatting */
/*
char *format_iso8601(time_t t);
time_t parse_iso8601(const char *str);
*/

/* Relative time parsing */
/*
time_t parse_relative_time(const char *str);  // "in 2 hours", "3 days ago"
char *format_relative_time(time_t base, time_t target);
*/

/* Timezone utilities */
/*
char *convert_timezone(time_t t, const char *from_tz, const char *to_tz);
char *get_player_timezone(dbref player);
void set_player_timezone(dbref player, const char *tz);
*/

/* Scheduling utilities */
/*
int matches_cron_pattern(const char *pattern, time_t when);
time_t next_cron_match(const char *pattern, time_t after);
*/

/* Date arithmetic */
/*
time_t add_duration(time_t base, int years, int months, int days, 
                   int hours, int minutes, int seconds);
void diff_dates(time_t start, time_t end, 
                int *years, int *months, int *days,
                int *hours, int *minutes, int *seconds);
*/

#endif /* __DATETIME_H__ */
