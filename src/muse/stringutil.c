/* stringutil.c */
/* $Id: stringutil.c,v 1.3 1993/01/30 03:39:49 nils Exp $ */

/* ============================================================================
 * MODERNIZATION NOTES (2025)
 * ============================================================================
 * This file has been modernized with the following improvements:
 * 
 * SAFETY IMPROVEMENTS:
 * - Added NULL pointer checks to all functions
 * - Added bounds checking where appropriate
 * - Improved handling of edge cases
 *
 * CODE QUALITY:
 * - Full ANSI C compliance
 * - Better organization with section markers
 * - Comprehensive inline documentation
 * - Added safety notes for each function
 *
 * ANSI C COMPLIANCE:
 * - All function declarations use ANSI C prototypes
 * - Proper parameter types
 * 
 * SECURITY NOTES:
 * - All string operations validate NULL inputs
 * - Case conversion handles full ASCII range
 */

/* ============================================================================
 * INCLUDES
 * ============================================================================ */

#include <ctype.h>
#include "db.h"
#include "config.h"
#include "externs.h"

/* ============================================================================
 * CASE-INSENSITIVE STRING COMPARISON
 * ============================================================================ */

/**
 * Perform case-insensitive string comparison
 * 
 * This function compares two strings ignoring case differences.
 * It handles NULL inputs safely and compares until the end of
 * either string.
 * 
 * SAFETY: NULL pointer checks prevent crashes
 * 
 * @param s1 First string to compare
 * @param s2 Second string to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int string_compare(const char *s1, const char *s2)
{
  /* Handle NULL pointers */
  if (!s1 && !s2) {
    return 0;  /* Both NULL - equal */
  }
  if (!s1) {
    return -1;  /* s1 is NULL, s2 is not */
  }
  if (!s2) {
    return 1;   /* s2 is NULL, s1 is not */
  }

  /* Compare character by character, case-insensitively */
  while (*s1 && *s2 && (to_lower(*s1) == to_lower(*s2))) {
    s1++;
    s2++;
  }

  /* Handle end-of-string cases */
  if (!*s1) {
    if (!*s2) {
      return 0;  /* Both strings ended - equal */
    } else {
      return (0 - to_lower(*s2));  /* s1 shorter than s2 */
    }
  } else {
    return (to_lower(*s1) - to_lower(*s2));  /* Compare last chars */
  }
}

/* ============================================================================
 * PREFIX MATCHING
 * ============================================================================ */

/**
 * Check if string starts with prefix (case-insensitive)
 * 
 * This function determines if 'string' begins with 'prefix',
 * ignoring case differences.
 * 
 * SAFETY: NULL pointer checks
 * 
 * @param string String to check
 * @param prefix Prefix to look for
 * @return 1 if string starts with prefix, 0 otherwise
 */
int string_prefix(const char *string, char *prefix)
{
  /* Handle NULL pointers */
  if (!string || !prefix) {
    return 0;
  }

  /* Compare prefix characters */
  while (*string && *prefix && 
         to_lower(*string) == to_lower(*prefix)) {
    string++;
    prefix++;
  }

  /* Prefix matches if we've consumed all prefix characters */
  return (*prefix == '\0');
}

/* ============================================================================
 * WORD BOUNDARY SEARCH
 * ============================================================================ */

/**
 * Find substring in string at word boundaries only (case-insensitive)
 * 
 * This function searches for 'sub' in 'src', but only matches that
 * occur at the beginning of a word. A word is defined as a sequence
 * of alphanumeric characters.
 * 
 * Empty substrings are not matched.
 * 
 * SAFETY: NULL pointer checks, handles empty strings
 * 
 * @param src String to search in
 * @param sub Substring to search for
 * @return Pointer to start of match, or NULL if not found
 */
char *string_match(char *src, char *sub)
{
  /* Handle NULL or empty pointers */
  if (!src || !sub || *sub == '\0') {
    return NULL;
  }

  /* Search through source string */
  while (*src) {
    /* Check if current position matches substring */
    if (string_prefix(src, sub)) {
      return src;
    }

    /* Skip to beginning of next word */
    /* First, skip current word */
    while (*src && isalnum((unsigned char)*src)) {
      src++;
    }
    
    /* Then skip non-alphanumeric characters */
    while (*src && !isalnum((unsigned char)*src)) {
      src++;
    }
  }

  return NULL;
}

/* ============================================================================
 * CASE CONVERSION
 * ============================================================================ */

/**
 * Convert character to uppercase
 * 
 * This function converts lowercase letters to uppercase.
 * Non-lowercase characters are returned unchanged.
 * 
 * SAFETY: Handles full ASCII range safely
 * 
 * @param x Character to convert (as int for compatibility)
 * @return Uppercase version of character
 */
char to_upper(int x)
{
  if (x >= 'a' && x <= 'z') {
    return 'A' + (x - 'a');
  }
  return (char)x;
}

/**
 * Convert character to lowercase
 * 
 * This function converts uppercase letters to lowercase.
 * Non-uppercase characters are returned unchanged.
 * 
 * SAFETY: Handles full ASCII range safely
 * 
 * @param x Character to convert (as int for compatibility)
 * @return Lowercase version of character
 */
char to_lower(int x)
{
  if (x >= 'A' && x <= 'Z') {
    return 'a' + (x - 'A');
  }
  return (char)x;
}

/* ============================================================================
 * CHARACTER SEARCH
 * ============================================================================ */

/**
 * Find first occurrence of character in string
 * 
 * This is essentially a safe wrapper around strchr functionality.
 * 
 * SAFETY: NULL pointer check
 * 
 * @param what String to search in
 * @param chr Character to search for
 * @return Pointer to first occurrence, or NULL if not found
 */
char *str_index(char *what, int chr)
{
  char *x;

  if (!what) {
    return NULL;
  }

  for (x = what; *x; x++) {
    if (chr == *x) {
      return x;
    }
  }

  return NULL;
}

/* ============================================================================
 * INTEGER TO STRING CONVERSION
 * ============================================================================ */

/**
 * Convert integer to allocated string
 * 
 * This function converts an integer to a string, allocating
 * memory for the result. Zero is represented as an empty string.
 * 
 * MEMORY: Result is allocated via stralloc() and must be freed
 * by the MUD's memory management system.
 * 
 * @param a Integer to convert
 * @return Allocated string representation (empty string for 0)
 */
char *int_to_str(int a)
{
  char buf[100];

  if (a != 0) {
    snprintf(buf, sizeof(buf), "%d", a);
  } else {
    *buf = '\0';
  }

  return stralloc(buf);
}

/* End of stringutil.c */
