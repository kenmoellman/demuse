/* ctrl.c */
/* $Id: ctrl.c,v 1.7 1994/02/18 22:45:48 nils Exp $ */

/* ============================================================================
 * MODERNIZATION NOTES (2025)
 * ============================================================================
 * This file has been modernized with the following improvements:
 *
 * SAFETY IMPROVEMENTS:
 * - Added GoodObject() validation throughout
 * - Replaced sprintf() with snprintf() for buffer safety
 * - Replaced strcpy() with strncpy() and manual null-termination
 * - Replaced strcat() with strncat() for bounded string operations
 * - Added bounds checking to prevent buffer overruns
 * - Safe handling of invalid object references
 *
 * CODE QUALITY:
 * - Full ANSI C compliance (no more K&R style)
 * - Better organization with section markers
 * - Comprehensive inline documentation
 * - Improved error handling
 *
 * ANSI C COMPLIANCE:
 * - All function declarations use ANSI C prototypes
 * - Proper parameter types
 *
 * SECURITY NOTES:
 * - All database access validated with GoodObject()
 * - Buffer operations are bounded
 * - Invalid references handled gracefully
 */

/* ============================================================================
 * INCLUDES
 * ============================================================================ */

#include "config.h"
#include "externs.h"

/* ============================================================================
 * CONTROL FLOW COMMANDS
 * ============================================================================ */

/**
 * @switch - Execute commands based on wildcard pattern matching
 *
 * This command compares an expression against a series of pattern/action
 * pairs and executes the first matching action. If no patterns match,
 * executes a default action if provided.
 *
 * Syntax: @switch <expression>=<pattern1>,<action1>,<pattern2>,<action2>,...[,<default>]
 *
 * BUFFER SAFETY: Uses bounded string operations
 * VALIDATION: Checks for null pointers
 *
 * @param player Player executing the command
 * @param exp Expression to match against patterns
 * @param argv Array of pattern/action pairs
 * @param cause Object that caused this action
 */
void do_switch(dbref player, char *exp, char *argv[], dbref cause)
{
  int any = 0, a, c;
  char buff[1024];
  char *ptrsrv[10];

  if (!GoodObject(player)) {
    log_error("do_switch: Invalid player reference");
    return;
  }

  /* Safe string copy with bounds checking */
  strncpy(buff, exp, sizeof(buff) - 1);
  buff[sizeof(buff) - 1] = '\0';

  if (!argv[1]) {
    return;
  }

  /* Save current wildcard pointers */
  for (c = 0; c < 10; c++) {
    ptrsrv[c] = wptr[c];
  }

  /* Try wildcard match of buff with patterns in argv
   * argv is organized as: pattern1, action1, pattern2, action2, ...
   * Process pairs until we find a match or run out */
  for (a = 1; (a < (MAX_ARG - 1)) && argv[a] && argv[a + 1]; a += 2) {
    if (wild_match(argv[a], buff)) {
      any = 1;

      /* Restore wildcard pointers for this action */
      for (c = 0; c < 10; c++) {
        wptr[c] = ptrsrv[c];
      }

      /* Execute the matching action */
      parse_que(player, argv[a + 1], cause);
    }
  }

  /* Restore wildcard pointers */
  for (c = 0; c < 10; c++) {
    wptr[c] = ptrsrv[c];
  }

  /* Execute default action if no pattern matched */
  if ((a < MAX_ARG) && !any && argv[a]) {
    parse_que(player, argv[a], cause);
  }
}

/**
 * @foreach - Execute command for each item in a space-separated list
 *
 * This command iterates over a list of items and executes a command
 * for each one, with %0 set to the current item.
 *
 * Syntax: @foreach <list>=<command>
 *
 * BUFFER SAFETY: Uses parse_up() which handles boundaries
 * VALIDATION: Checks for null pointers
 *
 * @param player Player executing the command
 * @param list Space-separated list of items
 * @param command Command to execute for each item
 * @param cause Object that caused this action
 */
