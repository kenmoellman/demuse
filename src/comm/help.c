/* help.c - Help system and text file display
 * Located in comm/ directory
 * 
 * This file contains the help system which displays indexed text files.
 * Supports help, news, and other text-based information systems.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "help.h"

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static void display_help_entry(dbref player, const char *topic, 
                               const char *default_topic,
                               const char *indxfile, const char *textfile,
                               ATTR *trigger);

/* ===================================================================
 * Input Validation
 * =================================================================== */

/**
 * Validate text file name to prevent directory traversal attacks
 * @param filename Filename to validate
 * @return 1 if valid, 0 if invalid
 */
static int is_valid_text_filename(const char *filename)
{
    size_t len;
    size_t i;

    if (!filename || !*filename) {
        return 0;
    }

    len = strlen(filename);

    /* Check length bounds */
    if (len < 1 || len > 32) {
        return 0;
    }

    /* Check for directory traversal attempts */
    if (strchr(filename, '/') || strchr(filename, '\\') ||
        strchr(filename, ':')) {
        return 0;
    }

    /* Check for embedded null bytes (not including terminator) */
    if (memchr(filename, '\0', len) != NULL) {
        return 0;
    }

    /* Ensure all characters are alphanumeric, underscore, or hyphen */
    for (i = 0; i < len; i++) {
        if (!isalnum((unsigned char)filename[i]) &&
            filename[i] != '_' && filename[i] != '-') {
            return 0;
        }
    }

    return 1;
}

/* ===================================================================
 * Text File Commands
 * =================================================================== */

/**
 * TEXT command - display content from a text file bundle
 * @param player Player requesting text
 * @param arg1 Text file bundle name
 * @param arg2 Topic to look up
 * @param trig Attribute to trigger after display
 */
void do_text(dbref player, char *arg1, char *arg2, ATTR *trig)
{
    char indxfile[64];
    char textfile[64];
    
    /* Validate input */
    if (!arg1 || !*arg1) {
        notify(player, "You must specify a text file.");
        return;
    }
    
    /* Security check: validate filename */
    if (!is_valid_text_filename(arg1)) {
        notify(player, "Invalid text file name.");
        log_security(tprintf("SECURITY: %s (#%" DBREF_FMT ") attempted invalid text file: %s (len=%zu)",
                            db[player].name, player, arg1, strlen(arg1)));
        return;
    }
    
    /* Build file paths */
    snprintf(indxfile, sizeof(indxfile), "msgs/%sindx", arg1);
    snprintf(textfile, sizeof(textfile), "msgs/%stext", arg1);
    
    /* Display the help entry */
    display_help_entry(player, arg2, arg1, indxfile, textfile, trig);
}

/* ===================================================================
 * Help System Implementation
 * =================================================================== */

/**
 * Display a help entry from indexed help file
 * @param player Player to display to
 * @param topic Topic to look up (or empty for default)
 * @param default_topic Default topic if none specified
 * @param indxfile Path to index file
 * @param textfile Path to text file
 * @param trigger Attribute to trigger after display (or NULL)
 */
static void display_help_entry(dbref player, const char *topic,
                               const char *default_topic,
                               const char *indxfile, const char *textfile,
                               ATTR *trigger)
{
    FILE *fp_indx = NULL;
    FILE *fp_text = NULL;
    help_indx entry;
    char line[LINE_SIZE + 1];
    char header[128];
    int found = 0;
    int header_len;
    int padding;
    char *p;
    
    /* Use default topic if none specified */
    if (!topic || !*topic) {
        topic = default_topic;
    }
    
    /* Open index file */
    fp_indx = fopen(indxfile, "rb");
    if (!fp_indx) {
        if (errno == ENOENT) {
            /* Try to open text file directly */
            fp_text = fopen(textfile, "r");
            if (!fp_text) {
                notify(player, tprintf("No help available for '%s'.", default_topic));
                return;
            }
            notify(player, tprintf("%s is not indexed.", default_topic));
            fclose(fp_text);
            return;
        } else {
            notify(player, tprintf("Error accessing help: %s", strerror(errno)));
            log_error(tprintf("help: fopen(%s): %s", indxfile, strerror(errno)));
            return;
        }
    }
    
    /* Search index for matching topic */
    while (fread(&entry, sizeof(help_indx), 1, fp_indx) == 1) {
        /* Cast away const for string_prefix compatibility */
        if (string_prefix(entry.topic, (char *)topic)) {
            found = 1;
            break;
        }
    }
    
    fclose(fp_indx);
    
    if (!found) {
        notify(player, tprintf("No %s for '%s'.", default_topic, topic));
        return;
    }
    
    /* Open text file */
    fp_text = fopen(textfile, "r");
    if (!fp_text) {
        notify(player, tprintf("%s: temporarily not available.", default_topic));
        log_error(tprintf("help: fopen(%s): %s", textfile, strerror(errno)));
        return;
    }
    
    /* Seek to the help entry position */
    if (fseek(fp_text, entry.pos, SEEK_SET) < 0) {
        notify(player, tprintf("%s: temporarily not available.", default_topic));
        log_error(tprintf("help: fseek(%s, %ld): %s", 
                         textfile, entry.pos, strerror(errno)));
        fclose(fp_text);
        return;
    }
    
    /* Build and display header */
    snprintf(header, sizeof(header), " %s on %s ", default_topic, entry.topic);
    header_len = (int)strlen(header);
    padding = (78 - header_len) / 2;
    
    /* Top border */
    notify(player, "------------------------------------------------------------------------------");
    
    /* Centered title */
    notify(player, tprintf("%*s%s%*s",
                          padding, "", header, 
                          78 - padding - header_len, ""));
    
    /* Display help text */
    while (fgets(line, sizeof(line), fp_text)) {
        /* Check for end marker */
        if (line[0] == '&') {
            break;
        }
        
        /* Remove trailing newline */
        p = strchr(line, '\n');
        if (p) {
            *p = '\0';
        }
        
        /* Send line to player */
        notify(player, line);
    }
    
    /* Bottom border */
    notify(player, "------------------------------------------------------------------------------");
    
    fclose(fp_text);
    
    /* Trigger attribute if specified */
    if (trigger) {
        dbref zone;
        
        /* Set up word parameter */
        wptr[0] = entry.topic;
        
        /* Trigger on player */
        did_it(player, player, NULL, NULL, NULL, NULL, trigger);
        
        /* Trigger on zones */
        DOZONE(zone, player) {
            did_it(player, zone, NULL, NULL, NULL, NULL, trigger);
        }
    }
}

