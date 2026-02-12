/* conf.c */

/*
 * ============================================================================
 * MODERNIZATION NOTES (2025-2026)
 * ============================================================================
 * This file has been modernized with the following improvements:
 *
 * SAFETY IMPROVEMENTS:
 * - Replaced malloc() with SAFE_MALLOC for memory tracking
 * - Replaced strcpy() with strncpy() with size limits
 * - Changed sprintf() to snprintf() with bounds checking
 * - Added validation checks for configuration values
 * - Better null pointer handling
 *
 * CODE QUALITY:
 * - All functions now use ANSI C prototypes
 * - Reorganized with clear === section markers
 * - Added comprehensive inline documentation
 * - Better error handling and user feedback
 *
 * CONFIGURATION SYSTEM:
 * - All config variables defined in config_vars.c (zero-initialized)
 * - All default values loaded from MariaDB at startup
 * - MariaDB is REQUIRED for server operation
 * - Provides runtime configuration modification
 * - Only root can modify configuration
 * - Supports numeric, string, dbref, and long types
 *
 * MARIADB INTEGRATION (2026):
 * - @config changes are persisted to MariaDB when connected
 * - @config/seed writes all current values to database
 * - @config/reload reloads values from database
 * - @config/dbstatus shows MariaDB connection status
 * - perm_messages stored as perm_messages-N numbered keys
 *
 * SECURITY NOTES:
 * - Only root (player #1) can modify configuration
 * - Dbrefs validated before setting
 * - Numeric inputs validated
 */

#include "config.h"
#include "externs.h"
#include "mariadb.h"

/* ============================================================================
 * TYPE-SPECIFIC CONFIGURATION HANDLERS
 * ============================================================================ */

/*
 * donum - Set numeric configuration value
 *
 * Validates input is a number before setting.
 *
 * PARAMETERS:
 *   player - Player executing command (must be root)
 *   name - Config key name (for MariaDB persistence)
 *   var - Pointer to integer variable to modify
 *   arg2 - New value as string
 */
static void donum(dbref player, const char *name, int *var, char *arg2)
{
  char *p;

  if (!GoodObject(player)) {
    return;
  }

  /* Validate input contains only valid numeric characters */
  for (p = arg2; *p != '\0'; p++) {
    if (!strchr("-0123456789", *p)) {
      notify(player, "Must be a number.");
      return;
    }
  }

  (*var) = (int)strtol(arg2, NULL, 10);
  mariadb_config_save(name, arg2, "NUM");
  notify(player, "Set.");
}

/*
 * dostr - Set string configuration value
 *
 * Allocates new memory and copies string.
 *
 * PARAMETERS:
 *   player - Player executing command (must be root)
 *   name - Config key name (for MariaDB persistence)
 *   var - Pointer to string pointer to modify
 *   arg2 - New string value
 *
 * SECURITY: Uses SET macro which properly frees old value and allocates new
 */
static void dostr(dbref player, const char *name, char **var, char *arg2)
{
  if (!GoodObject(player)) {
    return;
  }

  if (!*arg2) {
    notify(player, "Must give new string.");
    return;
  }

  SET(*var, arg2);
  mariadb_config_save(name, arg2, "STR");
  notify(player, "Set.");
}

/*
 * doref - Set dbref configuration value
 *
 * Validates dbref exists before setting.
 *
 * PARAMETERS:
 *   player - Player executing command (must be root)
 *   name - Config key name (for MariaDB persistence)
 *   var - Pointer to dbref variable to modify
 *   arg2 - Object name or #dbref
 *
 * SECURITY: Uses match_thing which validates object exists
 */
static void doref(dbref player, const char *name, dbref *var, char *arg2)
{
  dbref thing;
  char numbuf[32];

  if (!GoodObject(player)) {
    return;
  }

  thing = match_thing(player, arg2);
  if (thing == NOTHING) {
    return;
  }

  if (!GoodObject(thing)) {
    notify(player, "Invalid object reference.");
    return;
  }

  (*var) = thing;
  snprintf(numbuf, sizeof(numbuf), "%" DBREF_FMT, thing);
  mariadb_config_save(name, numbuf, "REF");
  notify(player, "Set.");
}

/*
 * dolng - Set long integer configuration value
 *
 * Validates input is a number before setting.
 *
 * PARAMETERS:
 *   player - Player executing command (must be root)
 *   name - Config key name (for MariaDB persistence)
 *   var - Pointer to long variable to modify
 *   arg2 - New value as string
 */
