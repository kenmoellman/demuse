/* com.c - Channel communication system
 *
 * ============================================================================
 * MODERNIZATION NOTES (2026)
 * ============================================================================
 * Fully migrated to MariaDB-backed storage with in-memory cache.
 * Replaces legacy TYPE_CHANNEL db[] objects and A_CHANNEL attribute
 * string parsing with channel_cache_t and channel_member_t lookups.
 *
 * All mutations write to MariaDB first, then update the cache.
 * Message delivery (com_send_int) uses cache for O(1) membership checks.
 *
 * Legacy compatibility functions (channel_int_lookup, channel_int_add, etc.)
 * are preserved as thin wrappers for external callers during transition.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "player.h"
#include "hash_table.h"
#include "mariadb_channel.h"

/* ===================================================================
 * Constants and Limits
 * =================================================================== */

#define CHANNEL_BUF_SIZE 1024
#define MESSAGE_BUF_SIZE 4096

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

/* Channel management functions */
void channel_create(dbref, char *);
void channel_destroy(dbref, char *);
void channel_op(dbref, char *);
void channel_owner(dbref, char *);
void channel_lock(dbref, char *);
void channel_password(dbref, char *);
void channel_join(dbref, char *);
void channel_leave(dbref, char *);
void channel_default(dbref, char *);
void channel_alias(dbref, char *);
void channel_boot(dbref, char *);
void channel_list(dbref, char *);
void channel_search(dbref, char *);
void channel_log(dbref, char *);
void channel_ban(dbref, char *);
void channel_unban(dbref, char *);
void channel_color(dbref, char *);

/* Helper functions */
char *channel_int_find_alias(dbref, const char *);
char *channel_int_is_alias(dbref, const char *);
int channel_int_is_on_channel(dbref, const char *);
int channel_int_is_on_only(dbref, const char *);
char *channel_int_find_color(dbref, const char *);
int channel_int_remove_player(dbref, const char *);
int channel_int_is_on(dbref, dbref);
void channel_mute(dbref, const char *, const char *);
void do_chemit(dbref, char *, char *);
void channel_talk(dbref, char *, char *, char *);
void com_send_int(char *, char *, dbref, int);

/* Channel hash management (legacy compatibility wrappers) */
void channel_dbinit_clear(void);

static void channel_who(dbref, const char *);


/* ===================================================================
 * Channel Utility Functions
 * =================================================================== */

/**
 * channel_int_get_default - Get the name of a player's default channel
 *
 * Searches the player's membership list for the entry with is_default=1.
 *
 * @return Channel name string (from tprintf), or NULL if none set
 */
static const char *channel_int_get_default(dbref player)
{
  channel_member_t *m;
  channel_cache_t *chan;

  m = channel_cache_get_member_list(player);
  while (m) {
    if (m->is_default) {
      chan = channel_cache_lookup_by_id(m->channel_id);
      if (chan) {
        return chan->name;
      }
    }
    m = m->next;
  }

  /* No default set — return first channel membership */
  m = channel_cache_get_member_list(player);
  if (m) {
    chan = channel_cache_lookup_by_id(m->channel_id);
    if (chan) {
      return chan->name;
    }
  }

  return NULL;
}

/* ===================================================================
 * Channel Message Sending - Core Functions
 * =================================================================== */

/**
 * Send message to channel (public wrapper)
 */
void com_send(char *channel, char *message)
{
  com_send_int(channel, message, 0, 0);
}

/**
 * Send message to channel as specific player
 */
void com_send_as(char *channel, char *message, dbref player)
{
  com_send_int(channel, message, player, 0);
}

/**
 * Send hidden message to channel (for staff monitoring)
 */
void com_send_as_hidden(char *channel, char *message, dbref player)
{
  com_send_int(channel, message, player, 1);
}

/**
 * Internal channel message sending with full options
 *
 * Uses the in-memory cache for O(1) membership and mute checks.
 * Each player sees their own personalized color_name for the channel.
 *
 * @param channel Channel name (plain, no prefix)
 * @param message Message to send
 * @param player Player sending (0 for system message)
 * @param hidden 1 if hidden from blacklisted users
 */
void com_send_int(char *channel, char *message, dbref player, int hidden)
{
  struct descriptor_data *d;
  char *output_str, *output_str2;
  channel_cache_t *chan;

  /* Validate input */
  if (!channel || !*channel || !message) {
    return;
  }

  /* Look up channel in cache (strip any access-level prefix) */
  chan = channel_cache_lookup(channel_strip_prefix(channel));
  if (!chan) {
    return;
  }

  /* Loop through all descriptors */
  for (d = descriptor_list; d; d = d->next) {
    channel_member_t *m;

    /* Check if descriptor is valid and connected */
    if (!d || d->state != CONNECTED || !GoodObject(d->player)) {
      continue;
    }

    /* Check membership and mute status via cache */
    m = channel_cache_get_member(d->player, chan->channel_id);
    if (!m || m->is_banned || m->muted) {
      continue;
    }

    /* Format the message with player's personalized color name */
    output_str = tprintf("[%s] %s",
                        m->color_name ? m->color_name : chan->cname,
                        message);

    /* Check visibility permissions */
    if (hidden && !could_doit(real_owner(d->player), real_owner(player), A_LHIDE)) {
      continue;
    }

    /* Check blacklist - both players must have each other blacklisted */
    if (player > 0) {
      char *p_blacklist = atr_get(real_owner(d->player), A_BLACKLIST);
      char *sender_blacklist = atr_get(real_owner(player), A_BLACKLIST);

      if ((p_blacklist && *p_blacklist) || (sender_blacklist && *sender_blacklist)) {
        if (could_doit(real_owner(player), real_owner(d->player), A_BLACKLIST) &&
            could_doit(real_owner(d->player), real_owner(player), A_BLACKLIST)) {
          continue;
        }
      }
    }

    /* Add puppet indicator if needed */
    if ((db[d->player].flags & PUPPET) && GoodObject(player) && (player != d->player)) {
      output_str2 = tprintf("%s  [#%" DBREF_FMT "/%s]", output_str, db[player].owner,
                           atr_get(db[player].owner, A_ALIAS));
      output_str = output_str2;
    }

    /* Add prefix/suffix */
    output_str2 = add_pre_suf(d->player, 1, output_str, d->pueblo);
    output_str = output_str2;

    /* Apply color/beep filtering based on player preferences */
    if (db[d->player].flags & PLAYER_NOBEEP) {
      if (db[d->player].flags & PLAYER_ANSI) {
        queue_string(d, parse_color_nobeep(output_str, d->pueblo));
      } else {
        queue_string(d, strip_color_nobeep(output_str));
      }
    } else {
      if (db[d->player].flags & PLAYER_ANSI) {
        queue_string(d, parse_color(output_str, d->pueblo));
      } else {
        queue_string(d, strip_color(output_str));
      }
    }

    queue_string(d, "\n");
  } /* end for loop through descriptors */
} /* end com_send_int */

/* ===================================================================
 * Channel WHO Command
 * =================================================================== */

/**
 * Show who is on a channel
 */
static void channel_who(dbref player, const char *channel)
{
  struct descriptor_data *d;
  channel_cache_t *chan;
  int hidden = 0;
  int visible = 0;

  if (!channel || !*channel) {
    notify(player, "+channel: No channel specified.");
    return;
  }

  chan = channel_cache_lookup(channel);

  if (!chan) {
    notify(player, "+channel: Sorry, this channel doesn't exist.");
    return;
  }

  /* Check if channel is dark */
  if ((chan->flags & DARK) &&
      !channel_controls(player, chan)) {
    notify(player, "+channel: Sorry, this channel is set DARK.");
    return;
  }

  /* Count visible and hidden users */
  for (d = descriptor_list; d; d = d->next) {
    channel_member_t *m;

    if (d->state != CONNECTED || !GoodObject(d->player)) {
      continue;
    }

    m = channel_cache_get_member(d->player, chan->channel_id);
    if (!m || m->is_banned) {
      continue;
    }

    if ((could_doit(real_owner(d->player), real_owner(player), A_LHIDE))
        && (((!strlen(atr_get(real_owner(d->player), A_BLACKLIST))) &&
             (!strlen(atr_get(real_owner(player), A_BLACKLIST)))) ||
            (!((could_doit(real_owner(player), real_owner(d->player), A_BLACKLIST)) &&
               (could_doit(real_owner(d->player), real_owner(player), A_BLACKLIST)))))
       ) {
      notify(player, tprintf("%s is on channel %s.",
                            unparse_object(player, d->player), channel));
      visible++;
    } else {
      hidden++;
    }
  }

  notify(player, tprintf("%d Visible and %d Hidden Players are on channel %s",
                        visible, hidden, channel));
  notify(player, tprintf("--- %s ---", channel));
}

