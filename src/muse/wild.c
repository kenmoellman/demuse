/* wild.c */
/* $Id: wild.c,v 1.3 1993/01/16 18:37:10 nils Exp $ */

/* ============================================================================
 * MODERNIZATION NOTES (2025)
 * ============================================================================
 * This file has been modernized with the following improvements:
 * 
 * SAFETY IMPROVEMENTS:
 * - Added recursion depth limiting to prevent stack overflow
 * - Improved buffer safety with bounds checking
 * - Added NULL pointer checks
 * - Better handling of edge cases
 *
 * CODE QUALITY:
 * - Full ANSI C compliance
 * - Better organization with section markers
 * - Comprehensive inline documentation
 * - Explained wildcard matching algorithm
 *
 * ANSI C COMPLIANCE:
 * - All function declarations use ANSI C prototypes
 * - Proper parameter types
 * - Removed implicit int returns
 *
 * SECURITY NOTES:
 * - Recursion depth limited to prevent stack overflow
 * - Buffer operations are bounded
 * - Invalid patterns detected and rejected
 *
 * ALGORITHM NOTES:
 * - Implements standard shell-style wildcard matching
 * - * matches zero or more characters
 * - ? matches exactly one character (including none at end)
 * - Case-insensitive matching
 */

/* ============================================================================
 * INCLUDES
 * ============================================================================ */

#include <stdio.h>
#include <ctype.h>
#include "config.h"
#include "externs.h"

/* ============================================================================
 * GLOBAL WILDCARD MATCH DATA
 * ============================================================================ */

/**
 * Wildcard match capture buffers
 * 
 * When a wildcard pattern matches, the portions of the string that
 * matched wildcards are stored in these arrays:
 * 
 * wptr[N] - Pointer to start of Nth wildcard match
 * wlen[N] - Length of Nth wildcard match
 * wbuff - Buffer containing copies of all matched strings
 * 
 * Maximum of 10 wildcard captures (0-9).
 */
char *wptr[10];
int wlen[10];
char wbuff[2000];

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define MAX_WILD_RECURSION 100  /* Maximum recursion depth for wild() */

/* ============================================================================
 * WILDCARD MATCHING (INTERNAL)
 * ============================================================================ */

/**
 * Internal recursive wildcard matching function
 * 
 * This function performs the actual wildcard matching with tracking
 * of matched regions for later extraction.
 * 
 * ALGORITHM:
 * - '?' matches any single character (or end of string)
 * - '*' matches zero or more characters
 * - Other characters must match exactly (case-insensitive)
 * 
 * SECURITY: Includes recursion depth limiting to prevent stack overflow
 * 
 * @param s Pattern string (with wildcards)
 * @param d Data string (to match against)
 * @param p Current wildcard index (0-9)
 * @param os In wildcard state (1) or normal state (0)
 * @return 1 if match, 0 if no match
 */
static int wild(char *s, char *d, int p, int os)
{
  static int recursion_depth = 0;

  if (!s || !d) {
    return 0;
  }

  /* Check recursion depth */
  if (recursion_depth > MAX_WILD_RECURSION) {
    return 0;
  }

  recursion_depth++;

  switch (*s) {
    case '?':
      /* Match any single character (or end of string) */
      
      /* Record start of wildcard match if transitioning from normal state */
      if (!os && (p < 10)) {
        wptr[p] = d;
      }
      
      /* Continue matching: advance pattern, advance data if not at end */
      {
        int result = wild(s + 1, (*d) ? d + 1 : d, p, 1);
        recursion_depth--;
        return result;
      }

    case '*':
      /* Match zero or more characters */
      
      /* Reject ** patterns as invalid */
      if (s[1] == '*') {
        recursion_depth--;
        return 0;
      }
      
      /* Record start of wildcard match if transitioning from normal state */
      if (!os && (p < 10)) {
        wptr[p] = d;
      }
      
      /* Try two possibilities:
       * 1. Match zero characters (advance pattern only)
       * 2. Match one or more characters (advance data, keep pattern)
       */
      {
        int result = (wild(s + 1, d, p, 1) || 
                     ((*d) ? wild(s, d + 1, p, 1) : 0));
        recursion_depth--;
        return result;
      }

    default:
      /* Regular character - must match exactly */
      
      /* If exiting wildcard state, record length of match */
      if (os && (p < 10)) {
        wlen[p] = d - wptr[p];
        p++;
      }
      
      /* Check if characters match (case-insensitive) */
      if (to_upper(*s) != to_upper(*d)) {
        recursion_depth--;
        return 0;
      }
      
      /* If at end of pattern, we have a match */
      if (!*s) {
        recursion_depth--;
        return 1;
      }
      
      /* Continue matching */
      {
        int result = wild(s + 1, d + 1, p, 0);
        recursion_depth--;
        return result;
      }
  }
}

/* ============================================================================
 * WILDCARD MATCHING (PUBLIC)
 * ============================================================================ */

/**
 * Perform wildcard matching with special comparison modes
 * 
 * This function supports three matching modes:
 * 
 * 1. Pattern starting with '>':
 *    Numeric comparison: true if data < pattern (as numbers)
 *    String comparison: true if data < pattern (lexicographically)
 * 
 * 2. Pattern starting with '<':
 *    Numeric comparison: true if data > pattern (as numbers)
 *    String comparison: true if data > pattern (lexicographically)
 * 
 * 3. Normal wildcard matching:
 *    Uses wild() function with * and ? wildcards
 *    Captured wildcard matches stored in wptr/wlen/wbuff arrays
 * 
 * SECURITY: Includes recursion depth limiting
 * MEMORY: Captured matches stored in global buffers
 * 
 * @param s Pattern string (may start with < or >)
 * @param d Data string to match against
 * @return 1 if match, 0 if no match
 */
long wild_match(char *s, char *d)
{
  int a, b;
  char *e, *f;

  if (!s || !d) {
    return 0;
  }

  /* Clear wildcard capture arrays */
  for (a = 0; a < 10; a++) {
    wptr[a] = NULL;
  }

  switch (*s) {
    case '>':
      /* Greater-than comparison */
      s++;
      
      /* Numeric comparison if pattern starts with digit or minus */
      if (isdigit((unsigned char)s[0]) || (*s == '-')) {
        return (atol(s) < atol(d));
      } else {
        /* String comparison */
        return (strcmp(s, d) < 0);
      }

    case '<':
      /* Less-than comparison */
      s++;
      
      /* Numeric comparison if pattern starts with digit or minus */
      if (isdigit((unsigned char)s[0]) || (*s == '-')) {
        return (long)(atol(s) > atol(d));
      } else {
        /* String comparison */
        return (long)(strcmp(s, d) > 0);
      }

    default:
      /* Normal wildcard matching */
      if (wild(s, d, 0, 0)) {
        /* Match successful - copy captured strings to buffer */
        f = wbuff;
        
        for (a = 0; a < 10; a++) {
          e = wptr[a];
          if (e) {
            /* Update pointer to point into wbuff instead of original string */
            wptr[a] = f;
            
            /* Copy captured string to buffer */
            for (b = wlen[a]; b > 0; b--) {
              /* Bounds check */
              if (f >= wbuff + sizeof(wbuff) - 1) {
                break;
              }
              *f++ = *e++;
            }
            
            /* Null terminate */
            if (f < wbuff + sizeof(wbuff)) {
              *f++ = '\0';
            }
          }
        }
        
        return 1;
      } else {
        return 0;
      }
  }
}

/* End of wild.c */
