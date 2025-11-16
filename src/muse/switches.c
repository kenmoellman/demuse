/* switches.c - Command switch definitions */

/* ============================================================================
 * MODERNIZATION NOTES (2025)
 * ============================================================================
 * This file has been modernized with the following improvements:
 * 
 * CODE QUALITY:
 * - Added comprehensive header documentation
 * - Better organization
 * - Explained purpose of switch system
 *
 * NOTES:
 * - This file is included by cmds.c
 * - Defines command-line switches/flags for commands
 * - Currently minimal as most switches are commented out
 */

/* ============================================================================
 * COMMAND SWITCH DEFINITIONS
 * ============================================================================
 * 
 * This array defines switches that can be used with commands.
 * Switches modify command behavior, similar to command-line flags.
 * 
 * Format: {"SWITCH_NAME", SWITCH_CONSTANT}
 * 
 * The list must be NULL-terminated.
 * 
 * FUTURE: Most switches are currently disabled (commented out).
 * They may be re-enabled as command functionality is restored.
 */

SWITCH_VALUE switch_list[] =
{
  /* Command switches would go here */
  /* Examples of disabled switches: */
  /* {"REBOOT", SWITCH_REBOOT}, */
  /* {"CONFIG", SWITCH_CONFIG}, */
  /* {"LINK", SWITCH_LINK}, */
  /* {"CREATE", SWITCH_CREATE}, */
  /* {"INFO", SWITCH_INFO}, */
  
  /* NULL terminator - required */
  {NULL, 0}
};

/* End of switches.c */