/* ===================================================================
 * Main COM Command - Channel Communication
 * =================================================================== */

/**
 * Main +com command handler
 * Syntax: +<channel> <message>
 */
void do_com(dbref player, char *arg1, char *arg2)
{
  char buf[MESSAGE_BUF_SIZE];
  char buf2[CHANNEL_BUF_SIZE];
  dbref toplayer;
  char *nocolor;
  char *dispname;
  int onoff;
  channel_cache_t *chan;
  channel_member_t *m;

  onoff = 0;

  /* Default channel handling */
  if (!*arg1) {
    const char *def = channel_int_get_default(player);
    if (!def) {
      notify(player, "+channel: You don't have a default channel set. Use '+channel default <channel>' to set one.");
      return;
    }
    arg1 = tprintf("%s", def);
    chan = channel_cache_lookup(arg1);
    if (chan) {
      m = channel_cache_get_member(player, chan->channel_id);
      onoff = (m && !m->muted) ? 1 : 0;
    }
  } else {
    chan = channel_cache_lookup(arg1);
    if (chan) {
      m = channel_cache_get_member(player, chan->channel_id);
      onoff = (m && !m->muted) ? 1 : 0;
    }
  }

  nocolor = strip_color_nobeep(arg1);
  if (!*nocolor) {
    notify(player, "No channel.");
    return;
  }

  if (strchr(nocolor, ' ')) {
    notify(player, "You're spacey.");
    return;
  }

  /* Permission checks */
  if (Typeof(player) != TYPE_PLAYER && !power(player, POW_COM_TALK)) {
    notify(player, "+channel: You don't have permission to talk on channels.");
    return;
  }

  chan = channel_cache_lookup(nocolor);
  if (!chan) {
    notify(player, "+channel: Sorry. You have old channels defined. Removing old channel..");
    channel_leave(player, nocolor);
    return;
  }

  m = channel_cache_get_member(player, chan->channel_id);

  /* Check if banned */
  if (m && m->is_banned) {
    notify(player, "+channel: You have been banned from that channel.");
    return;
  }

  /* Check speak lock */
  if (((chan->flags & HAVEN) ||
       !channel_eval_lock(player, chan->owner, chan->speak_lock)) &&
      !channel_controls(player, chan)) {
    notify(player, "+channel: You do not have permission to speak on this channel.");
    return;
  }

  /* Check if channel is on */
  if (onoff != 1) {
    notify(player, tprintf("+channel: Channel %s is currently turned off. Sorry.", nocolor));
    return;
  }

  /* Format message based on type */
  if (*arg2 == POSE_TOKEN) {
    snprintf(buf, sizeof(buf), "%s %s", db[player].cname, arg2 + 1);
  } else if (*arg2 == NOSP_POSE) {
    snprintf(buf, sizeof(buf), "%s's %s", db[player].cname, arg2 + 1);
  } else if (*arg2 == TO_TOKEN) {
    arg2++;
    strncpy(buf2, arg2, sizeof(buf2) - 1);
    buf2[sizeof(buf2) - 1] = '\0';

    char *space = strchr(buf2, ' ');
    if (space) {
      *space = '\0';
      arg2 += strlen(buf2) + 1;
    } else {
      arg2 += strlen(buf2);
    }

    toplayer = lookup_player(buf2);
    if (toplayer != NOTHING) {
      strncpy(buf2, db[toplayer].cname, sizeof(buf2) - 1);
      buf2[sizeof(buf2) - 1] = '\0';

      if (*arg2 == POSE_TOKEN) {
        snprintf(buf, sizeof(buf), "[to %s] %s %s",
                buf2, db[player].cname, arg2 + 1);
      } else if (*arg2 == NOSP_POSE) {
        snprintf(buf, sizeof(buf), "[to %s] %s's %s",
                buf2, db[player].cname, arg2 + 1);
      } else if (*arg2 == THINK_TOKEN) {
        snprintf(buf, sizeof(buf), "[to %s] %s . o O ( %s )",
                buf2, db[player].cname, arg2 + 1);
      } else {
        snprintf(buf, sizeof(buf), "%s [to %s]: %s",
                db[player].cname, buf2, arg2);
      }
    } else {
      snprintf(buf, sizeof(buf), "%s: %c%s %s",
              db[player].cname, TO_TOKEN, buf2, arg2);
    }
  } else if (*arg2 == THINK_TOKEN) {
    snprintf(buf, sizeof(buf), "%s . o O ( %s )",
            db[player].cname, arg2 + 1);
  } else {
    /* Normal speech */
    if (strlen(atr_get(player, A_CTITLE))) {
      dispname = tprintf("%s <%s>", db[player].cname,
                        atr_get(player, A_CTITLE));
    } else {
      dispname = db[player].cname;
    }
    snprintf(buf, sizeof(buf), "%s: %s", dispname, arg2);
  }

  /* Send to channel */
  com_send_int(nocolor, buf, player, 0);

  /* Notify sender if not on channel */
  if (!m) {
    notify(player, "Your +com has been sent.");
  }
}

/* ===================================================================
 * Channel Alias Management
 * =================================================================== */

/**
 * Find the alias for a channel that a player is on
 * @return Alias string or NULL if not found
 */
char *channel_int_find_alias(dbref player, const char *channel)
{
  channel_cache_t *chan;
  channel_member_t *m;

  if (!channel || !*channel) {
    return NULL;
  }

  chan = channel_cache_lookup(channel);
  if (!chan) {
    return NULL;
  }

  m = channel_cache_get_member(player, chan->channel_id);
  if (!m || !m->alias || !*m->alias) {
    return NULL;
  }

  return tprintf("%s", m->alias);
}

/**
 * Check if a string is an alias for a channel
 * @return Channel name if alias exists, NULL otherwise
 */
char *channel_int_is_alias(dbref player, const char *al)
{
  channel_member_t *m;
  channel_cache_t *chan;

  if (!al || !*al) {
    return NULL;
  }

  m = channel_cache_get_member_list(player);
  while (m) {
    if (m->alias && !strcmp(m->alias, al)) {
      chan = channel_cache_lookup_by_id(m->channel_id);
      if (chan) {
        return tprintf("%s", m->alias);
      }
    }
    m = m->next;
  }

  return NULL;
}

/**
 * Find channel name (wrapper with alias checking)
 */
char *channel_int_find(dbref player, const char *chan_name)
{
  channel_cache_t *chan;
  channel_member_t *m;

  if (!chan_name || !*chan_name) {
    return NULL;
  }

  /* Try exact channel name match first */
  chan = channel_cache_lookup(chan_name);
  if (chan) {
    m = channel_cache_get_member(player, chan->channel_id);
    if (m) {
      return tprintf("%s", chan->name);
    }
  }

  /* Try alias match */
  m = channel_cache_get_member_list(player);
  while (m) {
    if (m->alias && !strcmp(m->alias, chan_name)) {
      chan = channel_cache_lookup_by_id(m->channel_id);
      if (chan) {
        return tprintf("%s", chan->name);
      }
    }
    m = m->next;
  }

  return NULL;
}

/* ===================================================================
 * Channel Membership Checking
 * =================================================================== */

/**
 * Check if player is on a channel (with alias checking)
 * @return 0 if on channel, -1 if not (legacy return value compatibility)
 */