void do_foreach(dbref player, char *list, char *command, dbref cause)
{
  char *k;
  char *ptrsrv[10];
  int c;

  if (!GoodObject(player)) {
    log_error("do_foreach: Invalid player reference");
    return;
  }

  if (!list || !command) {
    return;
  }

  /* Save current wildcard pointers */
  for (c = 0; c < 10; c++) {
    ptrsrv[c] = wptr[c];
  }

  /* Clear wildcard pointers for iteration */
  for (c = 0; c < 10; c++) {
    wptr[c] = "";
  }

  /* Iterate over space-separated list */
  while ((k = parse_up(&list, ' '))) {
    wptr[0] = k;  /* Current item in %0 */
    parse_que(player, command, cause);
  }

  /* Restore wildcard pointers */
  for (c = 0; c < 10; c++) {
    wptr[c] = ptrsrv[c];
  }
}

/* ============================================================================
 * ATTRIBUTE TRIGGER COMMANDS
 * ============================================================================ */

/**
 * @trigger - Execute an attribute on an object
 *
 * This command triggers (executes) an attribute on an object as if
 * it had been activated normally. Arguments can be passed via %0-%9.
 *
 * Syntax: @trigger <object>/<attribute>[=<arg0>,...,<arg9>]
 *
 * SECURITY: Requires control over the object
 * VALIDATION: Checks object validity and permissions
 *
 * @param player Player executing the command
 * @param object Object/attribute specification
 * @param argv Arguments to pass to triggered attribute
 */
void do_trigger(dbref player, char *object, char *argv[])
{
  dbref thing;
  ATTR *attrib;
  int a;

  if (!GoodObject(player)) {
    log_error("do_trigger: Invalid player reference");
    return;
  }

  if (!object || !*object) {
    notify(player, "Trigger what?");
    return;
  }

  /* Parse object/attribute specification */
  if (!parse_attrib(player, object, &thing, &attrib, POW_SEEATR)) {
    notify(player, "No match.");
    return;
  }

  /* Validate the target object */
  if (!GoodObject(thing)) {
    notify(player, "Invalid object.");
    return;
  }

  /* Permission check */
  if (!controls(player, thing, POW_MODIFY)) {
    notify(player, perm_denied());
    return;
  }

  /* Prevent triggering on root object */
  if (thing == root) {
    notify(player, "You can't trigger root.");
    return;
  }

  /* Set up wildcard arguments %0-%9 */
  for (a = 0; a < 10; a++) {
    wptr[a] = argv[a + 1];
  }

  /* Execute the attribute */
  did_it(player, thing, NULL, 0, NULL, 0, attrib);

  /* Feedback (unless QUIET) */
  if (!(db[player].flags & QUIET)) {
    notify(player, tprintf("%s - Triggered.", db[thing].cname));
  }
}

/**
 * @trigger_as - Execute an attribute as if triggered by another object
 *
 * This command is similar to @trigger but allows specifying which
 * object should be the "cause" of the trigger. This affects %# and
 * permission checks within the triggered attribute.
 *
 * Syntax: @tr_as <object>/<attribute>=<cause>[,<arg0>,...,<arg8>]
 *
 * SECURITY: Requires control over the target object
 * VALIDATION: Checks both target and cause validity
 *
 * @param player Player executing the command
 * @param object Object/attribute specification
 * @param argv First element is cause, rest are arguments
 */
