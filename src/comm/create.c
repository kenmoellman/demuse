/* create.c - MIGRATION NOTICE
 *
 * ============================================================================
 * ALL FUNCTIONS FROM THIS FILE HAVE BEEN REORGANIZED (2025)
 * ============================================================================
 *
 * This file has been emptied as part of the 2025 code reorganization.
 * All functions have been moved to their proper locations:
 *
 * OBJECT CREATION FUNCTIONS moved to: db/object.c
 * ------------------------------------------------
 * - do_open()           Create exits
 * - do_link()           Link exits
 * - do_dig()            Create rooms
 * - do_create()         Create things
 * - do_ucreate()        Create universe objects
 * - do_clone()          Clone objects
 * - do_robot()          Create robot players
 * - parse_linkable_room()  Helper function
 * - validate_object_name() Helper function
 *
 * ZONE/UNIVERSE FUNCTIONS moved to: muse/zones.c
 * -----------------------------------------------
 * - do_zlink()          Link to zone
 * - do_unzlink()        Unlink from zone
 * - do_gzone()          Set global zone
 * - do_ulink()          Link to universe
 * - do_unulink()        Unlink from universe
 * - do_guniverse()      Set global universe
 * - init_universe()     Initialize universe data
 *
 * See: db/object.h and zones.h for function declarations
 * ============================================================================
 */

/* This file intentionally left empty - all functions moved */