int channel_int_is_on_channel(dbref player, const char *chan_name)
{
  channel_cache_t *chan;
  channel_member_t *m;

  if (!chan_name || !*chan_name) {
    return -1;
  }

  /* Try exact name */
  chan = channel_cache_lookup(chan_name);
  if (chan) {
    m = channel_cache_get_member(player, chan->channel_id);
    if (m && !m->is_banned) {
      return 0;
    }
  }

  /* Try alias */
  m = channel_cache_get_member_list(player);
  while (m) {
    if (m->alias && !strcmp(m->alias, chan_name) && !m->is_banned) {
      return 0;
    }
    m = m->next;
  }

  return -1;
}

/**
 * Check if player is on a channel (exact match only)
 * @return 0 if on channel, -1 if not (legacy return value compatibility)
 */
int channel_int_is_on_only(dbref player, const char *chan_name)
{
  channel_cache_t *chan;
  channel_member_t *m;

  if (!chan_name || !*chan_name) {
    return -1;
  }

  chan = channel_cache_lookup(chan_name);
  if (!chan) {
    return -1;
  }

  m = channel_cache_get_member(player, chan->channel_id);
  if (m && !m->is_banned) {
    return 0;
  }

  return -1;
}

/* ===================================================================
 * Channel Command Dispatcher
 * =================================================================== */

/**
 * Main +channel command dispatcher
 * Routes to appropriate subcommand handler
 */
void do_channel(dbref player, char *arg1, char *arg2)
{
  if (!arg1 || !*arg1) {
    channel_list(player, "");
    return;
  }

  /* Modern subcommand syntax */
  if (!strncmp(arg1, "create", 6)) {
    channel_create(player, arg2);
  } else if (!strncmp(arg1, "destroy", 7)) {
    channel_destroy(player, arg2);
  } else if (!strncmp(arg1, "owner", 5)) {
    channel_owner(player, arg2);
  } else if (!strncmp(arg1, "op", 2)) {
    channel_op(player, arg2);
  } else if (!strncmp(arg1, "lock", 4)) {
    channel_lock(player, arg2);
  } else if (!strncmp(arg1, "password", 8)) {
    channel_password(player, arg2);
  } else if (!strncmp(arg1, "join", 4)) {
    channel_join(player, arg2);
  } else if (!strncmp(arg1, "leave", 5)) {
    channel_leave(player, arg2);
  } else if (!strncmp(arg1, "default", 7)) {
    channel_default(player, arg2);
  } else if (!strncmp(arg1, "alias", 5)) {
    channel_alias(player, arg2);
  } else if (!strncmp(arg1, "boot", 4)) {
    channel_boot(player, arg2);
  } else if (!strncmp(arg1, "list", 4)) {
    channel_list(player, arg2);
  } else if (!strncmp(arg1, "search", 6)) {
    channel_search(player, arg2);
  } else if (!strncmp(arg1, "log", 3)) {
    channel_log(player, arg2);
  } else if (!strncmp(arg1, "ban", 3)) {
    channel_ban(player, arg2);
  } else if (!strncmp(arg1, "unban", 5)) {
    channel_unban(player, arg2);
  } else if (!strncmp(arg1, "color", 5)) {
    channel_color(player, arg2);
  } else if (!strncmp(arg1, "who", 3)) {
    if (arg2 && *arg2) {
      channel_who(player, strip_color_nobeep(arg2));
    } else {
      const char *def = channel_int_get_default(player);
      if (def) {
        channel_who(player, def);
      } else {
        notify(player, "+channel: No channel specified and no default channel set.");
      }
    }
  } else if (!strncmp(arg1, "unmute", 6)) {
    if (arg2 && *arg2) {
      channel_mute(player, strip_color_nobeep(arg2), "on");
    } else {
      const char *def = channel_int_get_default(player);
      if (def) {
        channel_mute(player, def, "on");
      } else {
        notify(player, "+channel: No channel specified and no default channel set.");
      }
    }
  } else if (!strncmp(arg1, "mute", 4)) {
    if (arg2 && *arg2) {
      channel_mute(player, strip_color_nobeep(arg2), "off");
    } else {
      const char *def = channel_int_get_default(player);
      if (def) {
        channel_mute(player, def, "off");
      } else {
        notify(player, "+channel: No channel specified and no default channel set.");
      }
    }
  } else if (!*arg2) {
    channel_default(player, arg1);
  } else {
    notify(player, "+channel: Invalid command.");
  }
}

/* ===================================================================
 * Channel Creation and Destruction
 * =================================================================== */

/**
 * Validate channel name
 * @return 1 if valid, 0 if invalid
 */
int channel_int_ok_name(char *name)
{
  char *scan;

  if (!name || !*name) {
    return 0;
  }

  /* Basic validity checks */
  if (*name == NUMBER_TOKEN ||
      *name == NOT_TOKEN ||
      strchr(name, ARG_DELIMITER) ||
      strchr(name, AND_TOKEN) ||
      strchr(name, OR_TOKEN) ||
      strchr(name, ';') ||
      strchr(name, ' ') ||
      strlen(name) > (size_t)channel_name_limit) {
    return 0;
  }

  /* Reserved words */
  if (!string_compare(name, "me") ||
      !string_compare(name, "home") ||
      !string_compare(name, "here") ||
      !string_compare(name, "i") ||
      !string_compare(name, "you") ||
      !string_compare(name, "your") ||
      !string_compare(name, "he") ||
      !string_compare(name, "she") ||
      !string_compare(name, "it") ||
      !string_compare(name, "his") ||
      !string_compare(name, "her") ||
      !string_compare(name, "hers") ||
      !string_compare(name, "its") ||
      !string_compare(name, "we") ||
      !string_compare(name, "us") ||
      !string_compare(name, "our") ||
      !string_compare(name, "they") ||
      !string_compare(name, "them") ||
      !string_compare(name, "their") ||
      !string_compare(name, "a") ||
      !string_compare(name, "an") ||
      !string_compare(name, "the") ||
      !string_compare(name, "one") ||
      !string_compare(name, "to") ||
      !string_compare(name, "if") ||
      !string_compare(name, "and") ||
      !string_compare(name, "or") ||
      !string_compare(name, "but") ||
      !string_compare(name, "at") ||
      !string_compare(name, "of") ||
      !string_compare(name, "op") ||
      !string_compare(name, "own") ||
      !string_compare(name, "all") ||
      !string_compare(name, "for") ||
      !string_compare(name, "foo") ||
      !string_compare(name, "so") ||
      !string_compare(name, "this") ||
      !string_compare(name, "that") ||
      !string_compare(name, ">") ||
      !string_compare(name, ".") ||
      !string_compare(name, "-") ||
      !string_compare(name, ">>") ||
      !string_compare(name, "..") ||
      !string_compare(name, "--") ||
      !string_compare(name, "->") ||
      !string_compare(name, ":)") ||
      !string_compare(name, "delete") ||
      !string_compare(name, "purge") ||
      !string_compare(name, "check")) {
    return 0;
  }

  /* Check if already exists */
  if (channel_cache_lookup(name) != NULL) {
    return 0;
  }

  /* Character validation */
  for (scan = name; *scan; scan++) {
    if (!isprint(*scan)) {
      return 0;
    }
    switch (*scan) {
      case '~':
      case ';':
      case ',':
      case '*':
      case '@':
      case '#':
        if (scan != name) {
          return 0;
        }
        break;
      default:
        break;
    }
  }

  return 1;
}

/**
 * Create a new channel
 * Syntax: +channel create <name>[:<alias>]
 */