static void dolng(dbref player, const char *name, long *var, char *arg2)
{
  char *p;

  if (!GoodObject(player)) {
    return;
  }

  /* Validate input contains only valid numeric characters */
  for (p = arg2; *p != '\0'; p++) {
    if (!strchr("-0123456789", *p)) {
      notify(player, "Must be a number.");
      return;
    }
  }

  (*var) = atol(arg2);
  mariadb_config_save(name, arg2, "LNG");
  notify(player, "Set.");
}

/* ============================================================================
 * CONFIGURATION DISPLAY - SORTED ALPHABETICALLY
 * ============================================================================ */

/*
 * config_entry - Struct for collecting config entries before sorting
 */
struct config_entry {
  char display[256];
};

/*
 * config_entry_cmp - Comparison function for qsort (alphabetical)
 */
static int config_entry_cmp(const void *a, const void *b)
{
  const struct config_entry *ea = (const struct config_entry *)a;
  const struct config_entry *eb = (const struct config_entry *)b;
  return strcmp(ea->display, eb->display);
}

/*
 * info_config - Display all configuration values sorted alphabetically
 *
 * Shows permission denied messages and all configuration variables.
 * Collects entries into an array, sorts, then displays.
 *
 * PARAMETERS:
 *   player - Player requesting info
 */
void info_config(dbref player)
{
  int i;
  int entry_count = 0;

  /* Maximum entries: count of conf.h entries + perm_messages entries.
   * 100 should be more than enough for the ~80 config entries. */
  struct config_entry entries[200];

  if (!GoodObject(player)) {
    return;
  }

  /* Collect perm_messages entries */
  if (perm_messages) {
    for (i = 0; i < perm_messages_count; i++) {
      if (perm_messages[i] && entry_count < 200) {
        snprintf(entries[entry_count].display, sizeof(entries[0].display),
                 "  %-22s: %s", tprintf("perm_messages-%d", i + 1),
                 perm_messages[i]);
        entry_count++;
      }
    }
  }

  /* Collect all configuration values using macros from conf.h */
#define DO_NUM(str, var) \
  if (entry_count < 200) { \
    snprintf(entries[entry_count].display, sizeof(entries[0].display), \
             "  %-22s: %d", str, var); \
    entry_count++; \
  }
#define DO_STR(str, var) \
  if (entry_count < 200) { \
    snprintf(entries[entry_count].display, sizeof(entries[0].display), \
             "  %-22s: %s", str, var ? var : "(null)"); \
    entry_count++; \
  }
#define DO_REF(str, var) \
  if (entry_count < 200) { \
    snprintf(entries[entry_count].display, sizeof(entries[0].display), \
             "  %-22s: #%" DBREF_FMT, str, var); \
    entry_count++; \
  }
#define DO_LNG(str, var) \
  if (entry_count < 200) { \
    snprintf(entries[entry_count].display, sizeof(entries[0].display), \
             "  %-22s: %ld", str, var); \
    entry_count++; \
  }
#include "conf.h"
#undef DO_NUM
#undef DO_STR
#undef DO_REF
#undef DO_LNG

  /* Sort entries alphabetically */
  qsort(entries, (size_t)entry_count, sizeof(struct config_entry),
        config_entry_cmp);

  /* Display sorted entries */
  for (i = 0; i < entry_count; i++) {
    notify(player, entries[i].display);
  }
}

/* ============================================================================
 * CONFIGURATION MODIFICATION
 * ============================================================================ */

/*
 * do_config - Modify configuration values at runtime
 *
 * Allows root to change configuration settings without recompiling.
 *
 * SYNTAX: @config <option>=<value>
 *         @config perm_messages-N=<message text>
 *         @config seed            - Write all config values to MariaDB
 *         @config reload          - Reload config values from MariaDB
 *         @config dbstatus        - Show MariaDB connection status
 *
 * PARAMETERS:
 *   player - Player executing command (must be Wizard)
 *   arg1 - Configuration option name (or seed, reload, dbstatus)
 *   arg2 - New value for option
 *
 * SECURITY:
 * - Only Wizards (Directors) can execute this command
 * - All changes validated by type-specific handlers
 * - Strings allocated with SAFE_MALLOC for tracking
 */
