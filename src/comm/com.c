/* com.c - Channel communication system
 * 
 * Handles all +channel related commands and functionality.
 * Converted from K&R C to ANSI C with security improvements.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "db.h"
#include "interface.h"
#include "externs.h"
#include "player.h"

/* ===================================================================
 * Constants and Limits
 * =================================================================== */

#define CHANNEL_BUF_SIZE 1024
#define MESSAGE_BUF_SIZE 4096
#define CHANNEL_LIST_SIZE (1 << 12)  /* must be power of 2 */

/* ===================================================================
 * Data Structures
 * =================================================================== */

struct pl_elt {
  dbref channel;
  struct pl_elt *prev;
  struct pl_elt *next;
};

/* Channel hash table */
static dbref hash_chan_fun_table[256];
static int hft_chan_initialized = 0;
static struct pl_elt *channel_list[CHANNEL_LIST_SIZE];
static int pl_used = 0;

/* ===================================================================
 * Utility Macros
 * =================================================================== */

#define DOWNCASE(x) to_lower(x)

#ifndef GoodObject
#define GoodObject(x) ((x) >= 0 && (x) < db_top && \
                      (Typeof(x) != NOTYPE) && \
                      !(db[x].flags & GOING))
#endif

/* ===================================================================
 * Forward Declarations
 * =================================================================== */


//void do_channel_create(dbref, char *);
//void do_channel_op(dbref, char *);
//void do_channel_lock(dbref, char *);
//void do_channel_password(dbref, char *);
//void do_channel_join(dbref, char *);
//void do_channel_leave(dbref, char *);
//void do_channel_default(dbref, char *);
//void do_channel_alias(dbref, char *);
//void do_channel_boot(dbref, char *);
//void do_channel_list(dbref, char *);
//void do_channel_search(dbref, char *);
//void do_channel_log(dbref, char *);
//void do_channel_ban(dbref, char *);
//void do_channel_unban(dbref, char *);
//void do_channel_color(dbref, char *);
//char *find_channel_alias(dbref, char *);
//char *find_channel_int(dbref, char *, int);
//char *find_channel_only(dbref, char *);
//int is_on_channel(dbref, char *);
//int is_on_channel_only(dbref, char *);
//int is_on_channel_int(dbref, char *, int);
//void list_channels(dbref, dbref);
//char *find_channel_color(dbref, char *);
//int remove_from_channel(dbref, char *);
//int is_in_attr(dbref, char *, ATTR *);
//char *remove_from_attr(dbref, int, ATTR *);
//char *remove_from_ch_attr(dbref, int);
//static dbref hash_chan_function(char *);
//int channel_onoff_chk(dbref, dbref);
//void channel_onoff_set(dbref, char *, char *);
//void do_chemit(dbref player, char *channel, char *message);

static int is_banned(dbref, const char *);
static void com_who(dbref, const char *);
static dbref hash_chan_function(const char *);
static void init_chan_hft(void);

/* Channel management functions */
void do_channel_create(dbref, char *);
void do_channel_destroy(dbref, char *);
void do_channel_op(dbref, char *);
void do_channel_lock(dbref, char *);
void do_channel_password(dbref, char *);
void do_channel_join(dbref, char *);
void do_channel_leave(dbref, char *);
void do_channel_default(dbref, char *);
void do_channel_alias(dbref, char *);
void do_channel_boot(dbref, char *);
void do_channel_list(dbref, char *);
void do_channel_search(dbref, char *);
void do_channel_log(dbref, char *);
void do_channel_ban(dbref, char *);
void do_channel_unban(dbref, char *);
void do_channel_color(dbref, char *);

/* Helper functions */
char *find_channel_alias(dbref, const char *);
char *find_channel_int(dbref, const char *, int);
char *find_channel_only(dbref, const char *);
char *is_channel_alias(dbref, const char *);
int is_on_channel(dbref, const char *);
int is_on_channel_only(dbref, const char *);
int is_on_channel_int(dbref, const char *, int);
void list_channels(dbref, dbref);
char *find_channel_color(dbref, const char *);
int remove_from_channel(dbref, const char *);
int is_in_attr(dbref, const char *, ATTR *);
char *remove_from_attr(dbref, int, ATTR *);
char *remove_from_ch_attr(dbref, int);
int channel_onoff_chk(dbref, dbref);
void channel_onoff_set(dbref, const char *, const char *);
void do_chemit(dbref, char *, char *);
void channel_talk(dbref, char *, char *, char *);
void com_send_int(char *, char *, dbref, int);



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
 * @param channel Channel name
 * @param message Message to send
 * @param player Player sending (0 for system message)
 * @param hidden 1 if hidden from blacklisted users
 */