void channel_create(dbref player, char *arg2)
{
  ptype k;
  char *alias;
  long cost = channel_cost;
  char *nocolor;
  long channel_id;
  int min_level;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Create what?");
    return;
  }

  if (strchr(arg2, ' ')) {
    notify(player, "+channel: Sorry, channel names cannot have spaces in them.");
    return;
  }

  nocolor = strip_color_nobeep(arg2);

  alias = strchr(nocolor, ':');
  if (alias && *alias) {
    *alias++ = '\0';
  }

  /* Determine min_level from prefix */
  min_level = channel_name_to_level(nocolor);
  const char *plain_name = channel_strip_prefix(nocolor);

  /* Validate names */
  if (!channel_int_ok_name((char *)plain_name) ||
      (alias && *alias && !channel_int_ok_name(alias))) {
    notify(player, "+channel: That's a silly name for a channel!");
    return;
  }

  /* Check permissions for special prefix channels */
  if (!db[player].pows) {
    return;
  }

  k = *db[player].pows;

  if (min_level == 3 &&
      !(k == CLASS_ADMIN || k == CLASS_DIR)) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  if (min_level == 2 &&
      !(k == CLASS_DIR || k == CLASS_ADMIN || k == CLASS_BUILDER)) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  if (min_level == 1 &&
      !(k == CLASS_DIR || k == CLASS_ADMIN || k == CLASS_BUILDER ||
        k == CLASS_OFFICIAL || k == CLASS_JUNOFF)) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  /* Check if player can pay */
  if (!can_pay_fees(def_owner(player), (int)cost, QUOTA_COST)) {
    notify(player, "+channel: You don't have enough credits or quota.");
    return;
  }

  /* Create the channel in MariaDB */
  channel_id = mariadb_channel_create(plain_name, arg2, def_owner(player),
                                       (long)SEE_OK, min_level, 0);

  if (channel_id < 0) {
    notify(player, "+channel: Failed to create channel.");
    return;
  }

  notify(player, tprintf("+channel: Channel '%s' created.", plain_name));

  /* Auto-join creator to channel */
  if (alias && *alias) {
    channel_join(player, tprintf("%s:%s", plain_name, alias));
  } else {
    channel_join(player, (char *)plain_name);
  }
}

/**
 * Destroy a channel
 * Syntax: +channel destroy <name>
 */
void channel_destroy(dbref player, char *name)
{
  channel_cache_t *chan;

  if (!name || !*name) {
    notify(player, "+channel: Destroy what?");
    return;
  }

  chan = channel_cache_lookup(channel_strip_prefix(name));

  if (!chan) {
    notify(player, "+channel: Invalid channel name.");
    return;
  }

  /* Check if system channel */
  if (chan->is_system) {
    notify(player, "+channel: Cannot destroy a system channel.");
    return;
  }

  /* Permission checks */
  if (Typeof(player) != TYPE_PLAYER) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  if (!channel_controls(player, chan) &&
      !power(player, POW_NUKE)) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  /* Notify all members */
  {
    struct descriptor_data *d;
    for (d = descriptor_list; d; d = d->next) {
      if (d->state == CONNECTED && GoodObject(d->player)) {
        channel_member_t *m = channel_cache_get_member(d->player, chan->channel_id);
        if (m && !m->is_banned) {
          notify(d->player, tprintf("+channel: %s is being destroyed. You must leave now.",
                                   chan->cname));
        }
      }
    }
  }

  log_important(tprintf("%s executed: +channel destroy %s",
                       unparse_object_a(player, player), name));

  /* Destroy in MariaDB (CASCADE removes all memberships) */
  mariadb_channel_destroy(chan->channel_id);

  notify(player, tprintf("+channel: %s destroyed.", name));
}

/* ===================================================================
 * Channel Administrative Commands
 * =================================================================== */

/**
 * Set or remove channel operator status
 * Syntax: +channel op <channel>:<player> or <channel>:!<player>
 */
void channel_op(dbref player, char *arg2)
{
  char *user;
  dbref target;
  channel_cache_t *chan;
  channel_member_t *m;
  int yesno = 1;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Invalid op format.");
    return;
  }

  user = strchr(arg2, ':');
  if (!user || !*user) {
    notify(player, "+channel: Invalid op format.");
    return;
  }

  *user++ = '\0';

  /* Check for removal (!player) */
  if (*user == '!') {
    yesno = 0;
    user++;
  }

  if (!*user) {
    notify(player, "+channel: Invalid op format.");
    return;
  }

  chan = channel_cache_lookup(arg2);
  if (!chan) {
    notify(player, "+channel: Invalid channel specified in op operation.");
    return;
  }

  /* Check permissions */
  if (!channel_controls(player, chan)) {
    notify(player, "+channel: You don't have permission to set ops on this channel.");
    return;
  }

  target = lookup_player(user);
  if (target == NOTHING) {
    notify(player, "+channel: Invalid player specified in op operation.");
    return;
  }

  m = channel_cache_get_member(target, chan->channel_id);

  if (yesno == 0) {
    /* Removing op status */
    if (m && m->is_operator) {
      mariadb_channel_set_operator(chan->channel_id, target, 0);
      notify(player, tprintf("+channel: %s is no longer an op on %s",
                            unparse_object(player, target), chan->name));
    } else {
      notify(player, tprintf("+channel: %s was not an op on %s anyway!",
                            unparse_object(player, target), chan->name));
    }
  } else {
    /* Adding op status */
    if (m && m->is_operator) {
      notify(player, tprintf("+channel: %s is already an op on %s!",
                            unparse_object(player, target), chan->name));
    } else if (m) {
      mariadb_channel_set_operator(chan->channel_id, target, 1);
      notify(player, tprintf("+channel: %s is now an op on %s",
                            unparse_object(player, target), chan->name));
    } else {
      notify(player, tprintf("+channel: %s is not on channel %s.",
                            unparse_object(player, target), chan->name));
    }
  }
}

/**
 * Change channel owner
 * Syntax: +channel owner=<channel>:<player>
 *
 * The current owner can transfer to any player who hasn't blacklisted them.
 * Directors and admins with POW_CHANNEL can transfer any non-system channel.
 * System channels cannot have their owner changed.
 */
void channel_owner(dbref player, char *arg2)
{
  char *target_name;
  dbref target;
  channel_cache_t *chan;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Usage: +channel owner=<channel>:<player>");
    return;
  }

  target_name = strchr(arg2, ':');
  if (!target_name || !*(target_name + 1)) {
    notify(player, "+channel: Usage: +channel owner=<channel>:<player>");
    return;
  }

  *target_name++ = '\0';

  chan = channel_cache_lookup(arg2);
  if (!chan) {
    notify(player, "+channel: Channel not found.");
    return;
  }

  /* System channels must always be owned by root */
  if (chan->is_system) {
    notify(player, "+channel: System channels cannot have their owner changed.");
    return;
  }

  /* Only owner, director, or admin with POW_CHANNEL can transfer ownership */
  if (chan->owner != player &&
      db[player].pows[0] < CLASS_DIR &&
      !power(player, POW_CHANNEL)) {
    notify(player, "+channel: You must be the channel owner to transfer ownership.");
    return;
  }

  target = lookup_player(target_name);
  if (target == NOTHING) {
    notify(player, "+channel: Player not found.");
    return;
  }

  /* Non-admin owners must pass the recipient's blacklist check */
  if (db[player].pows[0] < CLASS_DIR && !power(player, POW_CHANNEL)) {
    if (!could_doit(real_owner(player), real_owner(target), A_BLACKLIST)) {
      notify(player, "+channel: That player has blacklisted you.");
      return;
    }
  }

  {
    dbref old_owner = chan->owner;

    if (!mariadb_channel_update_owner(chan->channel_id, target)) {
      notify(player, "+channel: Failed to update channel owner.");
      return;
    }

    log_important(tprintf("%s executed: +channel owner %s:%s (was %s)",
                          unparse_object(player, player),
                          chan->name,
                          unparse_object(player, target),
                          GoodObject(old_owner) ?
                            unparse_object(player, old_owner) : "???"));
  }

  notify(player, tprintf("+channel: Ownership of %s transferred to %s.",
                          chan->name, unparse_object(player, target)));
  notify(target, tprintf("+channel: %s has transferred ownership of channel %s to you.",
                          unparse_object(target, player), chan->name));
}

/**
 * Set channel lock
 * Syntax: +channel lock <channel>:<locktype>=<lock>
 */