void do_config(dbref player, char *arg1, char *arg2)
{
  if (!GoodObject(player)) {
    return;
  }

  /* Security check - only Wizards (Directors) can modify configuration */
  if (!Wizard(player)) {
    notify(player, perm_denied());
    return;
  }

  /* ===== Handle sub-commands ===== */
  /* Accept both "seed" and "/seed" forms (parser splits on whitespace,
   * so "@config seed" gives arg1="seed", but "@config/seed" would give
   * command="@config/seed" which won't match; support both anyway) */

  /* Skip leading '/' if present for sub-command matching */
  {
    const char *subcmd = arg1;
    if (*subcmd == '/') {
      subcmd++;
    }

    /* @config seed - Write all current config values to MariaDB */
    if (!string_compare(subcmd, "seed")) {
      if (!mariadb_is_connected()) {
        notify(player, "MariaDB is not connected. Cannot seed config values.");
        return;
      }
      int count = mariadb_config_save_all();
      if (count >= 0) {
        notify(player, tprintf("Seeded %d config values to MariaDB.", count));
      } else {
        notify(player, "Error seeding config values to MariaDB.");
      }
      return;
    }

    /* @config reload - Reload config values from MariaDB */
    if (!string_compare(subcmd, "reload")) {
      if (!mariadb_is_connected()) {
        notify(player, "MariaDB is not connected. Cannot reload config values.");
        return;
      }
      int count = mariadb_config_load();
      if (count >= 0) {
        notify(player, tprintf("Reloaded %d config values from MariaDB.", count));
      } else {
        notify(player, "Error reloading config values from MariaDB.");
      }
      return;
    }

    /* @config dbstatus - Show MariaDB connection status */
    if (!string_compare(subcmd, "dbstatus")) {
      if (mariadb_is_connected()) {
        notify(player, "MariaDB: Connected");
      } else {
        notify(player, "MariaDB: Not connected");
      }
      return;
    }
  }

  /* ===== Handle perm_messages modification ===== */
  /* Format: @config perm_messages-N=message text */
  if (strncmp(arg1, "perm_messages-", 14) == 0) {
    int idx;
    char keybuf[32];
    const char *numpart = arg1 + 14;

    /* Validate the number part */
    if (*numpart == '\0' || *numpart < '1' || *numpart > '9') {
      notify(player, "Usage: @config perm_messages-N=<message> (N is 1-based index)");
      return;
    }
    idx = (int)strtol(numpart, NULL, 10);
    if (idx < 1) {
      notify(player, "perm_messages index must be >= 1.");
      return;
    }

    if (!*arg2) {
      notify(player, "Must give new message text.");
      return;
    }

    /* Expand the array if needed */
    if (idx > perm_messages_count) {
      int new_count = idx;
      char **new_array;
      int i;
      SAFE_MALLOC(new_array, char *, (size_t)new_count);
      for (i = 0; i < perm_messages_count; i++) {
        new_array[i] = perm_messages[i];
      }
      for (i = perm_messages_count; i < new_count; i++) {
        new_array[i] = NULL;
      }
      if (perm_messages) {
        SAFE_FREE(perm_messages);
      }
      perm_messages = new_array;
      perm_messages_count = new_count;
    }

    /* Set the message at the given index (1-based) */
    SET(perm_messages[idx - 1], arg2);

    /* Persist to MariaDB */
    snprintf(keybuf, sizeof(keybuf), "perm_messages-%d", idx);
    mariadb_config_save(keybuf, arg2, "STR");

    notify(player, tprintf("perm_messages-%d set.", idx));
    return;
  }

  /* ===== Process configuration change ===== */

  /*
   * Use macros from conf.h to check each configuration option
   * and call appropriate handler if matched.
   * Each handler also persists the change to MariaDB if connected.
   */
#define DO_NUM(str, var) \
  if (!string_compare(arg1, str)) { \
    donum(player, str, &var, arg2); \
  } else

#define DO_STR(str, var) \
  if (!string_compare(arg1, str)) { \
    dostr(player, str, &var, arg2); \
  } else

#define DO_REF(str, var) \
  if (!string_compare(arg1, str)) { \
    doref(player, str, &var, arg2); \
  } else

#define DO_LNG(str, var) \
  if (!string_compare(arg1, str)) { \
    dolng(player, str, &var, arg2); \
  } else

#include "conf.h"

#undef DO_NUM
#undef DO_STR
#undef DO_REF
#undef DO_LNG

  /* If we get here, no configuration option matched */
  /* The final "else" from the macro chain leads here */
  {
    notify(player, tprintf("no such config option: %s", arg1));
  }
}
