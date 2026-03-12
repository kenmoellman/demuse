/* help.c - Help system and MOTD display
 * Located in comm/ directory
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Help is backed by MariaDB instead of flat text files.
 *
 * Help system:
 *   Two-level hierarchy: command + subcommand.
 *   "help +channel" shows overview + lists subcommands (join, leave, etc.)
 *   "help +channel join" shows specific subcommand help.
 *   "help" with no args shows general help.
 *
 * News system:
 *   Moved to messaging.c — news articles are stored in the board table
 *   with board_room = NEWS_ROOM (-2), sharing all board infrastructure.
 *
 * Legacy support:
 *   @text command still reads from flat files (msgs/ directory).
 *   One-time import of helptext/newstext via mariadb_help_import_legacy()
 *   and mariadb_news_import_legacy().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "help.h"
#include "mariadb_help.h"

/* ===================================================================
 * Input Validation (for legacy @text command)
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

    if (len < 1 || len > 32) {
        return 0;
    }

    if (strchr(filename, '/') || strchr(filename, '\\') ||
        strchr(filename, ':')) {
        return 0;
    }

    if (memchr(filename, '\0', len) != NULL) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        if (!isalnum((unsigned char)filename[i]) &&
            filename[i] != '_' && filename[i] != '-') {
            return 0;
        }
    }

    return 1;
}

/* ===================================================================
 * Legacy @text Command (flat-file based)
 * =================================================================== */

/**
 * Display a help entry from indexed help file (legacy flat-file system)
 */
static void display_help_entry(dbref player, const char *topic,
                               const char *default_topic,
                               const char *indxfile, const char *textfile)
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

    if (!topic || !*topic) {
        topic = default_topic;
    }

    fp_indx = fopen(indxfile, "rb");
    if (!fp_indx) {
        if (errno == ENOENT) {
            notify(player, tprintf("No help available for '%s'.", default_topic));
        } else {
            notify(player, tprintf("Error accessing help: %s", strerror(errno)));
            log_error(tprintf("help: fopen(%s): %s", indxfile, strerror(errno)));
        }
        return;
    }

    while (fread(&entry, sizeof(help_indx), 1, fp_indx) == 1) {
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

    fp_text = fopen(textfile, "r");
    if (!fp_text) {
        notify(player, tprintf("%s: temporarily not available.", default_topic));
        log_error(tprintf("help: fopen(%s): %s", textfile, strerror(errno)));
        return;
    }

    if (fseek(fp_text, entry.pos, SEEK_SET) < 0) {
        notify(player, tprintf("%s: temporarily not available.", default_topic));
        log_error(tprintf("help: fseek(%s, %ld): %s",
                         textfile, entry.pos, strerror(errno)));
        fclose(fp_text);
        return;
    }

    snprintf(header, sizeof(header), " %s on %s ", default_topic, entry.topic);
    header_len = (int)strlen(header);
    padding = (78 - header_len) / 2;

    notify(player, "------------------------------------------------------------------------------");
    notify(player, tprintf("%*s%s%*s",
                          padding, "", header,
                          78 - padding - header_len, ""));

    while (fgets(line, sizeof(line), fp_text)) {
        if (line[0] == '&') break;
        p = strchr(line, '\n');
        if (p) *p = '\0';
        notify(player, line);
    }

    notify(player, "------------------------------------------------------------------------------");
    fclose(fp_text);
}

/**
 * TEXT command - display content from a text file bundle (legacy)
 * @param player Player requesting text
 * @param arg1 Text file bundle name
 * @param arg2 Topic to look up
 * @param trig Attribute to trigger after display (unused, kept for API compat)
 */
void do_text(dbref player, char *arg1, char *arg2,
             ATTR *trig __attribute__((unused)))
{
    char indxfile[64];
    char textfile[64];

    if (!arg1 || !*arg1) {
        notify(player, "You must specify a text file.");
        return;
    }

    if (!is_valid_text_filename(arg1)) {
        notify(player, "Invalid text file name.");
        log_security(tprintf("SECURITY: %s (#%" DBREF_FMT ") attempted invalid text file: %s (len=%zu)",
                            db[player].name, player, arg1, strlen(arg1)));
        return;
    }

    snprintf(indxfile, sizeof(indxfile), "msgs/%sindx", arg1);
    snprintf(textfile, sizeof(textfile), "msgs/%stext", arg1);

    display_help_entry(player, arg2, arg1, indxfile, textfile);
}

/* ===================================================================
 * Help Command (MariaDB-backed)
 * =================================================================== */