void channel_lock(dbref player, char *arg2)
{
  char *locktype;
  char *lock_str;
  channel_cache_t *chan;
  const char *field;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Bad lock syntax. Use: +channel lock <channel>:<type>=<lock>");
    return;
  }

  locktype = strchr(arg2, ':');
  if (!locktype || !*locktype) {
    notify(player, "+channel: Bad lock syntax. Use: +channel lock <channel>:<type>=<lock>");
    return;
  }

  *locktype++ = '\0';

  lock_str = strchr(locktype, '=');
  if (lock_str) {
    *lock_str++ = '\0';
  }

  chan = channel_cache_lookup(arg2);
  if (!chan) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  if (!channel_controls(player, chan)) {
    notify(player, "+channel: You don't have permission to set locks on this channel.");
    return;
  }

  /* Map lock type to field name */
  if (!string_compare(locktype, "speak") || !string_compare(locktype, "slock")) {
    field = "speak_lock";
  } else if (!string_compare(locktype, "join") || !string_compare(locktype, "lock")) {
    field = "join_lock";
  } else if (!string_compare(locktype, "hide") || !string_compare(locktype, "lhide")) {
    field = "hide_lock";
  } else {
    notify(player, "+channel: Invalid lock type. Use: speak, join, or hide");
    return;
  }

  mariadb_channel_update_field(chan->channel_id, field,
                                (lock_str && *lock_str) ? lock_str : NULL);

  if (lock_str && *lock_str) {
    notify(player, tprintf("+channel: %s lock set on %s.", locktype, chan->name));
  } else {
    notify(player, tprintf("+channel: %s lock cleared on %s.", locktype, chan->name));
  }
}

/**
 * Set channel password
 * Syntax: +channel password <channel>:<password>
 */
void channel_password(dbref player, char *arg2)
{
  char *password;
  channel_cache_t *chan;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Bad password syntax.");
    return;
  }

  password = strchr(arg2, ':');
  if (!password) {
    notify(player, "+channel: Bad password syntax.");
    return;
  }

  *password++ = '\0';

  chan = channel_cache_lookup(arg2);
  if (!chan) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  /* Check permissions */
  if (!channel_controls(player, chan)) {
    notify(player, "+channel: You do not have permission to set the password on this channel.");
    return;
  }

  /* Set or clear password */
  if (password && *password) {
    mariadb_channel_update_field(chan->channel_id, "password",
                                  crypt(password, "XX"));
    notify(player, tprintf("+channel: %s password changed.", chan->name));
  } else {
    mariadb_channel_update_field(chan->channel_id, "password", NULL);
    notify(player, tprintf("+channel: %s password erased.", chan->name));
  }
}

/* ===================================================================
 * Channel Membership Management
 * =================================================================== */

/**
 * Join a channel
 * Syntax: +channel join <n>[:<alias>[:<password>]]
 */
void channel_join(dbref player, char *arg2)
{
  ptype k;
  char buf[MESSAGE_BUF_SIZE];
  char *alias;
  char *password;
  char *cryptpass;
  int pmatch = 0;
  channel_cache_t *chan;
  channel_member_t *m;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Join what?");
    return;
  }

  if (strchr(arg2, ' ')) {
    notify(player, "Sorry, channel names cannot have spaces in them.");
    return;
  }

  if (Typeof(player) != TYPE_PLAYER && !power(player, POW_COM_TALK)) {
    notify(player, "+channel: You don't have permission to be on channels.");
    return;
  }

  /* Parse alias and password */
  alias = strchr(arg2, ':');
  if (alias && *alias) {
    *alias++ = '\0';
  }

  if (!(alias && *alias)) {
    alias = arg2;
  } else {
    password = strchr(alias, ':');
    if (password && *password) {
      *password++ = '\0';
      if (password && *password) {
        cryptpass = crypt(password, "XX");
        if (!strcmp(cryptpass, Pass(player))) {
          pmatch = 1;
        }
      }
    }
  }

  chan = channel_cache_lookup(arg2);

  if (!chan) {
    notify(player, tprintf("+channel: Channel %s does not exist.", arg2));
    return;
  }

  /* Check if already on channel */
  m = channel_cache_get_member(player, chan->channel_id);
  if (m && !m->is_banned) {
    notify(player, "You are already on that channel. Try +ch alias to change aliases.");
    return;
  }

  /* Check if alias is already in use */
  if (channel_int_is_alias(player, alias)) {
    notify(player, tprintf("+channel: You're already using that alias. (%s)", alias));
    return;
  }

  /* Check if banned */
  if (m && m->is_banned) {
    notify(player, "You have been banned from that channel.");
    return;
  }

  /* Permission checks for min_level channels */
  if (!pmatch) {
    if (!db[player].pows) {
      return;
    }

    k = *db[player].pows;

    if (chan->min_level == 3 && !(k == CLASS_ADMIN || k == CLASS_DIR)) {
      notify(player, perm_denied());
      return;
    }

    if (chan->min_level == 2 &&
        !(k == CLASS_DIR || k == CLASS_ADMIN || k == CLASS_BUILDER)) {
      notify(player, perm_denied());
      return;
    }

    if (chan->min_level == 1 &&
        !(k == CLASS_DIR || k == CLASS_ADMIN || k == CLASS_BUILDER ||
          k == CLASS_OFFICIAL || k == CLASS_JUNOFF)) {
      notify(player, perm_denied());
      return;
    }

    /* Check channel join lock */
    if (!channel_eval_lock(player, chan->owner, chan->join_lock) &&
        !channel_controls(player, chan)) {
      notify(player, "+channel: Sorry, you are not permitted to join this channel.");
      return;
    }
  }

  /* Join the channel in MariaDB + cache */
  if (!mariadb_channel_join(chan->channel_id, player, alias, chan->cname)) {
    notify(player, "+channel: Failed to join channel.");
    return;
  }

  /* Announce join unless channel is QUIET */
  if (!(chan->flags & QUIET)) {
    if (chan->join_msg && *chan->join_msg) {
      char buf2[MESSAGE_BUF_SIZE];
      char *p;

      pronoun_substitute(buf2, player, chan->join_msg, chan->owner);
      p = buf2 + strlen(db[player].name) + 1;
      strncpy(buf, p, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
    } else {
      snprintf(buf, sizeof(buf), "|G!+*| %s has joined this channel.",
              db[player].cname);
    }
    com_send(arg2, buf);
  }

  notify(player, tprintf("+channel: %s added to your channel list with alias %s.",
                        arg2, alias));

  /* Show channel topic */
  if (chan->topic && *chan->topic) {
    notify(player, tprintf("+channel topic: %s", chan->topic));
  }
}

/**
 * Leave a channel
 * Syntax: +channel leave <n>
 */
void channel_leave(dbref player, char *arg2)
{
  char buf[MESSAGE_BUF_SIZE];
  channel_cache_t *chan;
  channel_member_t *m;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Leave what?");
    return;
  }

  if (strchr(arg2, ' ')) {
    notify(player, "Sorry, channel names cannot have spaces in them.");
    return;
  }

  chan = channel_cache_lookup(arg2);
  if (!chan) {
    notify(player, "You aren't on that channel.");
    return;
  }

  m = channel_cache_get_member(player, chan->channel_id);
  if (!m) {
    notify(player, "You aren't on that channel.");
    return;
  }

  /* Announce leave unless channel is QUIET */
  if (!(chan->flags & QUIET)) {
    if (chan->leave_msg && *chan->leave_msg) {
      char buf2[MESSAGE_BUF_SIZE];
      char *p;

      pronoun_substitute(buf2, player, chan->leave_msg, player);
      p = buf2 + strlen(db[player].name) + 1;
      strncpy(buf, p, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
    } else {
      snprintf(buf, sizeof(buf), "|G!+*| %s has left this channel.",
              db[player].cname);
    }
    com_send(arg2, buf);
  }

  notify(player, tprintf("%s has been deleted from your channel list.", arg2));

  /* Remove from MariaDB + cache */
  mariadb_channel_leave(chan->channel_id, player);

  /* Handle non-system channel cleanup after leave */
  if (!chan->is_system) {
    int remaining = mariadb_channel_member_count(chan->channel_id);
    if (remaining == 0) {
      /* Last member left — auto-destroy */
      log_important(tprintf("Channel %s auto-destroyed (last member %s left)",
                            chan->name, unparse_object_a(player, player)));
      mariadb_channel_destroy(chan->channel_id);
    } else if (chan->owner == player) {
      /* Owner left but others remain — transfer to oldest member */
      dbref new_owner = mariadb_channel_oldest_member(chan->channel_id);
      if (GoodObject(new_owner)) {
        mariadb_channel_update_owner(chan->channel_id, new_owner);
        log_important(tprintf("Channel %s ownership auto-transferred from %s to %s",
                              chan->name, unparse_object_a(player, player),
                              unparse_object_a(player, new_owner)));
        notify(new_owner, tprintf("+channel: You are now the owner of channel %s.",
                                  chan->name));
      }
    }
  }
}

