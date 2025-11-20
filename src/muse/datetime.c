/* datetime.c - Centralized date and time functions
 * Located in comm/ directory
 * 
 * This file contains all date/time formatting and parsing functions.
 * All date/time operations should go through these standardized functions.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"

/* ===================================================================
 * Constants
 * =================================================================== */

#undef HAVE_STRPTIME
#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_HOUR   3600
#define SECONDS_PER_DAY    86400
#define SECONDS_PER_WEEK   604800

#define MAX_TIME_STRING    256

/* Day and month names */
static const char *day_names[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

static const char *month_names[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char *month_abbrev[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* ===================================================================
 * Utility Functions
 * =================================================================== */

/**
 * Safely get tm structure with bounds checking
 * @param t Time value
 * @param use_local 1 for localtime, 0 for gmtime
 * @return Pointer to static tm struct, or NULL on error
 */
static struct tm *safe_localtime(time_t t, int use_local)
{
    struct tm *result;
    
    /* Sanity check time value */
    if (t < 0 || t > 2147483647) {  /* Year 2038 problem awareness */
        return NULL;
    }
    
    if (use_local) {
        result = localtime(&t);
    } else {
        result = gmtime(&t);
    }
    
    return result;
}

/**
 * Safe string allocation wrapper
 * @param str String to allocate
 * @return Allocated string (caller must manage)
 */
static char *safe_stralloc(const char *str)
{
    if (!str) {
        return stralloc("");
    }
    return stralloc(str);
}

/* ===================================================================
 * Basic Time Formatting Functions
 * =================================================================== */

/**
 * Get the day of the week as a string
 * @param day Time value
 * @return Day name string ("Monday", "Tuesday", etc.)
 */
char *get_day(time_t day)
{
    struct tm *tm_info;
    
    if (day < 0) {
        return safe_stralloc("Unknown");
    }
    
    tm_info = safe_localtime(day, 1);
    if (!tm_info || tm_info->tm_wday < 0 || tm_info->tm_wday > 6) {
        return safe_stralloc("Unknown");
    }
    
    return safe_stralloc(day_names[tm_info->tm_wday]);
}

/**
 * Convert military time to standard am/pm notation
 * @param day Time value  
 * @return "am" or "pm" string
 */
char *mil_to_stndrd(time_t day)
{
    struct tm *tm_info;
    
    if (day < 0) {
        return safe_stralloc("am");
    }
    
    tm_info = safe_localtime(day, 1);
    if (!tm_info) {
        return safe_stralloc("am");
    }
    
    return safe_stralloc((tm_info->tm_hour >= 12) ? "pm" : "am");
}

/**
 * Format time duration as weeks/days HH:MM
 * Examples: "2w 14:30", "3d 08:15", "14:30"
 * @param dt Time difference in seconds
 * @return Formatted duration string
 */
char *time_format_1(time_t dt)
{
    long weeks, days, hours, minutes;
    char buf[MAX_TIME_STRING];
    
    /* Handle negative or zero times */
    if (dt <= 0) {
        return safe_stralloc("00:00");
    }
    
    /* Calculate components */
    weeks = dt / SECONDS_PER_WEEK;
    dt %= SECONDS_PER_WEEK;
    
    days = dt / SECONDS_PER_DAY;
    dt %= SECONDS_PER_DAY;
    
    hours = dt / SECONDS_PER_HOUR;
    dt %= SECONDS_PER_HOUR;
    
    minutes = dt / SECONDS_PER_MINUTE;
    
    /* Format based on magnitude */
    if (weeks > 0) {
        snprintf(buf, sizeof(buf), "%ldw %02ld:%02ld", weeks, hours, minutes);
    } else if (days > 0) {
        snprintf(buf, sizeof(buf), "%ldd %02ld:%02ld", days, hours, minutes);
    } else {
        snprintf(buf, sizeof(buf), "%02ld:%02ld", hours, minutes);
    }
    
    return safe_stralloc(buf);
}

/**
 * Format time as single largest unit
 * Examples: "2w", "3d", "5h", "30m", "45s"
 * @param dt Time difference in seconds
 * @return Single unit duration string
 */
char *time_format_2(time_t dt)
{
    char buf[MAX_TIME_STRING];
    
    /* Handle negative times */
    if (dt < 0) {
        dt = -dt;
    }
    
    if (dt == 0) {
        return safe_stralloc("0s");
    }
    
    /* Return largest applicable unit */
    if (dt >= SECONDS_PER_WEEK) {
        snprintf(buf, sizeof(buf), "%ldw", dt / SECONDS_PER_WEEK);
    } else if (dt >= SECONDS_PER_DAY) {
        snprintf(buf, sizeof(buf), "%ldd", dt / SECONDS_PER_DAY);
    } else if (dt >= SECONDS_PER_HOUR) {
        snprintf(buf, sizeof(buf), "%ldh", dt / SECONDS_PER_HOUR);
    } else if (dt >= SECONDS_PER_MINUTE) {
        snprintf(buf, sizeof(buf), "%ldm", dt / SECONDS_PER_MINUTE);
    } else {
        snprintf(buf, sizeof(buf), "%lds", dt);
    }
    
    return safe_stralloc(buf);
}

/**
 * Format time in human-readable form with proper singular/plural
 * Examples: "3 days", "an hour", "2 minutes"
 * @param dt Time difference in seconds
 * @return Human-readable duration string
 */
char *time_format_3(time_t dt)
{
    long weeks, days, hours, minutes, seconds;
    char buf[MAX_TIME_STRING];
    
    /* Handle negative times */
    if (dt < 0) {
        dt = -dt;
    }
    
    if (dt == 0) {
        return safe_stralloc("no time");
    }
    
    /* Calculate components */
    weeks = dt / SECONDS_PER_WEEK;
    dt %= SECONDS_PER_WEEK;
    
    days = dt / SECONDS_PER_DAY;
    dt %= SECONDS_PER_DAY;
    
    hours = dt / SECONDS_PER_HOUR;
    dt %= SECONDS_PER_HOUR;
    
    minutes = dt / SECONDS_PER_MINUTE;
    seconds = dt % SECONDS_PER_MINUTE;
    
    /* Format with proper singular/plural */
    if (weeks > 1) {
        snprintf(buf, sizeof(buf), "%ld weeks", weeks);
    } else if (weeks == 1) {
        snprintf(buf, sizeof(buf), "a week");
    } else if (days > 1) {
        snprintf(buf, sizeof(buf), "%ld days", days);
    } else if (days == 1) {
        snprintf(buf, sizeof(buf), "a day");
    } else if (hours > 1) {
        snprintf(buf, sizeof(buf), "%ld hours", hours);
    } else if (hours == 1) {
        snprintf(buf, sizeof(buf), "an hour");
    } else if (minutes > 1) {
        snprintf(buf, sizeof(buf), "%ld minutes", minutes);
    } else if (minutes == 1) {
        snprintf(buf, sizeof(buf), "a minute");
    } else if (seconds != 1) {
        snprintf(buf, sizeof(buf), "%ld seconds", seconds);
    } else {
        snprintf(buf, sizeof(buf), "a second");
    }
    
    return safe_stralloc(buf);
}

/**
 * Format time with full detail
 * Example: "2 weeks, 3 days, 4 hours, and 30 minutes"
 * @param dt Time difference in seconds
 * @return Detailed duration string
 */
char *time_format_4(time_t dt)
{
    long weeks, days, hours, minutes;
    char buf[MAX_TIME_STRING];
    char *p;
    size_t remaining;
    int components = 0;
    
    /* Handle negative times */
    if (dt < 0) {
        dt = -dt;
    }
    
    if (dt == 0) {
        return safe_stralloc("no time");
    }
    
    /* Calculate components */
    weeks = dt / SECONDS_PER_WEEK;
    dt %= SECONDS_PER_WEEK;
    
    days = dt / SECONDS_PER_DAY;
    dt %= SECONDS_PER_DAY;
    
    hours = dt / SECONDS_PER_HOUR;
    dt %= SECONDS_PER_HOUR;
    
    minutes = dt / SECONDS_PER_MINUTE;
    
    /* Build string */
    p = buf;
    remaining = sizeof(buf);
    
    if (weeks > 0) {
        if (weeks == 1) {
            snprintf(p, remaining, "a week");
        } else {
            snprintf(p, remaining, "%ld weeks", weeks);
        }
        p += strlen(p);
        remaining = sizeof(buf) - (size_t)(p - buf);
        components++;
    }
    
    if (days > 0) {
        if (components > 0) {
            snprintf(p, remaining, ", ");
            p += 2;
            remaining -= 2;
        }
        if (days == 1) {
            snprintf(p, remaining, "a day");
        } else {
            snprintf(p, remaining, "%ld days", days);
        }
        p += strlen(p);
        remaining = sizeof(buf) - (size_t)(p - buf);
        components++;
    }
    
    if (hours > 0) {
        if (components > 0) {
            snprintf(p, remaining, ", ");
            p += 2;
            remaining -= 2;
        }
        if (hours == 1) {
            snprintf(p, remaining, "an hour");
        } else {
            snprintf(p, remaining, "%ld hours", hours);
        }
        p += strlen(p);
        remaining = sizeof(buf) - (size_t)(p - buf);
        components++;
    }
    
    if (minutes > 0) {
        if (components > 1) {
            snprintf(p, remaining, ", and ");
            p += 6;
            remaining -= 6;
        } else if (components == 1) {
            snprintf(p, remaining, " and ");
            p += 5;
            remaining -= 5;
        }
        if (minutes == 1) {
            snprintf(p, remaining, "a minute");
        } else {
            snprintf(p, remaining, "%ld minutes", minutes);
        }
        components++;
    }
    
    /* If nothing was added, show seconds */
    if (components == 0) {
        long seconds = dt % SECONDS_PER_MINUTE;
        if (seconds == 1) {
            snprintf(buf, sizeof(buf), "a second");
        } else {
            snprintf(buf, sizeof(buf), "%ld seconds", seconds);
        }
    }
    
    return safe_stralloc(buf);
}

/**
 * Format time as HH:MM:SS timestamp
 * @param dt Time value (not difference)
 * @return Time stamp string
 */
char *time_stamp(time_t dt)
{
    struct tm *tm_info;
    char buf[MAX_TIME_STRING];
    
    if (dt < 0) {
        return safe_stralloc("00:00:00");
    }
    
    tm_info = safe_localtime(dt, 1);
    if (!tm_info) {
        return safe_stralloc("00:00:00");
    }
    
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    return safe_stralloc(buf);
}

/* ===================================================================
 * Advanced Date/Time Formatting
 * =================================================================== */

/**
 * Format a time value with timezone awareness
 * @param t Time value to format
 * @param tz_name Timezone name (or NULL for player's timezone)
 * @param player Player for timezone lookup
 * @return Formatted time string
 */
char *mktm(time_t t, char *tz_name, dbref player)
{
    struct tm *tm_info;
    char buf[MAX_TIME_STRING];
    const char *format_str;
    char old_tz[256] = {0};
    const char *player_tz = NULL;
    int need_restore_tz = 0;
    
    /* Validate input */
    if (t < 0) {
        return safe_stralloc("Invalid time");
    }
    
    /* Get player's timezone if specified */
    if (!tz_name && player != NOTHING && player >= 0 && player < db_top) {
        player_tz = atr_get(player, A_TZ);
        if (player_tz && *player_tz) {
            tz_name = (char *)player_tz;
        }
    }
    
    /* Save and set timezone if specified */
    if (tz_name && *tz_name) {
        const char *current_tz = getenv("TZ");
        if (current_tz) {
            strncpy(old_tz, current_tz, sizeof(old_tz) - 1);
            old_tz[sizeof(old_tz) - 1] = '\0';
            need_restore_tz = 1;
        }
        setenv("TZ", tz_name, 1);
        tzset();
    }
    
    /* Get time structure */
    tm_info = safe_localtime(t, 1);
    if (!tm_info) {
        if (need_restore_tz) {
            setenv("TZ", old_tz, 1);
            tzset();
        }
        return safe_stralloc("Invalid time");
    }
    
    /* Determine format string */
    if (tz_name && *tz_name == 'D') {
        /* Date only */
        format_str = "%a %b %d %Y";
    } else if (tz_name && *tz_name == 'T') {
        /* Time only */
        format_str = "%I:%M %p";
    } else {
        /* Full date and time */
        format_str = "%a %b %d %Y %I:%M %p %Z";
    }
    
    /* Format the time */
    if (strftime(buf, sizeof(buf), format_str, tm_info) == 0) {
        strncpy(buf, "Format error", sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }
    
    /* Restore timezone */
    if (need_restore_tz) {
        setenv("TZ", old_tz, 1);
        tzset();
    }
    
    return safe_stralloc(buf);
}

/**
 * Parse a time string and convert to time_t
 * @param str Time string to parse
 * @param player Player for timezone
 * @param tz_name Timezone name
 * @return time_t value, or -1 on error
 */
long mkxtime(char *str, dbref player, char *tz_name)
{
    struct tm tm_parsed;
    time_t result;
    char old_tz[256] = {0};
    const char *player_tz = NULL;
    int need_restore_tz = 0;
    int parsed = 0;
    
    if (!str || !*str) {
        return -1;
    }
    
    /* Initialize tm structure */
    memset(&tm_parsed, 0, sizeof(tm_parsed));
    tm_parsed.tm_isdst = -1;  /* Let mktime determine DST */
    
    /* Get player's timezone if specified */
    if (!tz_name && player != NOTHING && player >= 0 && player < db_top) {
        player_tz = atr_get(player, A_TZ);
        if (player_tz && *player_tz) {
            tz_name = (char *)player_tz;
        }
    }
    
    /* Save and set timezone if specified */
    if (tz_name && *tz_name) {
        const char *current_tz = getenv("TZ");
        if (current_tz) {
            strncpy(old_tz, current_tz, sizeof(old_tz) - 1);
            old_tz[sizeof(old_tz) - 1] = '\0';
            need_restore_tz = 1;
        }
        setenv("TZ", tz_name, 1);
        tzset();
    }
    
#ifdef HAVE_STRPTIME
    /* Try to parse common formats using strptime if available */
    if (strptime(str, "%Y-%m-%d %H:%M:%S", &tm_parsed) != NULL ||
        strptime(str, "%Y/%m/%d %H:%M:%S", &tm_parsed) != NULL ||
        strptime(str, "%m/%d/%Y %I:%M %p", &tm_parsed) != NULL ||
        strptime(str, "%m/%d/%Y", &tm_parsed) != NULL) {
        parsed = 1;
    }
#else
    /* Manual parsing for common formats */
    /* Try YYYY-MM-DD HH:MM:SS format */
    if (sscanf(str, "%d-%d-%d %d:%d:%d",
               &tm_parsed.tm_year, &tm_parsed.tm_mon, &tm_parsed.tm_mday,
               &tm_parsed.tm_hour, &tm_parsed.tm_min, &tm_parsed.tm_sec) == 6) {
        tm_parsed.tm_year -= 1900;
        tm_parsed.tm_mon -= 1;
        parsed = 1;
    }
    /* Try YYYY/MM/DD HH:MM:SS format */
    else if (sscanf(str, "%d/%d/%d %d:%d:%d",
                    &tm_parsed.tm_year, &tm_parsed.tm_mon, &tm_parsed.tm_mday,
                    &tm_parsed.tm_hour, &tm_parsed.tm_min, &tm_parsed.tm_sec) == 6) {
        tm_parsed.tm_year -= 1900;
        tm_parsed.tm_mon -= 1;
        parsed = 1;
    }
    /* Try MM/DD/YYYY format */
    else if (sscanf(str, "%d/%d/%d",
                    &tm_parsed.tm_mon, &tm_parsed.tm_mday, &tm_parsed.tm_year) == 3) {
        tm_parsed.tm_mon -= 1;
        if (tm_parsed.tm_year < 100) {
            tm_parsed.tm_year += (tm_parsed.tm_year < 70) ? 2000 : 1900;
        }
        tm_parsed.tm_year -= 1900;
        tm_parsed.tm_hour = 0;
        tm_parsed.tm_min = 0;
        tm_parsed.tm_sec = 0;
        parsed = 1;
    }
#endif
    
    if (parsed) {
        /* Validate parsed values */
        if (tm_parsed.tm_year < 0 || tm_parsed.tm_year > 200 ||
            tm_parsed.tm_mon < 0 || tm_parsed.tm_mon > 11 ||
            tm_parsed.tm_mday < 1 || tm_parsed.tm_mday > 31 ||
            tm_parsed.tm_hour < 0 || tm_parsed.tm_hour > 23 ||
            tm_parsed.tm_min < 0 || tm_parsed.tm_min > 59 ||
            tm_parsed.tm_sec < 0 || tm_parsed.tm_sec > 61) {
            result = -1;
        } else {
            result = mktime(&tm_parsed);
        }
    } else {
        result = -1;
    }
    
    /* Restore timezone */
    if (need_restore_tz) {
        setenv("TZ", old_tz, 1);
        tzset();
    }
    
    return result;
}

/* ===================================================================
 * Date Calculation Functions
 * =================================================================== */

/**
 * Calculate the difference between two dates
 * @param start Start time
 * @param end End time
 * @return Difference in seconds
 */
time_t date_diff(time_t start, time_t end)
{
    if (end < start) {
        return 0;
    }
    return end - start;
}

/**
 * Check if a year is a leap year
 * @param year Year to check
 * @return 1 if leap year, 0 otherwise
 */
int is_leap_year(int year)
{
    if (year % 400 == 0) return 1;
    if (year % 100 == 0) return 0;
    if (year % 4 == 0) return 1;
    return 0;
}

/**
 * Get the number of days in a month
 * @param month Month (1-12)
 * @param year Year (for leap year calculation)
 * @return Number of days in the month
 */
int days_in_month(int month, int year)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    if (month < 1 || month > 12) {
        return 0;
    }
    
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    
    return days[month - 1];
}

/**
 * Format a date in ISO 8601 format
 * @param t Time value
 * @return ISO 8601 formatted string (YYYY-MM-DD HH:MM:SS)
 */
char *format_iso8601(time_t t)
{
    struct tm *tm_info;
    char buf[MAX_TIME_STRING];
    
    if (t < 0) {
        return safe_stralloc("0000-00-00 00:00:00");
    }
    
    tm_info = safe_localtime(t, 0);  /* Use GMT for ISO 8601 */
    if (!tm_info) {
        return safe_stralloc("0000-00-00 00:00:00");
    }
    
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
            tm_info->tm_year + 1900,
            tm_info->tm_mon + 1,
            tm_info->tm_mday,
            tm_info->tm_hour,
            tm_info->tm_min,
            tm_info->tm_sec);
    
    return safe_stralloc(buf);
}

/**
 * Get current time as a formatted string
 * @param format Format type (0=full, 1=date only, 2=time only)
 * @return Formatted current time
 */
char *current_time_string(int format)
{
    time_t now_time;
    
    time(&now_time);
    
    switch (format) {
        case 1:  /* Date only */
            return mktm(now_time, "D|", NOTHING);
        case 2:  /* Time only */
            return mktm(now_time, "T|", NOTHING);
        default:  /* Full */
            return mktm(now_time, NULL, NOTHING);
    }
}
