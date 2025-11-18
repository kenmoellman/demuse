/* credits.h */
/* $Id: credits.h,v 1.13 1997/02/10 23:43:09 mu-net org Exp $ */

/*
     This code began as TinyMUSH v1.5.  It has been heavily modified
     since that time by the following people:

	Bobby Newmark	newmark@mu-net.org
	NetRunner	kotcher@cis.ohio-state.edu
	shkoo		shkoo@chezmoto.ai.mit.edu
	Erk		erk@chezmoto.ai.mit.edu
	Michael		michael@chezmoto.ai.mit.edu

     The following variable defines the current version number.
*/

/*-------------------------------------------------------------------------*
   The following definitions are used to implement an automatic version
   number calculation system.  While major code changes reflect version
   number changes in the BASE_VERSION number variable (see below), day to
   day hacks require too much upkeep of the version number.  To simplify
   matters, dates of code changes are used to calculate a 4 field version
   number of the form: 'vX.X.X.X'.
 *-------------------------------------------------------------------------*/

/* If you make your own modifications to this code, please don't muck with
   our version number system as that might serve to confuse people.
   Instead, please simply uncomment the definition below for this purpose. */
/* #define MODIFIED */

/* Base version of this code.  In v<maj>.<min>, the major number reflects
   major code changes and major redesign.  The minor number reflects
   important code changes. */
#define BASE_VERSION	"|W+DeMUSE 2.26|"
#define BETA
#define BASE_REVISION   " 1"

/* These dates must be of the form MM/DD/YY */
/* BASE_DATE...: Date from last change to the value of BASE_VERSION */
/* UPGRADE_DATE: This is the release of this code. */
#define  BASE_DATE	"01/01/2026"
#define  UPGRADE_DATE	"01/01/2026"

/* This is the release number for a particular day.  If this is the third */
/* time mods have been released in a day, then this number should be 3    */
#define  DAY_RELEASE	1

/* This is the database version number.  Version numbers have been intoduced
   into the database to facilitate automatic database restructuring */
#define  DB_VERSION	14
