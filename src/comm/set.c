/* set.c - MIGRATION NOTICE
 *
 * ============================================================================
 * ALL FUNCTIONS FROM THIS FILE HAVE BEEN REORGANIZED (2025)
 * ============================================================================
 *
 * This file has been emptied as part of the 2025 code reorganization.
 * All functions have been moved to their proper locations:
 *
 * ATTRIBUTE OPERATIONS moved to: db/attr.c
 * ------------------------------------------
 * - do_set()           @set command (attributes and flags)
 * - test_set()         Check for abbreviated set command
 * - parse_attrib()     Parse object/attribute specification
 * - do_edit()          @edit command (attribute editing)
 * - do_haven()         @haven command (haven flag)
 *
 * IDLE MANAGEMENT moved to: muse/idle.c
 * --------------------------------------
 * - do_idle()          @idle command
 * - do_away()          @away command
 * - set_idle_command() Set idle command wrapper
 * - set_idle()         Internal idle status setter
 * - set_unidle()       Internal unidle status handler
 *
 * See: hdrs/attr.h and hdrs/idle.h for function declarations
 * ============================================================================
 */

/* This file intentionally left empty - all functions moved */