/**
 * Help command - display help from MariaDB
 *
 * Lookup behavior:
 *   "help"               -> command="help", subcommand=""
 *   "help +channel"      -> command="+channel", subcommand=""
 *   "help +channel join" -> command="+channel", subcommand="join"
 *
 * If the command has subcommand entries, they are always listed
 * after the main help text.
 *
 * @param player Player requesting help
 * @param arg1   Full help argument string (e.g., "+channel join")
 */
void do_help(dbref player, char *arg1)
{
    char command[64];
    char subcommand[64];
    char *body = NULL;
    char *sublist = NULL;
    int subcmd_count = 0;
    char header[128];
    int header_len, padding;

    /* Parse arg1 into command and subcommand */
    command[0] = '\0';
    subcommand[0] = '\0';

    if (!arg1 || !*arg1) {
        /* "help" with no args */
        strncpy(command, "help", sizeof(command) - 1);
        command[sizeof(command) - 1] = '\0';
    } else {
        /* Split on first space: "help +channel join" -> "+channel" + "join" */
        char *space = strchr(arg1, ' ');
        if (space) {
            size_t cmd_len = (size_t)(space - arg1);
            if (cmd_len >= sizeof(command)) {
                cmd_len = sizeof(command) - 1;
            }
            strncpy(command, arg1, cmd_len);
            command[cmd_len] = '\0';

            /* Skip spaces after command */
            char *sub = space + 1;
            while (*sub == ' ') sub++;

            strncpy(subcommand, sub, sizeof(subcommand) - 1);
            subcommand[sizeof(subcommand) - 1] = '\0';

            /* Trim trailing spaces from subcommand */
            size_t slen = strlen(subcommand);
            while (slen > 0 && subcommand[slen - 1] == ' ') {
                subcommand[--slen] = '\0';
            }
        } else {
            strncpy(command, arg1, sizeof(command) - 1);
            command[sizeof(command) - 1] = '\0';
        }
    }

    /* Convert to lowercase for case-insensitive lookup */
    {
        char *p;
        for (p = command; *p; p++) *p = (char)tolower((unsigned char)*p);
        for (p = subcommand; *p; p++) *p = (char)tolower((unsigned char)*p);
    }

    /* Look up the topic */
    int found = mariadb_help_get(command, subcommand, &body);

    if (!found && subcommand[0]) {
        /* Try the full string as a single command (e.g., "help @create") */
        char full[128];
        snprintf(full, sizeof(full), "%s %s", command, subcommand);
        found = mariadb_help_get(full, "", &body);
        if (found) {
            /* Matched as a single command, adjust for display */
            strncpy(command, full, sizeof(command) - 1);
            command[sizeof(command) - 1] = '\0';
            subcommand[0] = '\0';
        }
    }

    /* Check for subcommands */
    mariadb_help_list_subcommands(command, &sublist, &subcmd_count);

    if (!found && subcmd_count == 0) {
        notify(player, tprintf("No help available for '%s'.", arg1 && *arg1 ? arg1 : "help"));
        return;
    }

    /* Display header */
    if (subcommand[0]) {
        snprintf(header, sizeof(header), " help on %s %s ", command, subcommand);
    } else {
        snprintf(header, sizeof(header), " help on %s ", command);
    }
    header_len = (int)strlen(header);
    padding = (78 - header_len) / 2;
    if (padding < 0) padding = 0;

    notify(player, "------------------------------------------------------------------------------");
    notify(player, tprintf("%*s%s%*s",
                          padding, "", header,
                          78 - padding - header_len, ""));

    /* Display body text */
    if (found && body) {
        /* Send line by line so notify can process each line */
        char *line_start = body;
        char *newline;

        while ((newline = strchr(line_start, '\n')) != NULL) {
            *newline = '\0';
            notify(player, line_start);
            line_start = newline + 1;
        }
        /* Send any remaining text after last newline */
        if (*line_start) {
            notify(player, line_start);
        }
    } else if (subcmd_count > 0) {
        notify(player, tprintf("No overview available for '%s'.", command));
    }

    /* Always show subcommand list if any exist */
    if (subcmd_count > 0 && subcommand[0] == '\0') {
        notify(player, "");
        notify(player, tprintf("Additional help topics for %s:", command));
        notify(player, tprintf("  %s", sublist));
    }

    notify(player, "------------------------------------------------------------------------------");

    if (body) {
        SMART_FREE(body);
    }
    if (sublist) {
        SMART_FREE(sublist);
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
    if (!motd_msg || !*motd_msg) {
        notify(player, "No MOTD set.");
        return;
    }

    notify(player, motd_msg);
}