/**
 * Set default channel
 * Syntax: +channel default <n>
 */
void channel_default(dbref player, char *arg1)
{
  channel_cache_t *chan;
  channel_member_t *m;

  if (!arg1 || !*arg1) {
    notify(player, "+channel: Set what as default?");
    return;
  }

  chan = channel_cache_lookup(arg1);
  if (!chan) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  m = channel_cache_get_member(player, chan->channel_id);
  if (!m) {
    notify(player, "+channel default: Need to join the channel first.");
    return;
  }

  mariadb_channel_set_default(player, chan->channel_id);

  notify(player, tprintf("+channel default: %s is now your default channel.", arg1));
}

/**
 * Set channel alias
 * Syntax: +channel alias <channel>:<alias>
 */
void channel_alias(dbref player, char *arg2)
{
  char *channel;
  char *alias;
  channel_cache_t *chan;
  channel_member_t *m;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Bad +channel alias syntax.");
    return;
  }

  channel = arg2;
  alias = strchr(channel, ':');

  if (!alias || !*alias) {
    notify(player, "+channel: Bad +channel alias syntax.");
    return;
  }

  *alias++ = '\0';

  /* Strip any trailing :onoff */
  char *s = strchr(alias, ':');
  if (s) {
    *s = '\0';
  }

  chan = channel_cache_lookup(channel);
  if (!chan) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  m = channel_cache_get_member(player, chan->channel_id);
  if (!m) {
    notify(player, "+channel: You must first join the channel before setting its alias.");
    return;
  }

  mariadb_channel_set_alias(chan->channel_id, player, alias);
  notify(player, tprintf("Alias for channel %s is now %s", channel, alias));
}

/**
 * Boot a player from a channel
 * Syntax: +channel boot <channel>:<player>
 */
void channel_boot(dbref player, char *channel)
{
  char *vic;
  dbref victim;
  channel_cache_t *chan;

  if (!channel || !*channel) {
    notify(player, "+channel: Bad boot syntax.");
    return;
  }

  vic = strchr(channel, ':');
  if (!vic || !*vic) {
    notify(player, "+channel: Bad boot syntax.");
    return;
  }

  *vic++ = '\0';

  if (!*vic) {
    notify(player, "+channel: Bad boot syntax.");
    return;
  }

  chan = channel_cache_lookup(channel);
  if (!chan) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  /* Check permissions */
  if (!channel_controls(player, chan)) {
    notify(player, "+channel: You don't have permission to boot from this channel.");
    return;
  }

  victim = lookup_player(vic);
  if (victim == NOTHING) {
    notify(player, "+channel: Invalid player.");
    return;
  }

  if (channel_int_remove_player(victim, channel) >= 0) {
    notify(player, tprintf("+channel: You have booted %s from %s.",
                          unparse_object(player, victim), channel));
    notify(victim, tprintf("+channel: You have been booted from %s by %s",
                          channel, unparse_object(victim, player)));
    com_send(channel, tprintf("%s has been booted from this channel",
                             db[victim].cname));
  } else {
    notify(player, "+channel: Player not on channel.");
  }
}

/* ===================================================================
 * Channel Ban Management
 * =================================================================== */

/**
 * Ban a player from a channel
 * Syntax: +channel ban <channel>:<player>
 */
void channel_ban(dbref player, char *arg2)
{
  dbref victim;
  char *arg1;
  channel_cache_t *chan;
  channel_member_t *m;

  if (!power(player, POW_BAN)) {
    notify(player, perm_denied());
    return;
  }

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Bad ban syntax.");
    return;
  }

  arg1 = strchr(arg2, ':');
  if (!(arg1 && *arg1)) {
    notify(player, "+channel: Bad ban syntax.");
    return;
  }

  *arg1++ = '\0';

  /* Match player */
  init_match(player, arg1, TYPE_PLAYER);
  match_neighbor();
  match_here();
  if (power(player, POW_REMOTE)) {
    match_player(NOTHING, NULL);
    match_absolute();
  }

  victim = noisy_match_result();
  if (victim == NOTHING) {
    return;
  }

  if (!controls(player, victim, POW_BAN)) {
    log_important(tprintf("%s failed to: +channel ban %s=%s",
                         unparse_object_a(player, player),
                         unparse_object_a(victim, victim), arg2));
    notify(player, perm_denied());
    return;
  }

  if (strchr(arg2, ' ') || !*arg2) {
    notify(player, "Sorry, channel names cannot have spaces in them.");
    return;
  }

  chan = channel_cache_lookup(arg2);
  if (!chan) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  m = channel_cache_get_member(victim, chan->channel_id);
  if (m && m->is_banned) {
    notify(player, tprintf("%s has already been banned from %s.",
                          unparse_object(player, victim), arg2));
    return;
  }

  /* Remove from channel first, then ban */
  channel_int_remove_player(victim, arg2);

  /* Set ban in MariaDB + cache */
  mariadb_channel_set_ban(chan->channel_id, victim, 1);

  log_important(tprintf("%s executed: +channel ban %s=%s",
                       unparse_object_a(player, player),
                       unparse_object_a(victim, victim), arg2));
  notify(player, tprintf("%s banned from channel %s.",
                        unparse_object(player, victim), arg2));
  notify(victim, tprintf("You have been banned from channel %s by %s.",
                        arg2, unparse_object(victim, player)));
  com_send(arg2, tprintf("%s has been banned from this channel.",
                        db[victim].cname));
}

/**
 * Unban a player from a channel
 * Syntax: +channel unban <channel>:<player>
 */
void channel_unban(dbref player, char *arg2)
{
  dbref victim;
  char *arg1;
  channel_cache_t *chan;
  channel_member_t *m;

  if (!power(player, POW_BAN)) {
    notify(player, perm_denied());
    return;
  }

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Bad unban syntax.");
    return;
  }

  arg1 = strchr(arg2, ':');
  if (!(arg1 && *arg1)) {
    notify(player, "+channel: Bad unban syntax.");
    return;
  }

  *arg1++ = '\0';

  /* Match player */
  init_match(player, arg1, TYPE_PLAYER);
  match_neighbor();
  match_here();
  if (power(player, POW_REMOTE)) {
    match_player(NOTHING, NULL);
    match_absolute();
  }

  victim = noisy_match_result();
  if (victim == NOTHING) {
    return;
  }

  if (!controls(player, victim, POW_BAN)) {
    notify(player, tprintf("You don't have the authority to unban %s.",
                          unparse_object(player, victim)));
    return;
  }

  if (strchr(arg2, ' ') || !*arg2) {
    notify(player, "Sorry, channel names cannot have spaces in them.");
    return;
  }

  chan = channel_cache_lookup(arg2);
  if (!chan) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  m = channel_cache_get_member(victim, chan->channel_id);
  if (!m || !m->is_banned) {
    notify(player, tprintf("%s is not banned from channel %s.",
                          unparse_object(player, victim), arg2));
    return;
  }

  /* Clear ban — remove the membership entry entirely */
  mariadb_channel_leave(chan->channel_id, victim);

  notify(player, tprintf("%s may now join channel %s again.",
                        unparse_object(player, victim), arg2));
  notify(victim, tprintf("%s has allowed you to join channel %s again.",
                        unparse_object(victim, player), arg2));
  com_send(arg2, tprintf("%s has been allowed back to this channel.",
                        db[victim].cname));
}


