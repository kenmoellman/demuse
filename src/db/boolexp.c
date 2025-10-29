/* boolexp.c */
/* $Id: boolexp.c,v 1.6 1993/08/16 01:56:52 nils Exp $ */

#include <ctype.h>
#include <string.h>

#include "db.h"
#include "match.h"
#include "externs.h"
#include "config.h"
#include "interface.h"

/* ===================================================================
 * Macro Definitions
 * =================================================================== */

#define RIGHT_DELIMITER(x) ((x == AND_TOKEN) || (x == OR_TOKEN) || \
                           (x == ':') || (x == '.') || (x == ')') || \
                           (x == '=') || (!x))

#define LEFT_DELIMITER(x) ((x == NOT_TOKEN) || (x == '(') || \
                          (x == AT_TOKEN) || (x == IS_TOKEN) || \
                          (x == CARRY_TOKEN))

#define FUN_CALL 1

/* Lock type constants */
#define IS_TYPE 0
#define CARRY_TYPE 1
#define _TYPE 2

/* ===================================================================
 * Static Variables
 * =================================================================== */

static dbref parse_player;
static dbref parse_object;
static dbref parse_zone;
static int bool_depth;  /* recursion protection */

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static int eval_boolexp1(dbref object, char *key, dbref zone);
static int eval_boolexp_OR(char **buf);
static int eval_boolexp_AND(char **buf);
static int eval_boolexp_REF(char **buf);
static int get_word(char *d, char **s);
static dbref match_dbref(dbref player, char *name);
static int test_atr(char **buf, dbref player, int ind);
static int grab_num(char **buf);
static dbref get_dbref(char **buf);
static void eval_fun(char *buffer, char *str, dbref doer, dbref privs);

/* ===================================================================
 * String Parsing Functions
 * =================================================================== */

/**
 * Extract a word from the input stream, handling function calls
 * @param d Destination buffer
 * @param s Pointer to source string pointer (advanced by function)
 * @return Type of word (FUN_CALL or 0)
 */
static int get_word(char *d, char **s)
{
  int type = 0;

  while (!RIGHT_DELIMITER(**s))
  {
    if (**s == '[')
    {
      type = FUN_CALL;
      while (**s && (**s != ']'))
        *d++ = *(*s)++;
    }
    else
      *d++ = *(*s)++;
  }
  *d = '\0';
  return type;
}

/**
 * Match a database reference from a name
 * @param player Player performing the match
 * @param name Name to match
 * @return Database reference or NOTHING on failure
 */
static dbref match_dbref(dbref player, char *name)
{
  init_match(player, name, NOTYPE);
  match_everything();
  return noisy_match_result();
}

/* ===================================================================
 * Lock Processing Functions
 * =================================================================== */

/**
 * Process a lock string, converting object names to #dbrefs
 * @param player Player processing the lock
 * @param arg Lock string to process
 * @return Processed lock string (static buffer) or NULL on error
 */
char *process_lock(dbref player, char *arg)
{
  static char buffer[BUFFER_LEN];
  char *s = arg;
  char *t = buffer;
  int type;
  dbref thing;

  if (!arg || !*arg)
  {
    *buffer = '\0';
    return buffer;
  }

  while (*s && (t - buffer < BUFFER_LEN - 1))
  {
    while (LEFT_DELIMITER(*s) && (t - buffer < BUFFER_LEN - 1))
      *t++ = *s++;
    
    if (t - buffer >= BUFFER_LEN - 1)
      break;

    /* time to get a literal */
    type = get_word(t, &s);
    
    switch (*s)
    {
    case ':':  /* a built in attribute */
      if ((!builtin_atr_str(t)) && (type != FUN_CALL))
        notify(player, tprintf("Warning: no such built in attribute '%s'", t));
      t += strlen(t);
      if (t - buffer < BUFFER_LEN - 1)
        *t++ = *s++;
      if (t - buffer < BUFFER_LEN - 1)
      {
        type = get_word(t, &s);
        t += strlen(t);
      }
      break;
      
    case '.':  /* a user defined attribute */
      if ((thing = match_dbref(player, t)) < (dbref) 0)
        return NULL;
      snprintf(t, BUFFER_LEN - (t - buffer), "#%ld", thing);
      t += strlen(t);
      if (t - buffer < BUFFER_LEN - 1)
        *t++ = *s++;
      if (t - buffer < BUFFER_LEN - 1)
      {
        type = get_word(t, &s);
        if ((!atr_str(player, thing, t)) && (type != FUN_CALL))
          notify(player, tprintf("Warning: no such attribute '%s' on #%ld", t, thing));
        t += strlen(t);
      }
      if (*s != ':')
      {
        notify(player, "I don't understand that key.");
        return NULL;
      }
      if (t - buffer < BUFFER_LEN - 1)
        *t++ = *s++;
      if (t - buffer < BUFFER_LEN - 1)
      {
        type = get_word(t, &s);
        t += strlen(t);
      }
      break;
      
    default:
      if (type != FUN_CALL)
      {
        if ((thing = match_dbref(player, t)) == NOTHING)
          return NULL;
        snprintf(t, BUFFER_LEN - (t - buffer), "#%ld", thing);
      }
      t += strlen(t);
    }
    
    while (*s && RIGHT_DELIMITER(*s) && (t - buffer < BUFFER_LEN - 1))
      *t++ = *s++;
  }
  
  *t = '\0';
  return buffer;
}