/* ===================================================================
 * MOTD Command
 * =================================================================== */

/**
 * MOTD command - display message of the day
 * @param player Player requesting MOTD
 */
void do_motd(dbref player)
{
    struct descriptor_data *d;
    
    /* Find player's descriptor */
    for (d = descriptor_list; d; d = d->next) {
        if (d->player == player) {
            break;
        }
    }
    
    if (!d) {
        notify(player, "Unable to display MOTD.");
        return;
    }
    
    /* Display the MOTD file */
    connect_message(d, motd_msg_file, 0);
}

/* ===================================================================
 * Help File Utilities
 * =================================================================== */

/**
 * Check if a help topic exists in an index
 * @param indxfile Path to index file
 * @param topic Topic to search for
 * @return 1 if found, 0 if not found
 */
int help_topic_exists(const char *indxfile, const char *topic)
{
    FILE *fp;
    help_indx entry;
    int found = 0;
    
    if (!indxfile || !topic || !*topic) {
        return 0;
    }
    
    fp = fopen(indxfile, "rb");
    if (!fp) {
        return 0;
    }
    
    while (fread(&entry, sizeof(help_indx), 1, fp) == 1) {
        /* Cast away const for string_prefix compatibility */
        if (string_prefix(entry.topic, (char *)topic)) {
            found = 1;
            break;
        }
    }
    
    fclose(fp);
    return found;
}

/**
 * Get list of all help topics from an index
 * @param player Player to display to
 * @param indxfile Path to index file
 */
void list_help_topics(dbref player, const char *indxfile)
{
    FILE *fp;
    help_indx entry;
    int count = 0;
    char buf[BUFFER_LEN];
    char *bp;
    int first = 1;
    size_t space_needed;
    
    if (!indxfile || !*indxfile) {
        notify(player, "Invalid help file.");
        return;
    }
    
    fp = fopen(indxfile, "rb");
    if (!fp) {
        notify(player, "Help index not available.");
        return;
    }
    
    notify(player, "Available topics:");
    buf[0] = '\0';
    bp = buf;
    
    while (fread(&entry, sizeof(help_indx), 1, fp) == 1) {
        space_needed = strlen(entry.topic);
        
        if (!first) {
            space_needed += 2; /* for ", " */
            if ((size_t)(bp - buf) + space_needed > 78) {
                notify(player, buf);
                buf[0] = '\0';
                bp = buf;
                first = 1;
            }
        }
        
        if (!first) {
            *bp++ = ',';
            *bp++ = ' ';
        } else {
            first = 0;
        }
        
        strcpy(bp, entry.topic);
        bp += strlen(entry.topic);
        count++;
    }
    
    if (buf[0]) {
        notify(player, buf);
    }
    
    notify(player, tprintf("Total topics: %d", count));
    fclose(fp);
}

/**
 * Search help topics for a pattern
 * @param player Player requesting search
 * @param indxfile Path to index file
 * @param pattern Pattern to search for
 */
void search_help_topics(dbref player, const char *indxfile, const char *pattern)
{
    FILE *fp;
    help_indx entry;
    int count = 0;
    
    if (!indxfile || !pattern || !*pattern) {
        notify(player, "Usage: helpsearch <pattern>");
        return;
    }
    
    fp = fopen(indxfile, "rb");
    if (!fp) {
        notify(player, "Help index not available.");
        return;
    }
    
    notify(player, tprintf("Topics matching '%s':", pattern));
    
    while (fread(&entry, sizeof(help_indx), 1, fp) == 1) {
        if (string_match(entry.topic, (char *)pattern)) {
            notify(player, tprintf("  %s", entry.topic));
            count++;
        }
    }
    
    if (count == 0) {
        notify(player, "No matching topics found.");
    } else {
        notify(player, tprintf("Found %d matching topic%s.", 
                              count, count == 1 ? "" : "s"));
    }
    
    fclose(fp);
}