void do_trigger_as(dbref player, char *object, char *argv[])
{
  dbref thing;
  dbref cause;
  ATTR *attrib;
  int a;

  if (!GoodObject(player)) {
    log_error("do_trigger_as: Invalid player reference");
    return;
  }

  if (!object || !*object) {
    notify(player, "Trigger what?");
    return;
  }

  /* First argument is the cause object */
  if (!argv[1] || !*argv[1]) {
    notify(player, "You must specify a cause object.");
    return;
  }

  cause = match_thing(player, argv[1]);
  if (!GoodObject(cause)) {
    notify(player, "Invalid cause object.");
    return;
  }

  /* Parse object/attribute specification */
  if (!parse_attrib(player, object, &thing, &attrib, POW_SEEATR)) {
    notify(player, "No match.");
    return;
  }

  /* Validate the target object */
  if (!GoodObject(thing)) {
    notify(player, "Invalid object.");
    return;
  }

  /* Permission check */
  if (!controls(player, thing, POW_MODIFY)) {
    notify(player, perm_denied());
    return;
  }

  /* Prevent triggering on root object */
  if (thing == root) {
    notify(player, "You can't trigger root.");
    return;
  }

  /* Set up wildcard arguments %0-%8 (argv[2] onwards) */
  for (a = 0; a < 9; a++) {
    wptr[a] = argv[a + 2];
  }

  /* Execute the attribute with specified cause */
  did_it(cause, thing, NULL, 0, NULL, 0, attrib);

  /* Feedback (unless QUIET) */
  if (!(db[player].flags & QUIET)) {
    notify(player, tprintf("%s - Triggered.", db[thing].cname));
  }
}

/* ============================================================================
 * DECOMPILE COMMAND
 * ============================================================================ */

/**
 * @decompile - Generate commands to recreate an object's configuration
 *
 * This command produces a series of @set, @defattr, @addparent, and @nset
 * commands that would recreate the flags, attribute definitions, parents,
 * and attribute values of an object.
 *
 * Syntax: @decompile <object>[=<prefix>]
 *
 * SECURITY: Requires control or SEE_OK flag
 * BUFFER SAFETY: Uses bounded string operations
 * VALIDATION: Checks object validity
 *
 * @param player Player executing the command
 * @param arg1 Object to decompile
 * @param arg2 Optional prefix for generated commands
 */
void do_decompile(dbref player, char *arg1, char *arg2)
{
  dbref obj;
  int i;
  ALIST *a;
  ATRDEF *k;
  char buf[1024];
  char *s;
  const char *prefix;

  if (!GoodObject(player)) {
    log_error("do_decompile: Invalid player reference");
    return;
  }

  if (!arg1 || !*arg1) {
    notify(player, "Decompile what?");
    return;
  }

  /* Match the object */
  obj = match_thing(player, arg1);
  if (!GoodObject(obj)) {
    notify(player, "No match.");
    return;
  }

  /* Permission check */
  if ((!controls(player, obj, POW_SEEATR) || !controls(player, obj, POW_EXAMINE)) &&
      !(db[obj].flags & SEE_OK)) {
    notify(player, perm_denied());
    return;
  }

  /* Determine prefix to use */
  prefix = (arg2 && *arg2) ? arg2 : arg1;

  /* Decompile flags */
  s = flag_description(obj);
  if (s && (s = strchr(s, ':')) && (s = strchr(++s, ':'))) {
    char *g;

    s += 2;
    while ((g = parse_up(&s, ' '))) {
      notify(player, tprintf("@set %s=%s", prefix, g));
    }
  }

  /* Decompile attribute definitions */
  if (db[obj].atrdefs) {
    for (k = db[obj].atrdefs; k; k = k->next) {
      size_t len;

      /* Build @defattr command with bounds checking */
      len = snprintf(buf, sizeof(buf), "@defattr %s/%s%s",
                     prefix,
                     k->a.name,
                     (k->a.flags) ? "=" : "");

      /* Add attribute flags */
      if (len < sizeof(buf) - 1) {
        if (k->a.flags & AF_WIZARD) {
          strncat(buf, " wizard", sizeof(buf) - strlen(buf) - 1);
        }
        if (k->a.flags & AF_UNIMP) {
          strncat(buf, " unsaved", sizeof(buf) - strlen(buf) - 1);
        }
        if (k->a.flags & AF_OSEE) {
          strncat(buf, " osee", sizeof(buf) - strlen(buf) - 1);
        }
        if (k->a.flags & AF_INHERIT) {
          strncat(buf, " inherit", sizeof(buf) - strlen(buf) - 1);
        }
        if (k->a.flags & AF_DARK) {
          strncat(buf, " dark", sizeof(buf) - strlen(buf) - 1);
        }
        if (k->a.flags & AF_DATE) {
          strncat(buf, " date", sizeof(buf) - strlen(buf) - 1);
        }
        if (k->a.flags & AF_LOCK) {
          strncat(buf, " lock", sizeof(buf) - strlen(buf) - 1);
        }
        if (k->a.flags & AF_FUNC) {
          strncat(buf, " function", sizeof(buf) - strlen(buf) - 1);
        }
      }

      notify(player, buf);
    }
  }

  /* Decompile parents */
  if (db[obj].parents) {
    for (i = 0; db[obj].parents[i] != NOTHING; i++) {
      /* Validate parent reference */
      if (GoodObject(db[obj].parents[i])) {
        notify(player, tprintf("@addparent %s=%" DBREF_FMT,
                              prefix, db[obj].parents[i]));
      }
    }
  }

  /* Decompile attributes */
  for (a = db[obj].list; a; a = AL_NEXT(a)) {
    if (AL_TYPE(a)) {
      if (!(AL_TYPE(a)->flags & AF_UNIMP)) {
        if (can_see_atr(player, obj, AL_TYPE(a))) {
          char attrbuf[256];

          /* Get attribute name safely */
          snprintf(attrbuf, sizeof(attrbuf), "%s", unparse_attr(AL_TYPE(a), 0));

          /* Output @nset command */
          notify(player, tprintf("@nset %s=%s:%s",
                                prefix,
                                attrbuf,
                                AL_STR(a) ? AL_STR(a) : ""));
        }
      }
    }
  }
}