/* ===================================================================
 * Channel Listing and Search
 * =================================================================== */

/**
 * List channels player is on
 * Syntax: +channel list [<player>]
 */
void channel_list(dbref player, char *arg2)
{
  dbref target;

  if (arg2 && *arg2) {
    target = lookup_player(arg2);
    if ((!target) || (target == NOTHING) || (!valid_player(target))) {
      notify(player, "+channel: Invalid player specified.");
      return;
    }
  } else {
    target = player;
  }

  /* Permission check */
  if (!controls(player, target, POW_CHANNEL) &&
      (db[player].pows[0] != CLASS_DIR) &&
      (target != player)) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  channel_member_t *m = channel_cache_get_member_list(target);
  if (m) {
    notify(player, tprintf("+channel: %s is currently on the following channels:",
                          unparse_object(player, target)));

    /* Display formatted list */
    notify(player, "Channel:             Alias:     Status: Owner:");

    while (m) {
      channel_cache_t *chan = channel_cache_lookup_by_id(m->channel_id);

      if (!chan) {
        m = m->next;
        continue;
      }

      if (m->is_banned) {
        m = m->next;
        continue;
      }

      {
        char channame[MESSAGE_BUF_SIZE];
        char filler[26] = "                         ";
        const char *status = m->muted ? "OFF" : "ON ";
        const char *prefix = channel_level_prefix(chan->min_level);
        size_t name_len;

        snprintf(channame, sizeof(channame), "%s%s", prefix, chan->name);
        name_len = strlen(channame);
        if (name_len < 20) {
          filler[20 - name_len] = '\0';
        } else {
          filler[0] = '\0';
        }

        notify(player, tprintf("%s%s %-10.10s %s     %s",
                              channame, filler,
                              (m->alias && *m->alias) ? m->alias : "UNDEFINED",
                              status,
                              GoodObject(chan->owner) ?
                                unparse_object(player, chan->owner) : "???"));
      }

      m = m->next;
    }
  } else {
    notify(player, tprintf("+channel: %s isn't currently on any channels.",
                          unparse_object(player, target)));
    notify(player, "+channel: For a general chatting channel, turn to channel 'public'");
  }
}

/**
 * do_channel_search - List channels matching optional filters
 *
 * Used by @search type=channel and @search channels=<name>.
 * Iterates the MariaDB-backed channel cache.
 *
 * @param player     Player requesting the search
 * @param name_filter If non-NULL, only show channels whose name starts with this
 * @param owner_filter If >= 0, only show channels owned by this dbref;
 *                     ANY_OWNER (-2) or NOTHING (-1) means no owner filter
 * @return Number of channels found
 */
int do_channel_search(dbref player, const char *name_filter, dbref owner_filter)
{
  hash_table_t *ht;
  hash_iterator_t iter;
  const char *hkey;
  void *hval;
  int count = 0;

  ht = (hash_table_t *)channel_cache_get_hash();
  if (!ht || ht->count == 0)
    return 0;

  hash_iterate_init(ht, &iter);
  while (hash_iterate_next(&iter, &hkey, &hval))
  {
    channel_cache_t *chan = (channel_cache_t *)hval;
    if (!chan)
      continue;
    if (owner_filter >= 0 && owner_filter != chan->owner)
      continue;
    if (name_filter != NULL && *name_filter != '\0')
    {
      if (!string_prefix(chan->name, name_filter))
        continue;
    }
    if (count == 0)
    {
      notify(player, "");
      notify(player, "CHANNELS:");
    }
    notify(player, tprintf("%s%s (owner: %s)",
             channel_level_prefix(chan->min_level), chan->name,
             GoodObject(chan->owner) ? db[chan->owner].name : "???"));
    count++;
  }
  return count;
}

/**
 * channel_search - Search and list channels
 *
 * Syntax: +channel search [own|op|all|<channelname>]
 */
void channel_search(dbref player, char *arg2)
{
  hash_table_t *ht;
  hash_iterator_t iter;
  char *key;
  void *value;
  channel_cache_t *chan;
  int level;
  char onoff[4];
  int found_specific = 0;

  /* Validate input */
  if (!(arg2 && *arg2)) {
    notify(player, "+channel: Bad search syntax.");
    return;
  }

  ht = (hash_table_t *)channel_cache_get_hash();
  if (!ht || ht->count == 0) {
    notify(player, "+channel: No channels exist.");
    return;
  }

  /* Determine search level */
  level = 0;
  if (!strncmp(arg2, "own", 3)) {
    level = 1;
  } else if (!strncmp(arg2, "op", 2)) {
    level = 2;
  } else if (!strncmp(arg2, "all", 3)) {
    level = 3;
  }

  notify(player, "+channel search results:");

  /* Iterate through all channels in hash table */
  hash_iterate_init(ht, &iter);
  while (hash_iterate_next(&iter, &key, &value)) {
    char owner[MESSAGE_BUF_SIZE];
    char chan_disp[MESSAGE_BUF_SIZE];
    char filler[26] = "                         ";
    size_t owner_len, chan_len;
    channel_member_t *m;
    const char *prefix;

    chan = (channel_cache_t *)value;
    if (!chan) {
      continue;
    }

    /* Format owner name */
    if (GoodObject(chan->owner)) {
      strncpy(owner, truncate_color(unparse_object(player, chan->owner), 20),
              sizeof(owner) - 1);
    } else {
      strncpy(owner, "???", sizeof(owner) - 1);
    }
    owner[sizeof(owner) - 1] = '\0';

    owner_len = strlen(strip_color(owner));
    if (owner_len < 20) {
      filler[20 - owner_len] = '\0';
    } else {
      filler[0] = '\0';
    }
    strncat(owner, filler, sizeof(owner) - strlen(owner) - 1);

    /* Format channel name with prefix */
    prefix = channel_level_prefix(chan->min_level);
    strncpy(filler, "                        ", sizeof(filler) - 1);
    filler[sizeof(filler) - 1] = '\0';

    snprintf(chan_disp, sizeof(chan_disp), "%s%s", prefix, chan->name);
    chan_len = strlen(chan_disp);
    if (chan_len < 20) {
      filler[20 - chan_len] = '\0';
    } else {
      filler[0] = '\0';
    }
    strncat(chan_disp, filler, sizeof(chan_disp) - strlen(chan_disp) - 1);

    /* Determine on/off status */
    m = channel_cache_get_member(player, chan->channel_id);
    if (m && !m->is_banned) {
      strncpy(onoff, m->muted ? "OFF" : "ON ", sizeof(onoff) - 1);
    } else {
      strncpy(onoff, "   ", sizeof(onoff) - 1);
    }
    onoff[sizeof(onoff) - 1] = '\0';

    /* Filter by search level and display */
    if (level == 0) {
      /* Searching for specific channel name */
      if (!string_compare(chan->name, arg2)) {
        notify(player, tprintf("  %s %s %s", onoff, chan_disp, owner));
        found_specific = 1;
        break;
      }
    } else if (chan->owner == player) {
      /* Player owns this channel */
      notify(player, tprintf("* %s %s %s", onoff, chan_disp, owner));
    } else if ((level > 1) && channel_controls(player, chan)) {
      /* Player is operator on this channel */
      notify(player, tprintf("# %s %s %s", onoff, chan_disp, owner));
    } else if (level == 3) {
      /* Show all channels — directors see everything, others respect visibility */
      if (db[player].pows[0] >= CLASS_DIR) {
        /* Directors/Wizards see all channels, hidden ones marked HID */
        if (!(chan->flags & SEE_OK) ||
            !channel_eval_lock(player, chan->owner, chan->hide_lock)) {
          strncpy(onoff, "HID", sizeof(onoff) - 1);
          onoff[sizeof(onoff) - 1] = '\0';
        }
        notify(player, tprintf("  %s %s %s", onoff, chan_disp, owner));
      } else if ((chan->flags & SEE_OK) &&
          channel_eval_lock(player, chan->owner, chan->hide_lock)) {
        notify(player, tprintf("  %s %s %s", onoff, chan_disp, owner));
      } else if (channel_controls(player, chan)) {
        strncpy(onoff, "HID", sizeof(onoff) - 1);
        onoff[sizeof(onoff) - 1] = '\0';
        notify(player, tprintf("  %s %s %s", onoff, chan_disp, owner));
      }
    }
  }

  /* If searching for specific channel and not found */
  if (level == 0 && !found_specific) {
    notify(player, tprintf("+channel: No channel named '%s' found.", arg2));
  }
}

