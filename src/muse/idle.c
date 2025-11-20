/* idle.c - Player Idle and Away Status Management
 *
 * CONSOLIDATED (2025 reorganization): comm/set.c → muse/idle.c
 * Idle management belongs in muse/ as it's game state management.
 *
 * Functions:
 * - do_idle() - @idle command
 * - do_away() - @away command
 * - set_idle_command() - Set idle command
 * - set_idle() - Internal idle status setter
 * - set_unidle() - Internal unidle status handler
 */

/* $Id: set.c,v 1.23 1994/01/26 22:28:27 nils Exp $ */

#include <stdio.h>
#include <ctype.h>
#include <values.h>
#include <string.h>

#include "db.h"
#include "config.h"
#include "match.h"
#include "interface.h"
#include "externs.h"
#include "credits.h"

extern time_t muse_up_time;

void do_idle(dbref player, char *idle)
{
  if (!idle) {
    notify(player, "Idle what?");
    return;
  }

  if (*idle == '?')
  {
    char *current_idle = Idle(player);
    if (current_idle && *current_idle)
    {
      notify(player, tprintf("Your Idle message is: %s",
			     atr_get(player, A_IDLE)));
      return;
    }
    else
    {
      notify(player, "You have no Idle message.");
      return;
    }
  }

  if (*idle == '\0')
  {
    atr_clr(player, A_IDLE);
    notify(player, "Idle message removed.");
    return;
  }

  atr_add(player, A_IDLE, idle);
  notify(player, tprintf("Idle message set as: %s", idle));

}

void do_away(dbref player, char *away)
{
  if (!away) {
    notify(player, "Away what?");
    return;
  }

  if (*away == '?')
  {
    char *current_away = Away(player);
    if (current_away && *current_away)
    {
      notify(player, tprintf("Your Away message is: %s",
			     atr_get(player, A_AWAY)));
      return;
    }
    else
    {
      notify(player, "You have no Away message.");
      return;
    }
  }

  if (*away == '\0')
  {
    atr_clr(player, A_AWAY);
    notify(player, "Away message removed.");
    return;
  }
  atr_add(player, A_AWAY, away);
  notify(player, tprintf("Away message set as: %s", away));
}


void set_idle_command(dbref player, char *arg1, char *arg2)
{
  dbref target;
  time_t time = -1;

  if (arg2 && *arg2)
  {
    target = lookup_player(arg1);
    if (target == NOTHING)
    {
      set_idle(player, player, time, tprintf("%s = %s", arg1, arg2));
    }
    else
    {
      set_idle(target, player, time, arg2);
    }
  }
  else
  {
    set_idle(player, player, time, arg1);
  }
}