/* ============================================================================
 * CYCLE COMMAND
 * ============================================================================ */

/**
 * @cycle - Rotate an attribute value through a list of possibilities
 *
 * This command changes an attribute to the next value in a specified list.
 * If the current value matches one in the list, it advances to the next.
 * If it reaches the end or doesn't match, it defaults to the first value.
 *
 * Syntax: @cycle <object>/<attribute>=<value1>,<value2>,...,<valueN>
 *
 * SECURITY: Requires permission to set the attribute
 * VALIDATION: Checks object and attribute validity
 *
 * @param player Player executing the command
 * @param arg1 Object/attribute specification
 * @param argv List of values to cycle through
 */
void do_cycle(dbref player, char *arg1, char **argv)
{
  int i;
  dbref thing;
  ATTR *attrib;
  char *curv;

  if (!GoodObject(player)) {
    log_error("do_cycle: Invalid player reference");
    return;
  }

  if (!arg1 || !*arg1) {
    notify(player, "Cycle what?");
    return;
  }

  /* Parse object/attribute specification */
  if (!parse_attrib(player, arg1, &thing, &attrib, POW_SEEATR)) {
    notify(player, "No match.");
    return;
  }

  /* Validate the target object */
  if (!GoodObject(thing)) {
    notify(player, "Invalid object.");
    return;
  }

  /* Check that we have values to cycle through */
  if (!argv[1]) {
    notify(player, "You must specify an attribute.");
    return;
  }

  /* Permission check */
  if (!can_set_atr(player, thing, attrib) || attrib == A_ALIAS) {
    notify(player, perm_denied());
    return;
  }

  /* Get current attribute value */
  curv = atr_get(thing, attrib);

  /* Find current value in the list and advance to next */
  for (i = 1; i < 10 && argv[i]; i++) {
    if (!string_compare(curv, argv[i])) {
      i++;  /* Move to next value */

      if (!(db[player].flags & QUIET)) {
        notify(player, "Cycling...");
      }

      /* Set to next value, or wrap to first if at end */
      if (i < 10 && argv[i]) {
        atr_add(thing, attrib, argv[i]);
      } else {
        atr_add(thing, attrib, argv[1]);
      }
      return;
    }
  }

  /* Current value not in list - default to first value */
  if (!(db[player].flags & QUIET)) {
    notify(player, "Defaulting to first in cycle.");
  }
  atr_add(thing, attrib, argv[1]);
}

/* End of ctrl.c */