/**
 * Channel log command (placeholder)
 */
void channel_log(dbref player, char *arg2)
{
  (void)arg2;
  notify(player, "+channel: Log functionality not yet implemented.");
}

/**
 * Set channel color
 * Syntax: +channel color <channel>:<colored_name>
 */
void channel_color(dbref player, char *arg2)
{
  char *channel;
  char *color;
  channel_cache_t *chan;
  channel_member_t *m;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Bad color syntax.");
    return;
  }

  channel = arg2;
  color = strchr(channel, ':');

  if (!color) {
    color = arg2;
    channel = strip_color_nobeep(color);
  } else {
    *color++ = '\0';
  }

  if (!(color && *color)) {
    color = channel;
  }

  chan = channel_cache_lookup(channel);
  if (!chan) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  m = channel_cache_get_member(player, chan->channel_id);
  if (!m) {
    notify(player, "+channel: You must first join the channel before setting its color.");
    return;
  }

  if (strcmp(channel, strip_color_nobeep(color)) != 0) {
    notify(player, "+channel: Colored name does not match channel name.");
    return;
  }

  /* Update player's personalized color name */
  mariadb_channel_set_color(chan->channel_id, player, color);

  /* If owner, also update the channel default cname */
  if (chan->owner == player || channel_controls(player, chan)) {
    mariadb_channel_update_field(chan->channel_id, "cname", color);
  }

  notify(player, tprintf("+channel: %s is now colored as %s", channel, color));
}

/**
 * Find the colored name for a channel
 * @return Colored channel name or default name
 */
char *channel_int_find_color(dbref player, const char *channel)
{
  channel_cache_t *chan;
  channel_member_t *m;

  if (!channel || !*channel) {
    return tprintf("%s", channel ? channel : "");
  }

  chan = channel_cache_lookup(channel);
  if (!chan) {
    return tprintf("%s", channel);
  }

  m = channel_cache_get_member(player, chan->channel_id);
  if (m && m->color_name && *m->color_name) {
    return tprintf("%s", m->color_name);
  }

  return tprintf("%s", chan->cname);
}

/* ===================================================================
 * Channel Membership Helper Functions
 * =================================================================== */

/**
 * Remove player from channel
 * @return 0 if removed, -1 if not on channel (legacy return value)
 */
int channel_int_remove_player(dbref victim, const char *arg2)
{
  channel_cache_t *chan;
  channel_member_t *m;

  if (!arg2 || !*arg2) {
    return -1;
  }

  chan = channel_cache_lookup(arg2);
  if (!chan) {
    return -1;
  }

  m = channel_cache_get_member(victim, chan->channel_id);
  if (!m) {
    return -1;
  }

  mariadb_channel_leave(chan->channel_id, victim);
  return 0;
}

/* ===================================================================
 * Channel On/Off Management
 * =================================================================== */

/**
 * Check if channel is turned on for player
 *
 * @return 1 if on (not muted), 0 if off (muted)
 */
int channel_int_is_on(dbref player, dbref channum)
{
  channel_cache_t *chan;
  channel_member_t *m;

  /* Legacy compatibility: channum was a db[] object ref.
   * Now we look up by name from the cache. */
  if (channum == NOTHING || !GoodObject(channum)) {
    return 0;
  }

  /* Try to find channel by the object's name (for legacy callers) */
  chan = channel_cache_lookup(db[channum].name);
  if (!chan) {
    return 0;
  }

  m = channel_cache_get_member(player, chan->channel_id);
  if (!m || m->is_banned) {
    return 0;
  }

  return m->muted ? 0 : 1;
}

/**
 * Set channel mute/unmute state
 */
void channel_mute(dbref player, const char *arg1, const char *arg2)
{
  channel_cache_t *chan;
  channel_member_t *m;
  int change;

  if (!arg1 || !*arg1 || !arg2 || !*arg2) {
    notify(player, "+channel: Bad on/off syntax.");
    return;
  }

  chan = channel_cache_lookup(arg1);
  if (!chan) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  m = channel_cache_get_member(player, chan->channel_id);
  if (!m) {
    notify(player, "+channel: You must first join the channel before changing its status.");
    return;
  }

  change = (!strcmp("on", arg2)) ? 0 : 1;  /* on = not muted, off = muted */

  if (change == m->muted) {
    notify(player, tprintf("+channel: Channel %s is already %s!",
                          chan->name, arg2));
    return;
  }

  mariadb_channel_set_mute(chan->channel_id, player, change);

  if (change == 0) {
    notify(player, tprintf("+channel: Channel %s unmuted.", chan->name));
  } else {
    notify(player, tprintf("+channel: Channel %s muted.", chan->name));
  }
}

/* ===================================================================
 * Legacy Compatibility - Hash Table Functions
 * =================================================================== */

/**
 * channel_dbinit_clear - Legacy compatibility wrapper
 *
 * In the old system, this cleared the channel hash table.
 * Now the cache is managed by mariadb_channel.c.
 * This is kept for callers in db_io.c that call it during DB load.
 */
void channel_dbinit_clear(void)
{
  /* No-op: cache is now managed by channel_cache_init/channel_cache_load */
}

/**
 * channel_int_add - Legacy compatibility wrapper
 *
 * In the old system, this added a TYPE_CHANNEL db[] object to the hash.
 * Now channels are managed in MariaDB. This is a no-op.
 */
void channel_int_add(dbref channel)
{
  (void)channel;
  /* No-op: channels are now in MariaDB cache */
}

/**
 * channel_int_lookup - Legacy compatibility wrapper
 *
 * Returns NOTHING (channels are no longer db[] objects).
 * Code should be migrated to use channel_cache_lookup() instead.
 */
dbref channel_int_lookup(const char *name)
{
  channel_cache_t *chan;

  if (!name || !*name) {
    return NOTHING;
  }

  chan = channel_cache_lookup(name);
  if (chan) {
    /* Return owner dbref as a reference point for legacy callers
     * that need a valid dbref for permission checks */
    return chan->owner;
  }

  return NOTHING;
}

/**
 * channel_int_delete - Legacy compatibility wrapper
 *
 * No-op: channels are no longer db[] objects.
 */
void channel_int_delete(dbref channel)
{
  (void)channel;
  /* No-op: channels are now in MariaDB cache */
}


/* ===================================================================
 * Channel Communication Wrappers
 * =================================================================== */

/**
 * Wrapper for talking on channels via aliases
 * Used by command parser for =<alias> <message>
 */
void channel_talk(dbref player, char *chan, char *arg1, char *arg2)
{
  char *msg;
  char *channel;

  if (!chan || !*chan) {
    notify(player, "+channel: No channel specified.");
    return;
  }

  channel = channel_int_find(player, chan);

  if (!channel || !*channel) {
    notify(player, "+channel: Invalid channel. Please leave and rejoin it.");
    return;
  }

  /* Reassemble message */
  if (arg2 && *arg2) {
    msg = tprintf("%s = %s", arg1, arg2);
  } else {
    msg = tprintf("%s", arg1);
  }

  do_com(player, channel, msg);
}

/**
 * CHEMIT command - emit to channel as staff
 * Syntax: @chemit <channel>=<message>
 */
void do_chemit(dbref player, char *channel, char *message)
{
  if (!channel || !*channel) {
    notify(player, "What channel?");
    return;
  }

  if (strchr(channel, ' ')) {
    notify(player, "You're spacey.");
    return;
  }

  if (!message || !*message) {
    notify(player, "No message");
    return;
  }

  /* Check if channel exists */
  if (!channel_cache_lookup(channel)) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  /* Send message */
  com_send_int(channel, message, player, 0);
}

/* ===================================================================
 * End of com.c
 * =================================================================== */