void com_send_int(char *channel, char *message, dbref player, int hidden)
{
  struct descriptor_data *d;
  char *output_str, *output_str2;

  /* Validate input */
  if (!channel || !*channel || !message) {
    return;
  }

  /* Loop through all descriptors */
  for (d = descriptor_list; d; d = d->next) {
    /* Check if descriptor is valid and connected */
    if (!d || d->state != CONNECTED || d->player <= 0) {
      continue;
    }

    /* Check if player is on this channel */
    if (is_on_channel_only(d->player, channel) < 0) {
      continue;
    }

    /* Check if channel is enabled for this player */
    if (channel_onoff_chk(d->player, lookup_channel(channel)) != 1) {
      continue;
    }

    /* Format the message with channel name */
    output_str = tprintf("[%s] %s",
                        find_channel_color(d->player, channel),
                        message);

    /* Check visibility permissions */
    if (hidden && !could_doit(real_owner(d->player), real_owner(player), A_LHIDE)) {
      continue;
    }

#ifdef USE_BLACKLIST
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
#endif

    /* Add puppet indicator if needed */
    if ((db[d->player].flags & PUPPET) && (player > 0) && (player != d->player)) {
      output_str2 = tprintf("%s  [#%ld/%s]", output_str, db[player].owner,
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
static void com_who(dbref player, const char *channel)
{
  struct descriptor_data *d;
  dbref channum;
  int hidden = 0;
  int visible = 0;

  if (!channel || !*channel) {
    notify(player, "+channel: No channel specified.");
    return;
  }

  channum = lookup_channel(channel);

  if (channum == NOTHING) {
    notify(player, "+channel: Sorry, this channel doesn't exist.");
    return;
  }

  /* Check if channel is dark */
  if ((db[channum].flags & DARK) &&
      (!controls(player, db[channum].owner, POW_CHANNEL)) &&
      (!group_controls(player, channum))) {
    notify(player, "+channel: Sorry, this channel is set DARK.");
    return;
  }

  /* Count visible and hidden users */
  for (d = descriptor_list; d; d = d->next) {
    if (d->state == CONNECTED && d->player > 0 &&
        is_on_channel_only(d->player, channel) >= 0) {

      if ((could_doit(real_owner(d->player), real_owner(player), A_LHIDE))
#ifdef USE_BLACKLIST
          && (((!strlen(atr_get(real_owner(d->player), A_BLACKLIST))) &&
               (!strlen(atr_get(real_owner(player), A_BLACKLIST)))) ||
              (!((could_doit(real_owner(player), real_owner(d->player), A_BLACKLIST)) &&
                 (could_doit(real_owner(d->player), real_owner(player), A_BLACKLIST)))))
#endif
         ) {
        notify(player, tprintf("%s is on channel %s.",
                              unparse_object(player, d->player), channel));
        visible++;
      } else {
        hidden++;
      }
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
  char *curr;

  onoff = 0;

  /* Default channel handling */
  if (!*arg1) {
    char *buf_local;
    char *alias;
    char *q;
    char *next;

    next = curr = buf_local = tprintf("%s ", atr_get(player, A_CHANNEL));

    next = strchr(curr, ' ');
    if (next) {
      *next++ = '\0';
    }

    alias = strchr(curr, ':');
    if (alias && *alias) {
      *alias++ = '\0';
      q = strchr(alias, ':');
      if (q && *q) {
        *q++ = '\0';
        onoff = atoi(q);
      } else {
        do_channel_alias(player, tprintf("%s:%s", curr, alias));
        onoff = 1;
      }
    } else {
      do_channel_alias(player, tprintf("%s:%s", curr, curr));
      onoff = 1;
    }
  } else {
    curr = arg1;
    onoff = channel_onoff_chk(player, lookup_channel(curr));
  }

  nocolor = strip_color_nobeep(curr);
  if (!*nocolor) {
    notify(player, "No channel.");
    return;
  }

  if (strchr(nocolor, ' ')) {
    notify(player, "You're spacey.");
    return;
  }

  /* Special WHO command */
  if (!string_compare(arg2, "who")) {
    com_who(player, nocolor);
    return;
  }

  /* Permission checks */
#ifdef ALLOW_COM_NP
  if (Typeof(player) == TYPE_CHANNEL) {
    notify(player, "+channel: Channels can't talk on channels. Imagine the Spam.");
    return;
  }
#else
  if (Typeof(player) != TYPE_PLAYER) {
    notify(player, "+channel: Non-players cannot talk on channels. Sorry.");
    return;
  }
#endif

  if (is_banned(player, nocolor) >= 0) {
    notify(player, "+channel: You have been banned from that channel.");
    return;
  }

  if (lookup_channel(nocolor) == NOTHING) {
    notify(player, "+channel: Sorry. You have old channels defined. Removing old channel..");
    do_channel_leave(player, nocolor);
    return;
  }

  /* Check speak lock */
  if (((db[lookup_channel(nocolor)].flags & HAVEN) ||
       (!could_doit(player, lookup_channel(nocolor), A_SLOCK))) &&
      (!controls(player, db[lookup_channel(nocolor)].owner, POW_CHANNEL)) &&
      (!group_controls(player, lookup_channel(nocolor)))) {
    notify(player, "+channel: You do not have permission to speak on this channel.");
    return;
  }

  /* On/Off toggle */
  if (!strcmp("on", arg2) || !strcmp("off", arg2)) {
    channel_onoff_set(player, nocolor, arg2);
    return;
  }

  /* Check if channel is on */
  if (onoff != 1) {
    notify(player, tprintf("+channel: Channel %s is currently turned off. Sorry.", curr));
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
  if (is_on_channel_only(player, nocolor) < 0) {
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
char *find_channel_alias(dbref player, const char *channel)
{
  char *clist, *clbegin, *alias, *s;

  if (!channel || !*channel) {
    return NULL;
  }

  clbegin = clist = tprintf("%s ", atr_get(player, A_CHANNEL));

  while (*clist) {
    char *space = strchr(clist, ' ');
    if (space) {
      *space = '\0';
    }

    alias = strchr(clist, ':');
    if (alias && *alias) {
      *alias++ = '\0';
      s = strchr(alias, ':');
      if (s && *s) {
        *s++ = '\0';
      }

      if (!strcmp(strip_color_nobeep(clist), channel)) {
        return tprintf("%s", alias);
      }
      clist += strlen(clist) + strlen(alias) + 2;
    } else {
      clist += strlen(clist) + 1;
    }
  }

  return NULL;
}

/**
 * Check if a string is an alias for a channel
 * @return Channel name if alias exists, NULL otherwise
 */
char *is_channel_alias(dbref player, const char *al)
{
  char *clist, *clbegin, *alias, *s;

  if (!al || !*al) {
    return NULL;
  }

  clbegin = clist = tprintf("%s ", atr_get(player, A_CHANNEL));

  while (*clist) {
    char *space = strchr(clist, ' ');
    if (space) {
      *space = '\0';
    }

    alias = strchr(clist, ':');
    if (alias && *alias) {
      *alias++ = '\0';
      s = strchr(alias, ':');
      if (s && *s) {
        *s++ = '\0';
      }

      if (!strcmp(alias, al)) {
        return tprintf("%s", alias);
      }
      clist += strlen(clist) + strlen(alias) + 2;
    } else {
      clist += strlen(clist) + 1;
    }
  }

  return NULL;
}

/**
 * Find channel name (wrapper with alias checking)
 */
char *find_channel(dbref player, const char *chan)
{
  return find_channel_int(player, chan, 1);
}

/**
 * Find channel name (exact match only, no alias)
 */
char *find_channel_only(dbref player, const char *chan)
{
  return find_channel_int(player, chan, 0);
}

/**
 * Find channel in player's channel list
 * @param level 1 to check aliases, 0 for exact match only
 * @return Channel name or NULL
 */
char *find_channel_int(dbref player, const char *chan, int level)
{
  char *clist, *clbegin, *alias, *s = NULL;

  if (!chan || !*chan) {
    return NULL;
  }

  clbegin = clist = tprintf("%s ", atr_get(player, A_CHANNEL));

  while (*clist) {
    char *space = strchr(clist, ' ');
    if (space) {
      *space = '\0';
    }

    alias = strchr(clist, ':');
    if (alias && *alias) {
      *alias++ = '\0';
      s = strchr(alias, ':');
      if (s && *s) {
        *s++ = '\0';
      }
    }

    if ((!strcmp(strip_color_nobeep(clist), chan)) ||
        ((level == 1) && (alias && *alias && (!strcmp(alias, chan))))) {
      if (lookup_channel(clist)) {
        return tprintf("%s", clist);
      }
    }

    if (alias && *alias) {
      if (s && *s) {
        clist += strlen(clist) + strlen(alias) + strlen(s) + 3;
      } else {
        clist += strlen(clist) + strlen(alias) + 2;
      }
    } else {
      clist += strlen(clist) + 1;
    }
  }

  return NULL;
}

/* ===================================================================
 * Channel Membership Checking
 * =================================================================== */

/**
 * Check if player is on a channel (with alias checking)
 * @return Position in channel list or -1
 */
int is_on_channel(dbref player, const char *chan)
{
  if (chan && *chan) {
    return is_on_channel_int(player, chan, 1);
  }
  return -1;
}

/**
 * Check if player is on a channel (exact match only)
 * @return Position in channel list or -1
 */
int is_on_channel_only(dbref player, const char *chan)
{
  if (chan && *chan) {
    return is_on_channel_int(player, chan, 0);
  }
  return -1;
}

/**
 * Internal channel membership check
 * @param level 1 to check aliases, 0 for exact match
 * @return Position in string or -1 if not found
 */
int is_on_channel_int(dbref player, const char *chan, int level)
{
  char *clist, *clbegin, *alias, *s = NULL;

  if (!chan || !*chan) {
    return -1;
  }

  clbegin = clist = tprintf("%s ", atr_get(player, A_CHANNEL));

  while (*clist) {
    char *space = strchr(clist, ' ');
    if (space) {
      *space = '\0';
    }

    alias = strchr(clist, ':');
    if (alias && *alias) {
      *alias++ = '\0';
      s = strchr(alias, ':');
      if (s && *s) {
        *s++ = '\0';
      }
    }

    if ((!strcmp(strip_color_nobeep(clist), chan)) ||
        ((level == 1) && (alias && *alias && (!strcmp(alias, chan))))) {
      if (lookup_channel(clist)) {
        return (clist - clbegin);
      }
    }

    if (alias && *alias) {
      if (s && *s) {
        clist += strlen(clist) + strlen(alias) + strlen(s) + 3;
      } else {
        clist += strlen(clist) + strlen(alias) + 2;
      }
    } else {
      clist += strlen(clist) + 1;
    }
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
    do_channel_list(player, "");
    return;
  }

  /* Modern subcommand syntax */
  if (!strncmp(arg1, "create", 6)) {
    do_channel_create(player, arg2);
  } else if (!strncmp(arg1, "destroy", 7)) {
    do_channel_destroy(player, arg2);
  } else if (!strncmp(arg1, "op", 2)) {
    do_channel_op(player, arg2);
  } else if (!strncmp(arg1, "lock", 4)) {
    do_channel_lock(player, arg2);
  } else if (!strncmp(arg1, "password", 8)) {
    do_channel_password(player, arg2);
  } else if (!strncmp(arg1, "join", 4)) {
    do_channel_join(player, arg2);
  } else if (!strncmp(arg1, "leave", 5)) {
    do_channel_leave(player, arg2);
  } else if (!strncmp(arg1, "default", 7)) {
    do_channel_default(player, arg2);
  } else if (!strncmp(arg1, "alias", 5)) {
    do_channel_alias(player, arg2);
  } else if (!strncmp(arg1, "boot", 4)) {
    do_channel_boot(player, arg2);
  } else if (!strncmp(arg1, "list", 4)) {
    do_channel_list(player, arg2);
  } else if (!strncmp(arg1, "search", 6)) {
    do_channel_search(player, arg2);
  } else if (!strncmp(arg1, "log", 3)) {
    do_channel_log(player, arg2);
  } else if (!strncmp(arg1, "ban", 3)) {
    do_channel_ban(player, arg2);
  } else if (!strncmp(arg1, "unban", 5)) {
    do_channel_unban(player, arg2);
  } else if (!strncmp(arg1, "color", 5)) {
    do_channel_color(player, arg2);
  }
  /* Legacy syntax support */
  else if (*arg1 == '+') {
    arg1++;
    do_channel_join(player, arg1);
  } else if (*arg1 == '-') {
    arg1++;
    do_channel_leave(player, arg1);
  } else if (!*arg2) {
    do_channel_default(player, arg1);
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
int ok_channel_name(char *name)
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
      strlen(name) > channel_name_limit) {
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
  if (lookup_channel(name) != NOTHING) {
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
void do_channel_create(dbref player, char *arg2)
{
  dbref channel;
  ptype k;
  char *alias;
  long cost = channel_cost;
  char *nocolor;

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

  /* Validate names */
  if (!ok_channel_name(nocolor) ||
      (alias && *alias && !ok_channel_name(alias))) {
    notify(player, "+channel: That's a silly name for a channel!");
    return;
  }

  /* Check permissions for special prefix channels */
  if (!db[player].pows) {
    return;
  }

  k = *db[player].pows;

  if (*nocolor == '*' &&
      !(k == CLASS_ADMIN || k == CLASS_DIR)) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  if (*nocolor == '.' &&
      !(k == CLASS_DIR || k == CLASS_ADMIN || k == CLASS_BUILDER)) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  if (*nocolor == '_' &&
      !(k == CLASS_DIR || k == CLASS_ADMIN || k == CLASS_BUILDER ||
        k == CLASS_OFFICIAL || k == CLASS_JUNOFF)) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  /* Check if player can pay */
  if (!can_pay_fees(def_owner(player), cost, QUOTA_COST)) {
    notify(player, "+channel: You don't have enough credits or quota.");
    return;
  }

  /* Create the channel object */
  channel = new_object();

  /* Initialize channel */
  SET(db[channel].name, nocolor);
  SET(db[channel].cname, arg2);
  db[channel].zone = NOTHING;
  db[channel].location = channel;
  db[channel].link = channel;
  db[channel].owner = def_owner(player);
  s_Pennies(channel, (long)OBJECT_ENDOWMENT(cost));
  db[channel].flags = TYPE_CHANNEL;
  db[channel].flags |= SEE_OK;

  /* Endow the object */
  if (Pennies(channel) > MAX_OBJECT_ENDOWMENT) {
    s_Pennies(channel, (long)MAX_OBJECT_ENDOWMENT);
  }

  atr_add(channel, A_LASTLOC, tprintf("%ld", channel));
  moveto(channel, channel);
  db[channel].i_flags &= I_MARKED;

  /* Add to channel hash table */
  add_channel(channel);

  notify(player, tprintf("+channel: %s created.",
                        unparse_object(player, channel)));

  /* Auto-join creator to channel */
  if (alias && *alias) {
    do_channel_join(player, tprintf("%s:%s", db[channel].name, alias));
  } else {
    do_channel_join(player, db[channel].name);
  }
}

/**
 * Destroy a channel
 * Syntax: +channel destroy <name>
 */
void do_channel_destroy(dbref player, char *name)
{
  dbref victim;
  char *plist;
  char *s;

  if (!name || !*name) {
    notify(player, "+channel: Destroy what?");
    return;
  }

  victim = lookup_channel(name);

  if (victim == NOTHING) {
    notify(player, "+channel: Invalid channel name.");
    return;
  }

  if (Typeof(victim) != TYPE_CHANNEL) {
    notify(player, "+channel: This isn't a channel!");
    return;
  }

  /* Permission checks */
  if (Typeof(player) != TYPE_PLAYER) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  if ((db[victim].owner != player) &&
      !power(player, POW_CHANNEL) &&
      !power(player, POW_NUKE)) {
    notify(player, tprintf("+channel: %s", perm_denied()));
    return;
  }

  if (!controls(player, victim, POW_NUKE)) {
    log_important(tprintf("%s failed to: +channel destroy=%s",
                         unparse_object_a(player, player),
                         unparse_object_a(victim, victim)));
    notify(player, perm_denied());
    return;
  }

  if (owns_stuff(victim)) {
    notify(player, "+channel: Problem. Channel owns something. That's bad.");
    return;
  }

  /* Boot all players off the channel */
  plist = tprintf("%s ", atr_get(victim, A_CHANNEL));

  while (plist && *plist) {
    s = strchr(plist, ' ');
    if (s && *s) {
      *s++ = '\0';
    }

    dbref p = atol(plist);
    if (GoodObject(p)) {
      notify(p, tprintf("+channel: %s is being destroyed. You must leave now.",
                       db[victim].cname));
      remove_from_channel(p, db[victim].name);
    }
    plist = s;
  }

  /* Clean up and destroy */
  do_halt(victim, "", "");
  db[victim].flags = TYPE_THING;
  db[victim].owner = root;
  delete_channel(victim);
  destroy_obj(victim, 1);

  notify(player, tprintf("+channel: %s destroyed.", db[victim].cname));
  log_important(tprintf("%s executed: +channel destroy=%s",
                       unparse_object_a(player, player),
                       unparse_object_a(victim, victim)));
}

/* ===================================================================
 * Channel Administrative Commands
 * =================================================================== */

/**
 * Set or remove channel operator status
 * Syntax: +channel op <channel>:<player> or <channel>:!<player>
 */
void do_channel_op(dbref player, char *arg2)
{
  char *user;
  dbref target;
  dbref channum;
  int yesno = 1;
  int place;

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

  channum = lookup_channel(arg2);
  if (channum == NOTHING) {
    notify(player, "+channel: Invalid channel specified in op operation.");
    return;
  }

  /* Check permissions */
  if (!controls(player, db[channum].owner, POW_CHANNEL) &&
      !group_controls(player, channum)) {
    notify(player, "+channel: You don't have permission to set ops on this channel.");
    return;
  }

  target = lookup_player(user);
  if (target == NOTHING) {
    notify(player, "+channel: Invalid player specified in op operation.");
    return;
  }

  place = is_in_attr(channum, tprintf("#%ld", target), A_USERS);

  if (yesno == 0) {
    /* Removing op status */
    if (place != NOTHING) {
      atr_add(channum, A_USERS, remove_from_attr(channum, place, A_USERS));
      notify(player, tprintf("+channel: %s is no longer an op on %s",
                            unparse_object(player, target),
                            unparse_object(player, channum)));
    } else {
      notify(player, tprintf("+channel: %s was not an op on %s anyway!",
                            unparse_object(player, target),
                            unparse_object(player, channum)));
    }
  } else {
    /* Adding op status */
    if (place != NOTHING) {
      notify(player, tprintf("+channel: %s is already an op on %s!",
                            unparse_object(player, target),
                            unparse_object(player, channum)));
    } else {
      char *tmp = atr_get(channum, A_USERS);

      if (tmp && strlen(tmp)) {
        atr_add(channum, A_USERS, tprintf("%s #%ld", tmp, target));
      } else {
        atr_add(channum, A_USERS, tprintf("#%ld", target));
      }

      notify(player, tprintf("+channel: %s is now an op on %s",
                            unparse_object(player, target),
                            unparse_object(player, channum)));
    }
  }
}

/**
 * Set channel lock (placeholder - needs implementation)
 */
void do_channel_lock(dbref player, char *arg2)
{
  notify(player, "+channel: Lock functionality not yet implemented.");
}

/**
 * Set channel password
 * Syntax: +channel password <channel>:<password>
 */
void do_channel_password(dbref player, char *arg2)
{
  char *chan;
  char *password;
  dbref channel;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Bad password syntax.");
    return;
  }

  chan = arg2;
  password = strchr(arg2, ':');

  if (!password) {
    notify(player, "+channel: Bad password syntax.");
    return;
  }

  *password++ = '\0';

  channel = lookup_channel(chan);

  if (!channel || (channel == NOTHING)) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  /* Check permissions */
  if (!controls(player, db[channel].owner, POW_CHANNEL) &&
      !group_controls(player, channel)) {
    notify(player, "+channel: You do not have permission to set the password on this channel.");
    return;
  }

  /* Set or clear password */
  if (password && *password) {
    s_Pass(channel, crypt(password, "XX"));
    notify(player, tprintf("+channel: %s password changed.",
                          unparse_object(player, channel)));
  } else {
    s_Pass(channel, "");
    notify(player, tprintf("+channel: %s password erased.",
                          unparse_object(player, channel)));
  }
}

/* ===================================================================
 * Channel Membership Management
 * =================================================================== */

/**
 * Join a channel
 * Syntax: +channel join <n>[:<alias>[:<password>]]
 */
void do_channel_join(dbref player, char *arg2)
{
  ptype k;
  char buf[MESSAGE_BUF_SIZE];
  char *alias;
  char *password;
  char *cryptpass;
  int pmatch = 0;
  dbref channum;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Join what?");
    return;
  }

  if (strchr(arg2, ' ')) {
    notify(player, "Sorry, channel names cannot have spaces in them.");
    return;
  }

#ifdef ALLOW_COM_NP
  if (Typeof(player) == TYPE_CHANNEL) {
    notify(player, "+channel: Channels can't talk on channels. Imagine the Spam.");
    return;
  }
#else
  if (Typeof(player) != TYPE_PLAYER) {
    notify(player, "+channel: Non-players cannot be on channels. Sorry.");
    return;
  }
#endif

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

  channum = lookup_channel(arg2);

  if (channum == NOTHING) {
    notify(player, tprintf("+channel: Channel %s does not exist.", arg2));
    return;
  }

  /* Check if already on channel */
  if (is_on_channel(player, arg2) != NOTHING) {
    if (is_on_channel_only(player, alias) != NOTHING) {
      notify(player, "You are already on that channel. Try +ch alias to change aliases.");
      return;
    } else {
      do_channel_alias(player, tprintf("%s:%s", arg2, alias));
      return;
    }
  }

  if (is_channel_alias(player, alias)) {
    notify(player, tprintf("+channel: You're already using that alias. (%s)", alias));
    return;
  }

  /* Check if banned */
  if (is_banned(player, arg2) >= 0) {
    notify(player, "You have been banned from that channel.");
    return;
  }

  /* Permission checks for special channels */
  if (!pmatch) {
    if (!db[player].pows) {
      return;
    }

    k = *db[player].pows;

    if (*arg2 == '*' && !(k == CLASS_ADMIN || k == CLASS_DIR)) {
      notify(player, perm_denied());
      return;
    }

    if (*arg2 == '.' &&
        !(k == CLASS_DIR || k == CLASS_ADMIN || k == CLASS_BUILDER)) {
      notify(player, perm_denied());
      return;
    }

    if (*arg2 == '_' &&
        !(k == CLASS_DIR || k == CLASS_ADMIN || k == CLASS_BUILDER ||
          k == CLASS_OFFICIAL || k == CLASS_JUNOFF)) {
      notify(player, perm_denied());
      return;
    }

    /* Check channel lock */
    if (!could_doit(player, channum, A_LOCK) &&
        !controls(player, db[channum].owner, POW_CHANNEL)) {
      notify(player, "+channel: Sorry, you are not permitted to join this channel.");
      return;
    }
  }

  /* Add to player's channel list */
  if (!*atr_get(player, A_CHANNEL)) {
    atr_add(player, A_CHANNEL, tprintf("%s:%s:1", arg2, alias));
  } else {
    atr_add(player, A_CHANNEL, tprintf("%s %s:%s:1",
                                       atr_get(player, A_CHANNEL),
                                       arg2, alias));
  }

  /* Add to channel's player list */
  if (!*atr_get(channum, A_CHANNEL)) {
    atr_add(channum, A_CHANNEL, tprintf("%ld", player));
  } else {
    atr_add(channum, A_CHANNEL, tprintf("%s %ld",
                                        atr_get(channum, A_CHANNEL),
                                        player));
  }

  /* Announce join unless channel is QUIET */
  if (!(db[channum].flags & QUIET)) {
    char sayit[CHANNEL_BUF_SIZE];

    strncpy(sayit, atr_get(channum, A_OENTER), sizeof(sayit) - 1);
    sayit[sizeof(sayit) - 1] = '\0';

    if (strlen(sayit)) {
      char buf2[MESSAGE_BUF_SIZE];
      char *p;

      pronoun_substitute(buf2, player, sayit, channum);
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
  if (strlen(atr_get(channum, A_DESC))) {
    notify(player, tprintf("+channel topic: %s", atr_get(channum, A_DESC)));
  }
}

/**
 * Leave a channel
 * Syntax: +channel leave <n>
 */
void do_channel_leave(dbref player, char *arg2)
{
  int i, j;
  char buf[MESSAGE_BUF_SIZE];
  char *pattr;
  char *cattr;
  dbref channum;

  if (!arg2 || !*arg2) {
    notify(player, "+channel: Leave what?");
    return;
  }

  if (strchr(arg2, ' ')) {
    notify(player, "Sorry, channel names cannot have spaces in them.");
    return;
  }

  i = is_on_channel(player, arg2);
  if ((i < 0) || (!find_channel_only(player, arg2))) {
    notify(player, "You aren't on that channel.");
    return;
  }

  channum = lookup_channel(arg2);

  /* Announce leave unless channel is QUIET */
  if (channum != NOTHING && !(db[channum].flags & QUIET)) {
    char sayit[CHANNEL_BUF_SIZE];

    strncpy(sayit, atr_get(channum, A_OLEAVE), sizeof(sayit) - 1);
    sayit[sizeof(sayit) - 1] = '\0';

    if (strlen(sayit)) {
      char buf2[MESSAGE_BUF_SIZE];
      char *p;

      pronoun_substitute(buf2, player, sayit, player);
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

  /* Remove from player's list */
  pattr = remove_from_ch_attr(player, i);

  /* Remove from channel's list */
  if (channum == NOTHING) {
    notify(player, "+channel: Removing old channel");
  } else {
    j = is_on_channel(channum, tprintf("%ld", player));
    if (j != NOTHING) {
      cattr = remove_from_ch_attr(channum, j);
      atr_add(channum, A_CHANNEL, cattr);
    }
  }

  atr_add(player, A_CHANNEL, pattr);
}

/**
 * Set default channel
 * Syntax: +channel default <n>
 */
void do_channel_default(dbref player, char *arg1)
{
  dbref channum;
  int onoff;
  int i;
  char *alias;

  if (!arg1 || !*arg1) {
    notify(player, "+channel: Set what as default?");
    return;
  }

  i = is_on_channel_only(player, arg1);

  if (i < 0) {
    notify(player, "+channel default: Need to join the channel first.");
    return;
  }

  channum = lookup_channel(arg1);
  if (channum == NOTHING) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  alias = find_channel_alias(player, arg1);
  if (!(alias && *alias)) {
    alias = arg1;
  }

  onoff = channel_onoff_chk(player, channum);

  /* Remove from current position */
  remove_from_channel(player, arg1);

  /* Add at beginning */
  if (!*atr_get(player, A_CHANNEL)) {
    atr_add(player, A_CHANNEL, tprintf("%s:%s:%d", arg1, alias, onoff));
  } else {
    atr_add(player, A_CHANNEL, tprintf("%s:%s:%d %s",
                                       arg1, alias, onoff,
                                       atr_get(player, A_CHANNEL)));
  }

  notify(player, tprintf("+channel default: %s is now your default channel.", arg1));
}

/**
 * Set channel alias
 * Syntax: +channel alias <channel>:<alias>
 */
void do_channel_alias(dbref player, char *arg2)
{
  char *channel;
  char *alias;
  char *s;
  int pos;
  char *old1, *old2, *new;

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

  /* Check for on/off state */
  s = strchr(alias, ':');
  if (s && *s) {
    *s++ = '\0';
    if (!(s && *s)) {
      s = "1";
    }
  } else {
    s = "1";
  }

  pos = is_on_channel_only(player, channel);
  if (pos == NOTHING) {
    notify(player, "+channel: You must first join the channel before setting its alias.");
    return;
  }

  /* Build new channel list */
  old1 = tprintf("%s", atr_get(player, A_CHANNEL));
  old2 = old1 + pos;

  if (old2 && *old2) {
    *old2++ = '\0';
    old2 = strchr(old2, ' ');
    if (old2 && *old2) {
      old2++;
    }

    if (old2 && *old2) {
      new = tprintf("%s%s:%s:%s %s", old1, channel, alias, s, old2);
    } else {
      new = tprintf("%s%s:%s:%s", old1, channel, alias, s);
    }
  } else {
    new = tprintf("%s%s:%s:%s", old1, channel, alias, s);
  }

  atr_add(player, A_CHANNEL, new);
  notify(player, tprintf("Alias for channel %s is now %s", channel, alias));
}

/**
 * Boot a player from a channel
 * Syntax: +channel boot <channel>:<player>
 */
void do_channel_boot(dbref player, char *channel)
{
  char *vic;
  dbref victim;
  dbref channum;

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

  channum = lookup_channel(channel);
  if (channum == NOTHING) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  /* Check permissions */
  if (!controls(player, db[channum].owner, POW_CHANNEL) &&
      !group_controls(player, channum)) {
    notify(player, "+channel: You don't have permission to boot from this channel.");
    return;
  }

  victim = lookup_player(vic);
  if (victim == NOTHING) {
    notify(player, "+channel: Invalid player.");
    return;
  }

  if (remove_from_channel(victim, channel) != NOTHING) {
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
 * Check if player is banned from a channel
 * @return Position in ban list or -1
 */
static int is_banned(dbref player, const char *chan)
{
  char *blist, *blbegin, *buf;

  if (!chan || !*chan) {
    return -1;
  }

  buf = tprintf("%s ", atr_get(player, A_BANNED));
  blbegin = blist = buf;

  while (*blist) {
    char *space = strchr(blist, ' ');
    if (space) {
      *space = '\0';
    }

    if (!strcmp(blist, chan)) {
      return (blist - blbegin);
    }

    blist += strlen(blist) + 1;
  }

  return -1;
}

/**
 * Ban a player from a channel
 * Syntax: +channel ban <channel>:<player>
 */
void do_channel_ban(dbref player, char *arg2)
{
  dbref victim;
  char *arg1;
  dbref channum;

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

  channum = lookup_channel(arg2);
  if (channum == NOTHING) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  if (is_banned(victim, arg2) >= 0) {
    notify(player, tprintf("%s has already been banned from %s.",
                          unparse_object(player, victim), arg2));
    return;
  }

  /* Add to ban list */
  if (!*atr_get(victim, A_BANNED)) {
    atr_add(victim, A_BANNED, arg2);
  } else {
    atr_add(victim, A_BANNED, tprintf("%s %s", arg2,
                                      atr_get(victim, A_BANNED)));
  }

  /* Remove from channel */
  remove_from_channel(victim, arg2);

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
void do_channel_unban(dbref player, char *arg2)
{
  dbref victim;
  int i;
  char *end;
  char buf[MESSAGE_BUF_SIZE];
  char buf2[MESSAGE_BUF_SIZE];
  char *arg1;

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

  i = is_banned(victim, arg2);
  if (i < 0) {
    notify(player, tprintf("%s is not banned from channel %s.",
                          unparse_object(player, victim), arg2));
    return;
  }

  /* Remove from ban list */
  strncpy(buf, atr_get(victim, A_BANNED), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  end = strchr(buf + i, ' ');
  if (!end) {
    end = strchr(buf + i, '\0');
  } else {
    end++;
  }

  strncpy(buf2, atr_get(victim, A_BANNED), sizeof(buf2) - 1);
  buf2[sizeof(buf2) - 1] = '\0';

  strcpy(buf2 + i, end);

  /* Trim trailing space */
  end = strchr(buf2, '\0');
  if (end > buf2 && *--end == ' ') {
    *end = '\0';
  }

  atr_add(victim, A_BANNED, buf2);

  notify(player, tprintf("%s may now join channel %s again.",
                        unparse_object(player, victim), arg2));
  notify(victim, tprintf("%s has allowed you to join channel %s again.",
                        unparse_object(victim, player), arg2));
  com_send(arg2, tprintf("%s has been allowed back to this channel.",
                        db[victim].cname));
}

/**
 * Deprecated +ban command - redirects to +channel ban
 */
void do_ban(dbref player, char *arg1, char *arg2)
{
  notify(player, "+channel: +ban deprecated, use +channel ban");
  do_channel_ban(player, tprintf("%s:%s", arg1, arg2));
}

/**
 * Deprecated +unban command - redirects to +channel unban
 */
void do_unban(dbref player, char *arg1, char *arg2)
{
  notify(player, "+channel: +unban deprecated, use +channel unban");
  do_channel_unban(player, tprintf("%s:%s", arg1, arg2));
}

/* ===================================================================
 * Channel Listing and Search
 * =================================================================== */

/**
 * List channels player is on
 * Syntax: +channel list [<player>]
 */
void do_channel_list(dbref player, char *arg2)
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

  if (*atr_get(target, A_CHANNEL)) {
    notify(player, tprintf("+channel: %s is currently on the following channels:",
                          unparse_object(player, target)));
    list_channels(player, target);
  } else {
    notify(player, tprintf("+channel: %s isn't currently on any channels.",
                          unparse_object(player, target)));
    notify(player, "+channel: For a general chatting channel, turn to channel 'public'");
  }
}

/**
 * Display formatted list of channels
 */
void list_channels(dbref player, dbref target)
{
  char *clist, *clbegin, *alias, *al, *status, *s = NULL;
  dbref channum;

  clbegin = clist = tprintf("%s ", atr_get(target, A_CHANNEL));

  notify(player, "Channel:             Alias:     Status: Owner:");

  while (*clist) {
    char *space = strchr(clist, ' ');
    if (space) {
      *space = '\0';
    }

    alias = strchr(clist, ':');
    if (alias && *alias) {
      *alias++ = '\0';
      s = strchr(alias, ':');
      if (s && *s) {
        *s++ = '\0';
        status = (!strncmp(s, "0", 1)) ? "OFF" : "ON ";
      } else {
        status = "ON ";
      }
      al = alias;
    } else {
      al = "UNDEFINED";
      status = "ON ";
    }

    channum = lookup_channel(clist);
    if (channum == NOTHING) {
      notify(player, tprintf("%-30.30s  Invalid Channel.", clist));
    } else {
      char channame[MESSAGE_BUF_SIZE];
      char filler[26] = "                         ";

      strncpy(channame, truncate_color(unparse_object(player, channum), 20),
              sizeof(channame) - 1);
      channame[sizeof(channame) - 1] = '\0';

      int name_len = strlen(strip_color(channame));
      if (name_len < 20) {
        filler[20 - name_len] = '\0';
      } else {
        filler[0] = '\0';
      }

      notify(player, tprintf("%s%s %-10.10s %s     %s",
                            channame, filler, al, status,
                            unparse_object(player, db[channum].owner)));
    }

    if (alias && *alias) {
      clist += strlen(clist) + strlen(alias) + 2;
      if (s && *s) {
        clist += strlen(s) + 1;
      }
    } else {
      clist += strlen(clist) + 1;
    }
  }
}

/**
 * Search for channels
 * Syntax: +channel search <own|op|all|<n>>
 */
void do_channel_search(dbref player, char *arg2)
{
  struct pl_elt *e;
  int level;
  char onoff[4];
  int i;

  if (!(arg2 && *arg2)) {
    notify(player, "+channel: Bad search syntax.");
    return;
  }

  level = 0;

  if (!strncmp(arg2, "own", 3)) {
    level = 1;
  } else if (!strncmp(arg2, "op", 2)) {
    level = 2;
  } else if (!strncmp(arg2, "all", 3)) {
    level = 3;
  }

  notify(player, "+channel search results:");

  for (i = 0; i < CHANNEL_LIST_SIZE; i++) {
    if (!pl_used) {
      continue;
    }

    for (e = channel_list[i]; e; e = e->next) {
      char owner[MESSAGE_BUF_SIZE];
      char chan[MESSAGE_BUF_SIZE];
      char filler[26] = "                         ";

      strncpy(owner, truncate_color(unparse_object(player, db[e->channel].owner), 20),
              sizeof(owner) - 1);
      owner[sizeof(owner) - 1] = '\0';

      int owner_len = strlen(strip_color(owner));
      if (owner_len < 20) {
        filler[20 - owner_len] = '\0';
      } else {
        filler[0] = '\0';
      }
      strncat(owner, filler, sizeof(owner) - strlen(owner) - 1);

      strcpy(filler, "                        ");
      strncpy(chan, truncate_color(unparse_object(player, e->channel), 20),
              sizeof(chan) - 1);
      chan[sizeof(chan) - 1] = '\0';

      int chan_len = strlen(strip_color(chan));
      if (chan_len < 20) {
        filler[20 - chan_len] = '\0';
      } else {
        filler[0] = '\0';
      }
      strncat(chan, filler, sizeof(chan) - strlen(chan) - 1);

      /* Determine on/off status */
      if (is_on_channel_only(player, db[e->channel].name) != NOTHING) {
        if (channel_onoff_chk(player, e->channel)) {
          strcpy(onoff, "ON ");
        } else {
          strcpy(onoff, "OFF");
        }
      } else {
        strcpy(onoff, "   ");
      }

      /* Filter by search level */
      if (level == 0) {
        if (!string_compare(db[e->channel].name, arg2)) {
          notify(player, tprintf("  %s %s %s", onoff, chan, owner));
          break;
        }
      } else if (db[e->channel].owner == player) {
        notify(player, tprintf("* %s %s %s", onoff, chan, owner));
      } else if ((level > 1) && group_controls(player, e->channel)) {
        notify(player, tprintf("# %s %s %s", onoff, chan, owner));
      } else if (level == 3) {
        if (!((db[e->channel].flags & SEE_OK) &&
              could_doit(player, e->channel, A_LHIDE))) {
          if (controls(player, e->channel, POW_CHANNEL)) {
            strcpy(onoff, "HID");
            notify(player, tprintf("  %s %s %s", onoff, chan, owner));
          }
        } else {
          notify(player, tprintf("  %s %s %s", onoff, chan, owner));
        }
      }
    }
  }
}

/**
 * Channel log command (placeholder)
 */
void do_channel_log(dbref player, char *arg2)
{
  notify(player, "+channel: Log functionality not yet implemented.");
}

/**
 * Set channel color
 * Syntax: +channel color <channel>:<colored_name>
 */
void do_channel_color(dbref player, char *arg2)
{
  char *channel;
  char *color;
  char *alias;
  int pos;
  int onoff;
  char *old1, *old2, *new;

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

  alias = find_channel_alias(player, channel);
  if (!(alias && *alias)) {
    alias = channel;
  }

  if (!(color && *color)) {
    color = channel;
  }

  onoff = channel_onoff_chk(player, lookup_channel(channel));

  pos = is_on_channel_only(player, channel);
  if (pos == -1) {
    notify(player, "+channel: You must first join the channel before setting its color.");
    return;
  }

  if (strcmp(channel, strip_color_nobeep(color)) != 0) {
    notify(player, "+channel: Colored name does not match channel name.");
    return;
  }

  /* Build new channel list */
  old1 = tprintf("%s", atr_get(player, A_CHANNEL));
  old2 = old1 + pos;

  if (old2 && *old2) {
    *old2++ = '\0';
    old2 = strchr(old2, ' ');
    if (old2 && *old2) {
      old2++;
    }

    if (old2 && *old2) {
      new = tprintf("%s%s:%s:%d %s", old1, color, alias, onoff, old2);
    } else {
      new = tprintf("%s%s:%s:%d", old1, color, alias, onoff);
    }
  } else {
    new = tprintf("%s%s:%s:%d", old1, color, alias, onoff);
  }

  atr_add(player, A_CHANNEL, new);
  notify(player, tprintf("+channel: %s is now colored as %s", channel, color));
}

/**
 * Find the colored name for a channel
 * @return Colored channel name or default name
 */
char *find_channel_color(dbref player, const char *channel)
{
  char *color;
  dbref channum;

  if (!channel || !*channel) {
    return tprintf("%s", channel ? channel : "");
  }

  color = find_channel_only(player, channel);

  if (!color || !strcmp(channel, color)) {
    channum = lookup_channel(channel);
    if (channum != NOTHING) {
      color = tprintf("%s", db[channum].cname);
    } else {
      color = tprintf("%s", channel);
    }
  }

  return color;
}

/* ===================================================================
 * Channel Membership Helper Functions
 * =================================================================== */

/**
 * Remove player from channel
 * @return Position removed from or NOTHING if not on channel
 */
int remove_from_channel(dbref victim, const char *arg2)
{
  int i, j;
  dbref channum;

  if (!arg2 || !*arg2) {
    return NOTHING;
  }

  i = is_on_channel_only(victim, arg2);
  if (i == NOTHING) {
    return NOTHING;
  }

  channum = lookup_channel(arg2);

  /* Remove from player's channel list */
  atr_add(victim, A_CHANNEL, remove_from_ch_attr(victim, i));

  /* Remove from channel's player list */
  if (channum != NOTHING) {
    j = is_on_channel_only(channum, tprintf("%ld", victim));
    if (j >= 0) {
      atr_add(channum, A_CHANNEL, remove_from_ch_attr(channum, j));
    }
  }

  return i;
}

/**
 * Check if string exists in attribute
 * @return Position of string or NOTHING
 */
int is_in_attr(dbref player, const char *str, ATTR *attr)
{
  char *cur;
  char *begin;
  char *next;

  if (!str || !*str || !attr) {
    return NOTHING;
  }

  begin = cur = tprintf("%s ", atr_get(player, attr));

  while (cur && *cur) {
    next = strchr(cur, ' ');
    if (next && *next) {
      *next++ = '\0';
      if (!strcmp(cur, str)) {
        return (cur - begin);
      }
    }
    cur = next;
  }

  return NOTHING;
}

/**
 * Remove entry from attribute at position
 * @return New attribute value
 */
char *remove_from_attr(dbref player, int i, ATTR *attr)
{
  char *end;
  char buf[MESSAGE_BUF_SIZE];
  char buf2[MESSAGE_BUF_SIZE];

  if (!attr || i < 0) {
    return tprintf("");
  }

  strncpy(buf, atr_get(player, attr), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  end = strchr(buf + i, ' ');
  if (!end) {
    end = strchr(buf + i, '\0');
  } else {
    end++;
  }

  strncpy(buf2, atr_get(player, attr), sizeof(buf2) - 1);
  buf2[sizeof(buf2) - 1] = '\0';

  strcpy(buf2 + i, end);

  /* Trim trailing space */
  end = strchr(buf2, '\0');
  if (end > buf2 && *--end == ' ') {
    *end = '\0';
  }

  return tprintf("%s", buf2);
}

/**
 * Remove entry from channel list attribute
 */
char *remove_from_ch_attr(dbref player, int i)
{
  return remove_from_attr(player, i, A_CHANNEL);
}

/* ===================================================================
 * Channel On/Off Management
 * =================================================================== */

/**
 * Check if channel is turned on for player
 * @return 1 if on, 0 if off
 */
int channel_onoff_chk(dbref player, dbref channum)
{
  char *next, *curr, *buf, *q, *alias;
  int onoff;

  if (channum == NOTHING) {
    return 0;
  }

  next = curr = buf = tprintf("%s ", atr_get(player, A_CHANNEL));

  next = strchr(curr, ' ');
  if (next) {
    *next++ = '\0';
  }

  alias = strchr(curr, ':');
  if (alias) {
    *alias++ = '\0';
    q = strchr(alias, ':');
    if (q && *q) {
      *q++ = '\0';
      onoff = atoi(q);
    } else {
      do_channel_alias(player, tprintf("%s:%s", curr, alias));
      onoff = 1;
    }
  } else {
    do_channel_alias(player, tprintf("%s:%s", curr, curr));
    onoff = 1;
  }

  return onoff;
}

/**
 * Set channel on/off state
 * Syntax: <channel> on|off
 */
void channel_onoff_set(dbref player, const char *arg1, const char *arg2)
{
  char *alias;
  dbref channel;
  int pos;
  int onoff;
  int change;
  char *old1, *old2, *new;

  if (!arg1 || !*arg1 || !arg2 || !*arg2) {
    notify(player, "+channel: Bad on/off syntax.");
    return;
  }

  channel = lookup_channel(arg1);
  if (channel == NOTHING) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  alias = find_channel_alias(player, db[channel].name);
  if (!(alias && *alias)) {
    notify(player, "+channel: Sorry, you must first leave and rejoin the channel.");
    return;
  }

  onoff = channel_onoff_chk(player, channel);

  pos = is_on_channel_only(player, db[channel].name);
  if (pos == NOTHING) {
    notify(player, "+channel: You must first join the channel before changing its status.");
    return;
  }

  change = (!strcmp("on", arg2)) ? 1 : 0;

  if (change == onoff) {
    notify(player, tprintf("+channel: Channel %s is already %s!",
                          db[channel].name, arg2));
    return;
  }

  /* Announce state change */
  if (change == 0) {
    com_send_as_hidden(db[channel].name,
                      tprintf("|Y!+*| %s |G!+has turned this channel OFF.|",
                             db[player].cname),
                      player);
  }

  /* Build new channel list */
  old1 = tprintf("%s", atr_get(player, A_CHANNEL));
  old2 = old1 + pos;

  if (old2 && *old2) {
    *old2++ = '\0';
    old2 = strchr(old2, ' ');
    if (old2 && *old2) {
      old2++;
      new = tprintf("%s%s:%s:%d %s", old1, db[channel].name, alias, change, old2);
    } else {
      new = tprintf("%s%s:%s:%d", old1, db[channel].name, alias, change);
    }
  } else {
    new = tprintf("%s%s:%s:%d", old1, db[channel].name, alias, change);
  }

  atr_add(player, A_CHANNEL, new);

  if (change == 1) {
    com_send_as_hidden(db[channel].name,
                      tprintf("|Y!+*| %s |G!+has turned this channel ON.|",
                             db[player].cname),
                      player);
  }
}

/* ===================================================================
 * Channel Hash Table Functions
 * =================================================================== */

/**
 * Initialize channel hash function table
 */
static void init_chan_hft(void)
{
  int i;

  for (i = 0; i < 256; i++) {
    hash_chan_fun_table[i] = random() & (CHANNEL_LIST_SIZE - 1);
  }
  hft_chan_initialized = 1;
}

/**
 * Hash function for channel names
 * @return Hash value
 */
static dbref hash_chan_function(const char *string)
{
  dbref hash;

  if (!string || !*string) {
    return 0;
  }

  if (!hft_chan_initialized) {
    init_chan_hft();
  }

  hash = 0;
  for (; *string; string++) {
    hash ^= ((hash >> 1) ^ hash_chan_fun_table[(int)DOWNCASE(*string)]);
  }

  return hash;
}

/**
 * Clear all channels from hash table
 */
void clear_channels(void)
{
  int i;
  struct pl_elt *e, *next;

  for (i = 0; i < CHANNEL_LIST_SIZE; i++) {
    if (pl_used) {
      for (e = channel_list[i]; e; e = next) {
        next = e->next;
        SAFE_FREE(e);
      }
    }
    channel_list[i] = NULL;
  }
  pl_used = 1;
}

/**
 * Add channel to hash table
 */
void add_channel(dbref channel)
{
  dbref hash;
  struct pl_elt *e;

  if (!GoodObject(channel)) {
    return;
  }

  if (strchr(db[channel].name, ' ')) {
    log_error(tprintf("Channel (%s) with a space in its name? Inconceivable!",
                     db[channel].name));
    return;
  }

  hash = hash_chan_function(db[channel].name);

//  e = (struct pl_elt *)malloc(sizeof(struct pl_elt));
//  SAFE_MALLOC(result, type, number)
  SAFE_MALLOC(e, struct pl_elt*, 1);
  if (!e) {
    log_error("Out of memory in add_channel!");
    return;
  }

  e->channel = channel;
  e->prev = NULL;
  e->next = channel_list[hash];

  if (channel_list[hash]) {
    channel_list[hash]->prev = e;
  }

  channel_list[hash] = e;
}

/**
 * Look up channel by name
 * @return Channel dbref or NOTHING
 */
dbref lookup_channel(const char *name)
{
  struct pl_elt *e;
  dbref a;

  if (!name || !*name) {
    return NOTHING;
  }

  /* Try hash table lookup */
  for (e = channel_list[hash_chan_function(name)]; e; e = e->next) {
    if (!string_compare(db[e->channel].name, name)) {
      return e->channel;
    }
  }

  /* Try dbref lookup */
  if (name[0] == '#' && name[1]) {
    a = atol(name + 1);
    if (a >= 0 && a < db_top) {
      return a;
    }
  }

  return NOTHING;
}

/**
 * Delete channel from hash table
 */
void delete_channel(dbref channel)
{
  dbref hash;
  struct pl_elt *prev, *e;

  if (!GoodObject(channel)) {
    return;
  }

  if (strchr(db[channel].name, ' ')) {
    log_error(tprintf("Channel (%s) with a space in its name? Inconceivable!",
                     db[channel].name));
    return;
  }

  hash = hash_chan_function(db[channel].name);

  e = channel_list[hash];
  if (!e) {
    return;
  }

  if (e->channel == channel) {
    /* It's the first one */
    channel_list[hash] = e->next;
    if (e->next) {
      e->next->prev = NULL;
    }
    SAFE_FREE(e);
    return;
  }

  /* Search for it */
  for (prev = e, e = e->next; e; prev = e, e = e->next) {
    if (e->channel == channel) {
      prev->next = e->next;
      if (e->next) {
        e->next->prev = prev;
      }
      SAFE_FREE(e);
      return;
    }
  }
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

  channel = find_channel(player, chan);

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
  if (lookup_channel(channel) == NOTHING) {
    notify(player, "+channel: Invalid channel.");
    return;
  }

  /* Send message */
  com_send_int(channel, message, player, 0);
}

/* ===================================================================
 * End of com.c
 * =================================================================== */