/**
 * Convert processed lock back to human-readable format
 * @param player Player viewing the lock
 * @param arg Processed lock string
 * @return Human-readable lock string (static buffer)
 */
char *unprocess_lock(dbref player, char *arg)
{
  static char buffer[BUFFER_LEN];
  char *s = arg;
  char *t = buffer;
  char save;
  char *p;
  dbref thing;

  if (!arg)
  {
    *buffer = '\0';
    return buffer;
  }

  while (*s && (t - buffer < BUFFER_LEN - 1))
  {
    if (*s == '#')
    {
      s++;
      for (p = s; isdigit(*s); s++)
        ;
      save = *s;
      *s = '\0';
      thing = atol(p);
      if (thing >= db_top || thing < 0)
        thing = NOTHING;
      snprintf(t, BUFFER_LEN - (t - buffer), "%s", 
               unparse_object(player, thing));
      t += strlen(t);
      if (save != '\0')
      {
        if (t - buffer < BUFFER_LEN - 1)
          *t++ = save;
      }
      else
        s--;
    }
    else if (*s == '[')
    {
      while (*s && (*s != ']') && (t - buffer < BUFFER_LEN - 1))
        *t++ = *s++;
      if (*s && (t - buffer < BUFFER_LEN - 1))
        *t++ = *s++;
    }
    else
    {
      *t++ = *s++;
    }
  }
  
  *t = '\0';
  return buffer;
}

/* ===================================================================
 * Function Evaluation
 * =================================================================== */

/**
 * Evaluate functions within a lock string
 * @param buffer Output buffer
 * @param str Input string with potential [functions]
 * @param doer Object performing evaluation
 * @param privs Object providing privileges
 */
static void eval_fun(char *buffer, char *str, dbref doer, dbref privs)
{
  char *s;

  if (!buffer || !str)
    return;

  for (s = str; *s;)
  {
    if (*s == '[')
    {
      s++;
      museexec(&s, buffer, privs, doer, 0);
      buffer += strlen(buffer);
      if (*s)
        s++;
    }
    else
      *buffer++ = *s++;
  }
  *buffer = '\0';
}

/* ===================================================================
 * Boolean Expression Evaluation
 * =================================================================== */

/**
 * Evaluate a boolean expression lock
 * @param player Player being evaluated
 * @param object Object with the lock
 * @param key Lock string
 * @param zone Zone for evaluation
 * @return 1 if lock passes, 0 if it fails
 */
int eval_boolexp(dbref player, dbref object, char *key, dbref zone)
{
  static char keybuf[BUFFER_LEN];

  bool_depth = 0;
  parse_player = player;
  parse_object = object;
  parse_zone = zone;
  
  if (!key || !*key)
    return 1;
  
  if (strlen(key) >= BUFFER_LEN)
  {
    notify(db[object].owner,
           tprintf("Warning: lock too long on %s", 
                   unparse_object(db[object].owner, object)));
    return 0;
  }
  
  strcpy(keybuf, key);
  return eval_boolexp1(object, keybuf, zone);
}

/**
 * Internal recursive boolean expression evaluator
 * @param object Object with the lock
 * @param key Lock string to evaluate
 * @param zone Zone for evaluation
 * @return 1 if lock passes, 0 if it fails
 */
static int eval_boolexp1(dbref object, char *key, dbref zone)
{
  char buffer[BUFFER_LEN];
  char *buf;

  if (!*key)
    return 1;
    
  if (++bool_depth > 10)
  {
    notify(db[parse_object].owner,
           tprintf("Warning: recursion detected in %s lock.",
                   unparse_object(db[parse_object].owner, object)));
    return 0;
  }
  
  eval_fun(buffer, key, parse_player, object);
  buf = buffer;
  return eval_boolexp_OR(&buf);
}

/**
 * Evaluate OR operations in boolean expression
 * @param buf Pointer to buffer pointer (advanced by function)
 * @return Boolean result
 */
static int eval_boolexp_OR(char **buf)
{
  int left;

  left = eval_boolexp_AND(buf);
  if (**buf == OR_TOKEN)
  {
    (*buf)++;
    return (eval_boolexp_OR(buf) || left);
  }
  else
    return left;
}