void set_idle(dbref player, dbref cause, time_t time, char *msg)
{
  char buf[8192];  /* Fix #1: Increased size and using snprintf */
  char *buf2;
  size_t buf_remaining;
  int result;

  /* Fix #6: Add null checks */
  if (player < 0 || player >= db_top || !db[player].name) {
    return;
  }

  if (is_pasting(player))
  {
    add_more_paste(player, "@pasteabort");
  }

  /* Fix #1: Use snprintf for safe string building */
  result = snprintf(buf, sizeof(buf), "%s idled ", db[player].name);
  if (result < 0 || result >= (int)sizeof(buf)) {
    notify(player, "Error setting idle status.");
    return;
  }
  buf_remaining = sizeof(buf) - result;

  if (cause == -1)
  {
    char *temp = tprintf("after %ld minutes inactivity", time);
    result = snprintf(buf + strlen(buf), buf_remaining, "%s", temp);
    if (result < 0 || result >= (int)buf_remaining) {
      notify(player, "Error setting idle status.");
      return;
    }

    if (strlen(atr_get(player, A_IDLE)) > 0)
      atr_add(player, A_IDLE_CUR, atr_get(player, A_IDLE));
    else
      atr_add(player, A_IDLE_CUR, "inactivity idle - no default idle message.");
  }
  else if (cause != player && (!controls(cause, player, POW_MODIFY) && !power(cause, POW_MODIFY)))
  {
    notify(cause, perm_denied());
    return;
  }
  else if (cause == player)
  {
    result = snprintf(buf + strlen(buf), buf_remaining, "manually");
    if (result < 0) return;
  }
  else
  {
    if (atr_get(player, A_IDLE))
    {
      char *temp = tprintf("- set by %s", db[cause].name ? db[cause].name : "someone");
      result = snprintf(buf + strlen(buf), buf_remaining, "%s", temp);
      if (result < 0) return;
    }
  }

  if (msg && *msg)
  {
    /* Truncate message if too long */
    char truncated_msg[513];
    if (strlen(msg) > 512)
    {
      strncpy(truncated_msg, msg, 512);
      truncated_msg[512] = '\0';
      msg = truncated_msg;
      notify(player, "Idle message truncated.");
    }

    char *temp = tprintf(" (%s)", msg);
    buf_remaining = sizeof(buf) - strlen(buf);
    result = snprintf(buf + strlen(buf), buf_remaining, "%s", temp);
    if (result < 0 || result >= (int)buf_remaining) {
      /* Message too long, truncate */
      buf[sizeof(buf) - 1] = '\0';
    }
    atr_add(player, A_IDLE_CUR, msg);
  }
  else
  {
    if (strlen(atr_get(player, A_IDLE)) > 0)
      atr_add(player, A_IDLE_CUR, atr_get(player, A_IDLE));
    else
      atr_add(player, A_IDLE_CUR, "");
  }

  if ((strlen(atr_get(player, A_BLACKLIST))) || (strlen(atr_get(player, A_LHIDE))))
  {
    buf2 = tprintf("|R+(||R!+HIDDEN||R+)| %s", buf);
  }
  else
  {
    buf2 = tprintf("%s", buf);
  }

  log_io(buf2);
  com_send_as_hidden("pub_io", buf2, player);
  db[player].flags |= PLAYER_IDLE;
  did_it(player, player, NULL, 0, NULL, 0, A_AIDLE);
  return;
}


void set_unidle(dbref player, time_t lasttime)
{
  long unidle_time;
  static int in_unidle = 0;  /* Fix #5: Recursion prevention flag */

  /* Fix #5: Prevent recursion */
  if (in_unidle) {
    return;
  }

  check_newday();

  /* Fix #6: Add bounds checking */
  if (player <= 0 || player >= db_top)
  {
    log_io(tprintf("problem with set_unidle -- player = %d lasttime = %ld", (int)player, lasttime));
    return;
  }

  /* if lasttime is MAXINT, we suppress the message. */
  if (lasttime != MAXINT)
  {
    char *buf, *buf2;
    unidle_time = now - lasttime;
    db[player].flags &= ~PLAYER_IDLE;

    if (unidle_time)
      buf = tprintf("%s unidled after %s.", unparse_object(player, player), time_format_4(unidle_time));
    else
      buf = tprintf("%s unidled immediately. duh.", unparse_object(player, player));

    if ((strlen(atr_get(player, A_BLACKLIST))) || (strlen(atr_get(player, A_LHIDE))))
    {
      buf2 = tprintf("|R+(||R!+HIDDEN||R+)| %s", buf);
    }
    else
    {
      buf2 = buf;
    }

    log_io(buf2);
    com_send_as_hidden("pub_io", buf2, player);
  }

  /* Fix #5: Set flag before executing A_AUNIDLE to prevent recursion */
  in_unidle = 1;
  did_it_now(player, player, NULL, 0, NULL, 0, A_AUNIDLE);
  in_unidle = 0;

  if (lasttime != MAXINT)
  {
    if ((check_mail_internal(player, "")) > 0)
      check_mail(player, "");
  }
  return;
}