/**
 * Evaluate AND operations in boolean expression
 * @param buf Pointer to buffer pointer (advanced by function)
 * @return Boolean result
 */
static int eval_boolexp_AND(char **buf)
{
  int left;

  left = eval_boolexp_REF(buf);
  if (**buf == AND_TOKEN)
  {
    (*buf)++;
    return (eval_boolexp_AND(buf) && left);
  }
  else
    return left;
}

/**
 * Test an attribute against a pattern
 * @param buf Pointer to buffer pointer (advanced by function)
 * @param player Object to check attribute on
 * @param ind Indirect flag
 * @return -1 on error, 0/1 for pattern match result
 */
static int test_atr(char **buf, dbref player, int ind)
{
  ATTR *attr;
  int result;
  char *s;
  char save;

  for (s = *buf; !RIGHT_DELIMITER(*s) || (*s == '.'); s++)
    ;
    
  if (!*s || (*s != ':'))
    return -1;
    
  *s++ = '\0';
  
  if (strchr(*buf, '.'))
    attr = atr_str(player, NOTHING, *buf);
  else
    attr = builtin_atr_str(*buf);
    
  if (!(attr) || (attr->flags & AF_DARK) ||
      ((ind) && !can_see_atr(parse_object, player, attr)))
    return -1;
    
  for (*buf = s; *s && (*s != AND_TOKEN) && (*s != OR_TOKEN) && (*s != ')'); s++)
    ;
    
  save = *s;
  *s = '\0';
  result = wild_match(*buf, atr_get(player, attr));
  *s = save;
  *buf = s;
  return result;
}

/**
 * Extract a number from the buffer
 * @param buf Pointer to buffer pointer (advanced by function)
 * @return Extracted number
 */
static int grab_num(char **buf)
{
  char *s;
  char save;
  int num;

  for (s = *buf; *s && isdigit(*s); s++)
    ;
    
  save = *s;
  *s = '\0';
  num = atoi(*buf);
  *s = save;
  *buf = s;
  return num;
}

/**
 * Extract a database reference from the buffer
 * @param buf Pointer to buffer pointer (advanced by function)
 * @return Database reference or NOTHING
 */
static dbref get_dbref(char **buf)
{
  char *s;
  char save;
  dbref temp;

  for (s = *buf; !RIGHT_DELIMITER(*s) && (*s != '='); s++)
    ;
    
  save = *s;
  *s = '\0';
  init_match(parse_object, *buf, NOTYPE);
  match_everything();
  *s = save;
  *buf = s;
  temp = match_result();
  return temp;
}

/**
 * Evaluate references (atoms) in boolean expressions
 * @param buf Pointer to buffer pointer (advanced by function)
 * @return Boolean result
 */
static int eval_boolexp_REF(char **buf)
{
  dbref thing;
  int type, t;

  switch (**buf)
  {
  case '(':
    (*buf)++;
    t = eval_boolexp_OR(buf);
    if (**buf == ')')
      (*buf)++;
    return t;
    
  case NOT_TOKEN:
    (*buf)++;
    return !eval_boolexp_REF(buf);
    
  case AT_TOKEN:
    (*buf)++;
    if (**buf == '(')
    {
      /* an indirect attribute */
      (*buf)++;
      thing = get_dbref(buf);
      if (**buf != '=')
        return eval_boolexp1(thing, atr_get(thing, A_LOCK), parse_zone);
      (*buf)++;
      t = test_atr(buf, thing, 1);
      if (**buf == ')')
        (*buf)++;
      return (t == -1) ? 0 : t;
    }
    thing = get_dbref(buf);
    if (can_see_atr(parse_object, thing, A_LOCK))
    {
      return eval_boolexp1(thing, atr_get(thing, A_LOCK), parse_zone);
    }
    else
      return 0;
      
  default:
    switch (**buf)
    {
    case IS_TOKEN:
      type = IS_TYPE;
      (*buf)++;
      break;
    case CARRY_TOKEN:
      type = CARRY_TYPE;
      (*buf)++;
      break;
    default:
      type = _TYPE;
    }
    
    if (isdigit(**buf))
      return grab_num(buf);
      
    if ((t = test_atr(buf, parse_player, 0)) != -1)
      return t;
      
    if ((thing = get_dbref(buf)) < (dbref) 0)
      return 0;
      
    switch (type)
    {
    case IS_TYPE:
      return (parse_player == thing) ? 1 : 0;
    case CARRY_TYPE:
      return member(thing, db[parse_player].contents);
    case _TYPE:
      return ((parse_player == thing) ||
              (member(thing, db[parse_player].contents)) ||
              (parse_zone == thing));
    default:
      return 0;
    }
  }
}
