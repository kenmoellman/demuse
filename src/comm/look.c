/* look.c */
/* Commands which look at things */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "db.h"
#include "config.h"
#include "interface.h"
#include "match.h"
#include "externs.h"

/* ===================================================================
 * Constants
 * =================================================================== */

#define MAX_LOOK_BUFFER 1024
#define MAX_EXIT_BUFFER 1024
#define MAX_FLAG_BUFFER BUFFER_LEN
#define MAX_SWEEP_BUFFER 30

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

char *eval_sweep(dbref thing);
char *unparse_list(dbref, const char *);


/* ===================================================================
 * Exit Display Functions
 * =================================================================== */

/**
 * Display exits from a location
 */
static void look_exits(dbref player, dbref loc, const char *exit_name)
{
    dbref thing;
    char buff[MAX_EXIT_BUFFER];
    char *e = buff;
    int flag = 0;
    
    /* Validate parameters */
    if (!GoodObject(player) || !GoodObject(loc)) {
        return;
    }
    
    if (!exit_name) {
        exit_name = "Obvious exits:";
    }
    
    /* Find first visible exit */
    for (thing = Exits(loc); (thing != NOTHING) && (Dark(thing)); thing = db[thing].next)
        ;
    
    if (thing == NOTHING) {
        return;
    }
    
    /* Build exit list */
    for (thing = Exits(loc); thing != NOTHING; thing = db[thing].next) {
        if (!(Dark(loc)) ||
            (IS(thing, TYPE_EXIT, EXIT_LIGHT) && controls(thing, loc, POW_MODIFY))) {
            if (!(Dark(thing))) {
                char *s;
                
                if (!flag) {
                    notify(player, exit_name);
                    flag++;
                }
                
                if (db[thing].cname && ((e - buff) < (MAX_EXIT_BUFFER - 100))) {
                    for (s = db[thing].cname; *s && (*s != ';') && (e - buff < MAX_EXIT_BUFFER - 10); *e++ = *s++)
                        ;
                    *e++ = ' ';
                    *e++ = ' ';
                }
            }
        }
    }
    
    *e = '\0';
    if (*buff) {
        notify(player, buff);
    }
}

/**
 * Display contents of a location
 */
static void look_contents(dbref player, dbref loc, const char *contents_name)
{
    dbref thing;
    dbref can_see_loc;
    
    /* Validate parameters */
    if (!GoodObject(player) || !GoodObject(loc)) {
        return;
    }
    
    if (!contents_name) {
        contents_name = "Contents:";
    }
    
    /* Check if player can see location */
    can_see_loc = !Dark(loc);
    
    /* Check for visible contents */
    DOLIST(thing, db[loc].contents) {
        if (can_see(player, thing, can_see_loc)) {
            /* Something exists - show everything */
            notify(player, contents_name);
            DOLIST(thing, db[loc].contents) {
                if (can_see(player, thing, can_see_loc)) {
                    notify(player, unparse_object_caption(player, thing));
                }
            }
            break;
        }
    }
}

/* ===================================================================
 * Attribute Functions
 * =================================================================== */

/**
 * Internal function to get all attributes including inherited
 */
struct all_atr_list *all_attributes_internal(dbref thing, 
                                             struct all_atr_list *myop,
                                             struct all_atr_list *myend, 
                                             int dep)
{
    ALIST *k;
    struct all_atr_list *tmp;
    int i;
    
    if (!GoodObject(thing)) {
        return myop;
    }
    
    for (k = db[thing].list; k; k = AL_NEXT(k)) {
        if (AL_TYPE(k) && ((dep == 0) || (AL_TYPE(k)->flags & AF_INHERIT))) {
            /* Check if already in list */
            for (tmp = myop; tmp; tmp = tmp->next) {
                if (tmp->type == AL_TYPE(k)) {
                    break;
                }
            }
            
            if (tmp) {
                continue;  /* Already have this attribute */
            }
            
            /* Add to list */
            if (!myop) {
                myop = myend = stack_em(sizeof(struct all_atr_list));
                myop->next = NULL;
            } else {
                struct all_atr_list *foo;
                foo = stack_em(sizeof(struct all_atr_list));
                foo->next = myop;
                myop = foo;
            }
            
            myop->type = AL_TYPE(k);
            myop->value = AL_STR(k);
            myop->numinherit = dep;
        }
    }
    
    /* Check parents */
    for (i = 0; db[thing].parents && db[thing].parents[i] != NOTHING; i++) {
        if (myend) {
            myop = all_attributes_internal(db[thing].parents[i], myop, myend, dep + 1);
        } else {
            myop = all_attributes_internal(db[thing].parents[i], 0, 0, dep + 1);
        }
        
        while (myend && myend->next) {
            myend = myend->next;
        }
    }
    
    return myop;
}

/**
 * Get all attributes for an object
 */
struct all_atr_list *all_attributes(dbref thing)
{
    return all_attributes_internal(thing, 0, 0, 0);
}

/**
 * Parse a list containing dbrefs and convert to object names
 */
char *unparse_list(dbref player, const char *list)
{
    char buf[MAX_LOOK_BUFFER];
    int pos = 0;
    
    if (!GoodObject(player) || !list) {
        return stralloc("");
    }
    
    while (pos < (MAX_LOOK_BUFFER - 100) && *list) {
        if (*list == '#' && list[1] >= '0' && list[1] <= '9') {
            int y = atoi(list + 1);
            char *x;
            
            if (y >= HOME || y < db_top) {
                x = unparse_object(player, y);
                if (x && strlen(x) + pos < (MAX_LOOK_BUFFER - 100)) {
                    buf[pos] = ' ';
                    strncpy(buf + pos + 1, x, MAX_LOOK_BUFFER - pos - 2);
                    buf[MAX_LOOK_BUFFER - 1] = '\0';
                    pos += strlen(buf + pos);
                    list++;  /* skip the # */
                    while (*list >= '0' && *list <= '9') {
                        list++;
                    }
                    continue;
                }
            }
        }
        
        if (pos < MAX_LOOK_BUFFER - 1) {
            buf[pos++] = *list++;
        } else {
            break;
        }
    }
    
    buf[pos] = '\0';
    return (*buf) ? stralloc(buf + 1) : stralloc(buf);
}

/**
 * Display a single attribute
 */
static void look_atr(dbref player, struct all_atr_list *allatrs)
{
    long cl;
    ATTR *attr;
    
    if (!GoodObject(player) || !allatrs || !allatrs->type) {
        return;
    }
    
    attr = allatrs->type;
    
    if (attr->flags & AF_DATE) {
        cl = atol((allatrs->value));
        notify(player, tprintf("%s:%s", unparse_attr(attr, allatrs->numinherit),
                              mktm(cl, "D", player)));
    }
    else if (attr->flags & AF_LOCK) {
        notify(player,
               tprintf("%s:%s", unparse_attr(attr, allatrs->numinherit),
                      unprocess_lock(player, stralloc(allatrs->value))));
    }
    else if (attr->flags & AF_FUNC) {
        notify_noc(player,
                   tprintf("%s():%s", unparse_attr(attr, allatrs->numinherit),
                          (allatrs->value)));
    }
    else if (attr->flags & AF_DBREF) {
        notify(player,
               tprintf("%s:%s", unparse_attr(attr, allatrs->numinherit),
                      unparse_list(player, (allatrs->value))));
    }
    else if (attr->flags & AF_TIME) {
        notify(player,
               tprintf("%s:%s", unparse_attr(attr, allatrs->numinherit),
                      time_format_4(atol((allatrs->value)))));
    }
    else {
        notify_noc(player,
                   tprintf("%s:%s", unparse_attr(attr, allatrs->numinherit),
                          (allatrs->value)));
    }
}

/**
 * Display all attributes for an object
 */
static void look_atrs(dbref player, dbref thing, int doall)
{
    struct all_atr_list *allatrs;
    ATTR *attr;
    
    if (!GoodObject(player) || !GoodObject(thing)) {
        return;
    }
    
    for (allatrs = all_attributes(thing); allatrs; allatrs = allatrs->next) {
        if ((allatrs->type != A_DESC) &&
            (attr = allatrs->type) &&
            (allatrs->numinherit == 0 || 
             !(db[allatrs->type->obj].flags & SEE_OK) || doall) &&
            can_see_atr(player, thing, attr)) {
            look_atr(player, allatrs);
        }
    }
}

/**
 * Simple look at an object
 */
static void look_simple(dbref player, dbref thing, int doatrs)
{
    if (!GoodObject(player) || !GoodObject(thing)) {
        return;
    }
    
    if (controls(player, thing, POW_EXAMINE) || (db[thing].flags & SEE_OK)) {
        notify(player, unparse_object_caption(player, thing));
    }
    
    if (doatrs) {
        did_it(player, thing, A_DESC, "You see nothing special.", 
               A_ODESC, NULL, A_ADESC);
    } else {
        did_it(player, thing, A_DESC, "You see nothing special.", 
               NULL, NULL, NULL);
    }
    
    if (Typeof(thing) == TYPE_EXIT) {
        if (db[thing].flags & OPAQUE) {
            if (db[thing].link != NOTHING) {
                notify(player, tprintf("You peer through to %s...", 
                                      db[db[thing].link].name));
                did_it(player, db[thing].link, A_DESC, 
                      "You see nothing on the other side.",
                      doatrs ? A_ODESC : NULL, NULL, 
                      doatrs ? A_ADESC : NULL);
                look_contents(player, db[thing].link, "You also notice:");
            }
        }
    }
}

/**
 * Look at a room
 */
void look_room(dbref player, dbref loc)
{
    char *s;
    
    if (!GoodObject(player) || !GoodObject(loc)) {
        return;
    }
    
    /* Tell name and number if can link */
    notify(player, unparse_object_caption(player, loc));
    
    if (Typeof(loc) != TYPE_ROOM) {
        did_it(player, loc, A_IDESC, NULL, A_OIDESC, NULL, A_AIDESC);
    } else {
        if (!(db[player].flags & PLAYER_TERSE)) {
            if (*(s = atr_get(get_zone_first(player), A_IDESC)) && 
                !(db[loc].flags & OPAQUE)) {
                notify(player, s);
            }
            did_it(player, loc, A_DESC, NULL, A_ODESC, NULL, A_ADESC);
        }
    }
    
    /* Check key and send appropriate messages */
    if (Typeof(loc) == TYPE_ROOM) {
        if (could_doit(player, loc, A_LOCK)) {
            did_it(player, loc, A_SUCC, NULL, A_OSUCC, NULL, A_ASUCC);
        } else {
            did_it(player, loc, A_FAIL, NULL, A_OFAIL, NULL, A_AFAIL);
        }
    }
    
    /* Show contents and exits */
    look_contents(player, loc, "Contents:");
    look_exits(player, loc, "Obvious exits:");
}

/**
 * Look around at current location
 */
void do_look_around(dbref player)
{
    dbref loc;
    
    if (!GoodObject(player)) {
        return;
    }
    
    if ((loc = getloc(player)) == NOTHING) {
        return;
    }
    
    look_room(player, loc);
}

/**
 * Look at a specific object or in a direction
 */
void do_look_at(dbref player, char *arg1)
{
    dbref thing, thing2;
    char *s, *p;
    char *name = arg1;
    
    if (!GoodObject(player)) {
        return;
    }
    
    if (!name) {
        name = "";
    }
    
    if (*name == '\0') {
        if ((thing = getloc(player)) != NOTHING) {
            look_room(player, thing);
        }
    } else {
        /* Look at a thing here */
        init_match(player, name, NOTYPE);
        match_exit();
        match_neighbor();
        match_possession();
        
        if (power(player, POW_REMOTE)) {
            match_absolute();
            match_player(NOTHING, NULL);
        }
        
        match_here();
        match_me();
        
        switch (thing = match_result()) {
        case NOTHING:
            for (s = name; *s && *s != ' '; s++)
                ;
            
            if (!*s) {
                notify(player, tprintf(NOMATCH_PATT, arg1));
                return;
            }
            
            p = name;
            if ((*(s - 1) == 's' && *(s - 2) == '\'' && *(s - 3) != 's') ||
                (*(s - 1) == '\'' && *(s - 2) == 's')) {
                
                if (*(s - 1) == 's') {
                    *(s - 2) = '\0';
                } else {
                    *(s - 1) = '\0';
                }
                
                name = s + 1;
                init_match(player, p, TYPE_PLAYER);
                match_neighbor();
                match_possession();
                
                switch (thing = match_result()) {
                case NOTHING:
                    notify(player, tprintf(NOMATCH_PATT, arg1));
                    break;
                case AMBIGUOUS:
                    notify(player, AMBIGUOUS_MESSAGE);
                    break;
                default:
                    init_match(thing, name, TYPE_THING);
                    match_possession();
                    
                    switch (thing2 = match_result()) {
                    case NOTHING:
                        notify(player, tprintf(NOMATCH_PATT, arg1));
                        break;
                    case AMBIGUOUS:
                        notify(player, AMBIGUOUS_MESSAGE);
                        break;
                    default:
                        if ((db[thing].flags & OPAQUE) &&
                            !power(player, POW_EXAMINE)) {
                            notify(player, tprintf(NOMATCH_PATT, name));
                        } else {
                            look_simple(player, thing2, 0);
                        }
                    }
                }
            } else {
                notify(player, tprintf(NOMATCH_PATT, arg1));
            }
            break;
            
        case AMBIGUOUS:
            notify(player, AMBIGUOUS_MESSAGE);
            break;
            
        default:
            switch (Typeof(thing)) {
            case TYPE_ROOM:
                look_room(player, thing);
                break;
            case TYPE_THING:
            case TYPE_PLAYER:
            case TYPE_CHANNEL:
#ifdef USE_UNIV
            case TYPE_UNIVERSE:
#endif
                look_simple(player, thing, 1);
                if (controls(player, thing, POW_EXAMINE) ||
                    !(db[thing].flags & OPAQUE) ||
                    power(player, POW_EXAMINE)) {
                    look_contents(player, thing, "Carrying:");
                }
                break;
            default:
                look_simple(player, thing, 1);
                break;
            }
        }
    }
}

/**
 * Get flag description for an object
 */
char *flag_description(dbref thing)
{
    char buf[MAX_FLAG_BUFFER];
    
    if (!GoodObject(thing)) {
        return stralloc("Invalid object");
    }
    
    snprintf(buf, sizeof(buf), "Type:");
    
    switch (Typeof(thing)) {
    case TYPE_ROOM:
        strncat(buf, "Room", sizeof(buf) - strlen(buf) - 1);
        break;
    case TYPE_EXIT:
        strncat(buf, "Exit", sizeof(buf) - strlen(buf) - 1);
        break;
    case TYPE_THING:
        strncat(buf, "Thing", sizeof(buf) - strlen(buf) - 1);
        break;
    case TYPE_CHANNEL:
        strncat(buf, "Channel", sizeof(buf) - strlen(buf) - 1);
        break;
#ifdef USE_UNIV
    case TYPE_UNIVERSE:
        strncat(buf, "Universe", sizeof(buf) - strlen(buf) - 1);
        break;
#endif
    case TYPE_PLAYER:
        strncat(buf, "Player", sizeof(buf) - strlen(buf) - 1);
        break;
    default:
        strncat(buf, "***UNKNOWN TYPE***", sizeof(buf) - strlen(buf) - 1);
        break;
    }
    
    if (db[thing].flags & ~TYPE_MASK) {
        /* Print flags */
        strncat(buf, "      Flags:", sizeof(buf) - strlen(buf) - 1);
        
        if (db[thing].flags & GOING)
            strncat(buf, " Going", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & PUPPET)
            strncat(buf, " Puppet", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & STICKY)
            strncat(buf, " Sticky", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & DARK)
            strncat(buf, " Dark", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & LINK_OK)
            strncat(buf, " Link_ok", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & HAVEN)
            strncat(buf, " Haven", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & CHOWN_OK)
            strncat(buf, " Chown_ok", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & ENTER_OK)
            strncat(buf, " Enter_ok", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & SEE_OK)
            strncat(buf, " Visible", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & OPAQUE)
            strncat(buf, (Typeof(thing) == TYPE_EXIT) ? " Transparent" : " Opaque",
                   sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & INHERIT_POWERS)
            strncat(buf, " Inherit", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & QUIET)
            strncat(buf, " Quiet", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & BEARING)
            strncat(buf, " Bearing", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].flags & CONNECT)
            strncat(buf, " Connected", sizeof(buf) - strlen(buf) - 1);
        
        switch (Typeof(thing)) {
        case TYPE_PLAYER:
            if (db[thing].flags & PLAYER_SLAVE)
                strncat(buf, " Slave", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & PLAYER_TERSE)
                strncat(buf, " Terse", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & PLAYER_MORTAL)
                strncat(buf, " Mortal", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & PLAYER_NO_WALLS)
                strncat(buf, " No_walls", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & PLAYER_ANSI)
                strncat(buf, " ANSI", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & PLAYER_NOBEEP)
                strncat(buf, " NoBeep", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & PLAYER_FREEZE)
                strncat(buf, " Freeze", sizeof(buf) - strlen(buf) - 1);
            break;
        case TYPE_EXIT:
            if (db[thing].flags & EXIT_LIGHT)
                strncat(buf, " Light", sizeof(buf) - strlen(buf) - 1);
            break;
        case TYPE_THING:
            if (db[thing].flags & THING_KEY)
                strncat(buf, " Key", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & THING_DEST_OK)
                strncat(buf, " Destroy_ok", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & THING_SACROK)
                strncat(buf, " X_ok", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & THING_LIGHT)
                strncat(buf, " Light", sizeof(buf) - strlen(buf) - 1);
            break;
        case TYPE_ROOM:
            if (db[thing].flags & ROOM_JUMP_OK)
                strncat(buf, " Jump_ok", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & ROOM_AUDITORIUM)
                strncat(buf, " Auditorium", sizeof(buf) - strlen(buf) - 1);
            if (db[thing].flags & ROOM_FLOATING)
                strncat(buf, " Floating", sizeof(buf) - strlen(buf) - 1);
            break;
        }
        
        if (db[thing].i_flags & I_MARKED)
            strncat(buf, " Marked", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].i_flags & I_QUOTAFULL)
            strncat(buf, " Quotafull", sizeof(buf) - strlen(buf) - 1);
        if (db[thing].i_flags & I_UPDATEBYTES)
            strncat(buf, " Updatebytes", sizeof(buf) - strlen(buf) - 1);
    }
    
    buf[sizeof(buf) - 1] = '\0';
    return stralloc(buf);
}

/**
 * Examine an object in detail
 */
void do_examine(dbref player, char *name, char *arg2)
{
    dbref thing;
    dbref content;
    dbref exit;
    dbref enter;
    char *rq, *rqm, *cr, *crm;
    char buf[16];
    int pos = 0;
    struct all_atr_list attr_entry;
    
    if (!GoodObject(player)) {
        return;
    }
    
    if (!name) {
        name = "";
    }
    if (!arg2) {
        arg2 = "";
    }
    
    if (*name == '\0') {
        if ((thing = getloc(player)) == NOTHING) {
            return;
        }
    } else {
        if (strchr(name, '/')) {
            if (!parse_attrib(player, name, &thing, &(attr_entry.type), 0)) {
                notify(player, "No match.");
            } else if (!can_see_atr(player, thing, attr_entry.type)) {
                notify(player, perm_denied());
            } else {
                attr_entry.value = atr_get(thing, attr_entry.type);
                attr_entry.numinherit = 0;
                look_atr(player, &attr_entry);
            }
            return;
        }
        
        /* Look it up */
        init_match(player, name, NOTYPE);
        match_exit();
        match_neighbor();
        match_possession();
        match_absolute();
        
        if (has_pow(player, NOTHING, POW_EXAMINE) || 
            has_pow(player, NOTHING, POW_REMOTE)) {
            match_player(NOTHING, NULL);
        }
        
        match_here();
        match_me();
        
        if ((thing = noisy_match_result()) == NOTHING) {
            return;
        }
    }
    
    if ((!can_link(player, thing, POW_EXAMINE) &&
         !(db[thing].flags & SEE_OK))) {
        char buf2[BUFFER_LEN];
        
        strncpy(buf2, unparse_object(player, thing), sizeof(buf2) - 1);
        buf2[sizeof(buf2) - 1] = '\0';
        
        notify(player, tprintf("%s is owned by %s",
                              buf2, unparse_object(player, db[thing].owner)));
        look_atrs(player, thing, *arg2);
        
        /* Check for owned contents */
        DOLIST(content, db[thing].contents) {
            if (can_link(player, content, POW_EXAMINE)) {
                notify(player, "Contents:");
                DOLIST(content, db[thing].contents) {
                    if (can_link(player, content, POW_EXAMINE)) {
                        notify(player, unparse_object(player, content));
                    }
                }
                break;
            }
        }
        return;
    }
    
    notify(player, unparse_object_caption(player, thing));
    
    if (*Desc(thing) && can_see_atr(player, thing, A_DESC)) {
        notify(player, Desc(thing));
    }
    
    rq = rqm = "";
    snprintf(buf, sizeof(buf), "%ld", Pennies(thing));
    cr = buf;
    crm = "  Credits: ";
    
    if (Typeof(thing) == TYPE_PLAYER) {
        if (Robot(thing)) {
            cr = crm = "";
        } else {
            if (inf_mon(thing)) {
                cr = "INFINITE";
            }
            rqm = "  Quota-Left: ";
            if (inf_quota(thing)) {
                rq = "INFINITE";
            } else {
                rq = atr_get(thing, A_RQUOTA);
                if (atol(rq) <= 0) {
                    rq = "NONE";
                }
            }
        }
    }
    
    notify(player, tprintf("Owner:%s%s%s%s%s",
                          db[db[thing].owner].cname, crm, cr, rqm, rq));
    notify(player, flag_description(thing));
    
    if (db[thing].zone != NOTHING) {
        notify(player, tprintf("Zone:%s",
                              unparse_object(player, db[thing].zone)));
    }
    
#ifdef USE_UNIV
    if (db[thing].universe != NOTHING) {
        notify(player, tprintf("Universe:%s",
                              unparse_object(player, db[thing].universe)));
    }
#endif
    
    notify(player, tprintf("Created:%s",
                          db[thing].create_time ?
                          mktm(db[thing].create_time, "D", player) : "never"));
    notify(player, tprintf("Modified:%s",
                          db[thing].mod_time ?
                          mktm(db[thing].mod_time, "D", player) : "never"));
    
    /* Show parents */
    if (db[thing].parents && *db[thing].parents != NOTHING) {
        char obuf[MAX_LOOK_BUFFER];
        char tbuf[MAX_LOOK_BUFFER];
        int i;
        
        strncpy(obuf, "Parents:", sizeof(obuf) - 1);
        obuf[sizeof(obuf) - 1] = '\0';
        
        for (i = 0; db[thing].parents[i] != NOTHING; i++) {
            snprintf(tbuf, sizeof(tbuf), " %s", 
                    unparse_object(player, db[thing].parents[i]));
            
            if (strlen(tbuf) + strlen(obuf) > sizeof(obuf) - 10) {
                notify(player, obuf);
                strncpy(obuf, tbuf + 1, sizeof(obuf) - 1);
                obuf[sizeof(obuf) - 1] = '\0';
            } else {
                strncat(obuf, tbuf, sizeof(obuf) - strlen(obuf) - 1);
            }
        }
        notify(player, obuf);
    }
    
    /* Show attribute definitions */
    if (db[thing].atrdefs) {
        ATRDEF *k;
        
        notify(player, "Attribute definitions:");
        for (k = db[thing].atrdefs; k; k = k->next) {
            char abuf[MAX_LOOK_BUFFER];
            
            snprintf(abuf, sizeof(abuf), "  %s%s%c", k->a.name,
                    (k->a.flags & AF_FUNC) ? "()" : "",
                    (k->a.flags) ? ':' : '\0');
            
            if (k->a.flags & AF_WIZARD)
                strncat(abuf, " Wizard", sizeof(abuf) - strlen(abuf) - 1);
            if (k->a.flags & AF_UNIMP)
                strncat(abuf, " Unsaved", sizeof(abuf) - strlen(abuf) - 1);
            if (k->a.flags & AF_OSEE)
                strncat(abuf, " Osee", sizeof(abuf) - strlen(abuf) - 1);
            if (k->a.flags & AF_INHERIT)
                strncat(abuf, " Inherit", sizeof(abuf) - strlen(abuf) - 1);
            if (k->a.flags & AF_DARK)
                strncat(abuf, " Dark", sizeof(abuf) - strlen(abuf) - 1);
            if (k->a.flags & AF_DATE)
                strncat(abuf, " Date", sizeof(abuf) - strlen(abuf) - 1);
            if (k->a.flags & AF_LOCK)
                strncat(abuf, " Lock", sizeof(abuf) - strlen(abuf) - 1);
            if (k->a.flags & AF_FUNC)
                strncat(abuf, " Function", sizeof(abuf) - strlen(abuf) - 1);
            if (k->a.flags & AF_DBREF)
                strncat(abuf, " Dbref", sizeof(abuf) - strlen(abuf) - 1);
            if (k->a.flags & AF_HAVEN)
                strncat(abuf, " Haven", sizeof(abuf) - strlen(abuf) - 1);
            
            notify(player, abuf);
        }
    }
    
    look_atrs(player, thing, *arg2);
    
    /* Show contents */
    if (db[thing].contents != NOTHING) {
        notify(player, "Contents:");
        DOLIST(content, db[thing].contents) {
            notify(player, unparse_object(player, content));
        }
    }
    
    fflush(stdout);
    
    switch (Typeof(thing)) {
    case TYPE_ROOM:
        /* Show entrances */
        for (enter = 0; enter < db_top; enter++) {
            if (Typeof(enter) == TYPE_EXIT && db[enter].link == thing) {
                if (pos == 0) {
                    notify(player, "Entrances:");
                }
                pos = 1;
                notify(player, unparse_object(player, enter));
            }
        }
        if (pos == 0) {
            notify(player, "No Entrances.");
        }
        
        /* Show exits */
        if (Exits(thing) != NOTHING) {
            notify(player, "Exits:");
            DOLIST(exit, Exits(thing)) {
                notify(player, unparse_object(player, exit));
            }
        } else {
            notify(player, "No exits.");
        }
        
        /* Show dropto */
        if (db[thing].link != NOTHING) {
            notify(player,
                   tprintf("Dropped objects go to:%s",
                          unparse_object(player, db[thing].link)));
        }
        break;
        
    case TYPE_THING:
    case TYPE_PLAYER:
    case TYPE_CHANNEL:
#ifdef USE_UNIV
    case TYPE_UNIVERSE:
#endif
        /* Show home */
        notify(player,
               tprintf("Home:%s",
                      unparse_object(player, db[thing].link)));
        
        /* Show location */
        if (db[thing].location != NOTHING &&
            (controls(player, db[thing].location, POW_EXAMINE) ||
             controls(player, thing, POW_EXAMINE) ||
             can_link_to(player, db[thing].location, POW_EXAMINE))) {
            notify(player,
                   tprintf("Location:%s",
                          unparse_object(player, db[thing].location)));
        }
        
        /* Show entrances for things */
        if (Typeof(thing) == TYPE_THING) {
            for (enter = 0; enter < db_top; enter++) {
                if (Typeof(enter) == TYPE_EXIT && db[enter].link == thing) {
                    if (pos == 0) {
                        notify(player, "Entrances:");
                    }
                    pos = 1;
                    notify(player, unparse_object(player, enter));
                }
            }
        }
        
        /* Show exits for things */
        if (Typeof(thing) == TYPE_THING && db[thing].exits != NOTHING) {
            notify(player, "Exits:");
            DOLIST(exit, db[thing].exits) {
                notify(player, unparse_object(player, exit));
            }
        }
        break;
        
    case TYPE_EXIT:
        /* Show source */
        notify(player, tprintf("Source:%s",
                              unparse_object(player, db[thing].location)));
        
        /* Show destination */
        switch (db[thing].link) {
        case NOTHING:
            break;
        case HOME:
            notify(player, "Destination:*HOME*");
            break;
        default:
            notify(player,
                   tprintf("Destination:%s",
                          unparse_object(player, db[thing].link)));
            break;
        }
        break;
        
    default:
        break;
    }
}

/**
 * Show player's credit balance
 */
void do_score(dbref player)
{
    if (!GoodObject(player)) {
        return;
    }
    
    notify(player,
           tprintf("You have %ld %s.",
                  Pennies(player),
                  Pennies(player) == 1 ? "Credit" : "Credits"));
}

/**
 * Show player's inventory
 */
void do_inventory(dbref player)
{
    dbref thing;
    
    if (!GoodObject(player)) {
        return;
    }
    
    if ((thing = db[player].contents) == NOTHING) {
        notify(player, "You aren't carrying anything.");
    } else {
        notify(player, "You are carrying:");
        DOLIST(thing, thing) {
            notify(player, unparse_object(player, thing));
        }
    }
    
    do_score(player);
}

/**
 * Find objects owned by player
 */
void do_find(dbref player, char *name)
{
    dbref i;
    
    if (!GoodObject(player)) {
        return;
    }
    
    if (!name) {
        name = "";
    }
    
    if (!payfor(player, find_cost)) {
        notify(player, "You don't have enough Credits.");
    } else {
        for (i = 0; i < db_top; i++) {
            if (Typeof(i) != TYPE_EXIT &&
                (power(player, POW_EXAMINE) || db[i].owner == db[player].owner) &&
                (!*name || string_match(db[i].name, name))) {
                notify(player, unparse_object(player, i));
            }
        }
        notify(player, "***End of List***");
    }
}

/**
 * Print attribute matches for sweep
 */
static void print_atr_match(dbref thing, dbref player, const char *str)
{
    struct all_atr_list *ptr;
    
    if (!GoodObject(thing) || !GoodObject(player) || !str) {
        return;
    }
    
    for (ptr = all_attributes(thing); ptr; ptr = ptr->next) {
        if ((ptr->type != 0) && !(ptr->type->flags & AF_LOCK) &&
            ((*ptr->value == '!') || (*ptr->value == '$'))) {
            char buff[MAX_LOOK_BUFFER];
            char *s;
            
            /* Copy and decode */
            strncpy(buff, (ptr->value), sizeof(buff) - 1);
            buff[sizeof(buff) - 1] = '\0';
            
            /* Search for first unescaped : */
            for (s = buff + 1; *s && (*s != ':'); s++)
                ;
            
            if (!*s) {
                continue;
            }
            
            *s++ = '\0';
            
            if (wild_match(buff + 1, (char *)str)) {
                if (controls(player, thing, POW_SEEATR)) {
                    notify(player, tprintf(" %s/%s: %s", 
                                          unparse_object(player, thing),
                                          unparse_attr(ptr->type, ptr->numinherit), 
                                          buff + 1));
                } else {
                    notify(player, tprintf(" %s", unparse_object(player, thing)));
                }
            }
        }
    }
}

/**
 * Sweep for listening objects
 */
void do_sweep(dbref player, char *arg1)
{
    dbref zon;
    
    if (!GoodObject(player)) {
        return;
    }
    
    if (!arg1) {
        arg1 = "";
    }
    
    if (*arg1) {
        dbref i;
        
        notify(player, tprintf("All places that respond to %s:", arg1));
        
        for (i = db[db[player].location].contents; i != NOTHING; i = db[i].next) {
            if (Typeof(i) != TYPE_PLAYER || i == player) {
                print_atr_match(i, player, arg1);
            }
        }
        
        for (i = db[player].contents; i != NOTHING; i = db[i].next) {
            if (Typeof(i) != TYPE_PLAYER || i == player) {
                print_atr_match(i, player, arg1);
            }
        }
        
        print_atr_match(db[player].location, player, arg1);
        
        for (i = db[db[player].location].exits; i != NOTHING; i = db[i].next) {
            if (Typeof(i) != TYPE_PLAYER || i == player) {
                print_atr_match(i, player, arg1);
            }
        }
        
        print_atr_match(db[player].zone, player, arg1);
        
        if (db[player].zone != db[0].zone) {
            print_atr_match(db[0].zone, player, arg1);
        }
    } else {
        dbref here = db[player].location;
        dbref test;
        
        test = here;
        if (here == NOTHING) {
            return;
        }
        
        if (Dark(here)) {
            notify(player, "Sorry it is dark here; you can't search for bugs");
            return;
        }
        
        notify(player, "Sweeping...");
        
        DOZONE(zon, player) {
            if (Hearer(zon)) {
                notify(player, "Zone:");
                break;
            }
        }
        
        DOZONE(zon, player) {
            if (Hearer(zon)) {
                notify(player, "Zone:");
                notify(player, tprintf("  %s =%s.", db[zon].name,
                                      eval_sweep(db[here].zone)));
            }
        }
        
        if (Hearer(here)) {
            notify(player, "Room:");
            notify(player, tprintf("  %s =%s.", db[here].name,
                                  eval_sweep(here)));
        }
        
        for (test = db[test].contents; test != NOTHING; test = db[test].next) {
            if (Hearer(test)) {
                notify(player, "Contents:");
                break;
            }
        }
        
        test = here;
        for (test = db[test].contents; test != NOTHING; test = db[test].next) {
            if (Hearer(test)) {
                notify(player, tprintf("  %s =%s.", db[test].name,
                                      eval_sweep(test)));
            }
        }
        
        test = here;
        for (test = db[test].exits; test != NOTHING; test = db[test].next) {
            if (Hearer(test)) {
                notify(player, "Exits:");
                break;
            }
        }
        
        test = here;
        for (test = db[test].exits; test != NOTHING; test = db[test].next) {
            if (Hearer(test)) {
                notify(player, tprintf("  %s =%s.", db[test].name,
                                      eval_sweep(test)));
            }
        }
        
        for (test = db[player].contents; test != NOTHING; test = db[test].next) {
            if (Hearer(test)) {
                notify(player, "Inventory:");
                break;
            }
        }
        
        for (test = db[player].contents; test != NOTHING; test = db[test].next) {
            if (Hearer(test)) {
                notify(player, tprintf("  %s =%s.", db[test].name,
                                      eval_sweep(test)));
            }
        }
        
        notify(player, "Done.");
    }
}

/**
 * Find a player's location
 */
void do_whereis(dbref player, char *name)
{
    dbref thing;
    
    if (!GoodObject(player)) {
        return;
    }
    
    if (!name || *name == '\0') {
        notify(player, "You must specify a valid player name.");
        return;
    }
    
    if ((thing = lookup_player(name)) == NOTHING) {
        notify(player, tprintf("%s does not seem to exist.", name));
        return;
    }
    
    if (db[thing].flags & DARK) {
        notify(player, tprintf("%s wishes to have some privacy.", db[thing].name));
        if (!could_doit(player, thing, A_LPAGE)) {
            notify(thing,
                   tprintf("%s tried to locate you and failed.",
                          unparse_object(thing, player)));
        }
        return;
    }
    
    notify(player,
           tprintf("%s is at: %s.", db[thing].name,
                  unparse_object(player, db[thing].location)));
    
    if (!could_doit(player, thing, A_LPAGE)) {
        notify(thing,
               tprintf("%s has just located your position.",
                      unparse_object(thing, player)));
    }
}

/**
 * Show when a player was last online
 */
void do_laston(dbref player, char *name)
{
    char *attr;
    dbref thing;
    long tim;
    
    if (!GoodObject(player)) {
        return;
    }
    
    if (!name || *name == '\0') {
        notify(player, "You must specify a valid player name.");
        return;
    }
    
    if ((thing = lookup_player(name)) == NOTHING || Typeof(thing) != TYPE_PLAYER) {
        notify(player, tprintf("%s does not seem to exist.", name));
        return;
    }
    
    attr = atr_get(thing, A_LASTCONN);
    tim = atol(attr);
    
    if (!tim) {
        notify(player, tprintf("%s has never logged on.", db[thing].name));
    } else {
        notify(player, tprintf("%s was last logged on: %s.",
                              db[thing].name, mktm(tim, "D", player)));
    }
    
    attr = atr_get(thing, A_LASTDISC);
    tim = atol(attr);
    
    if (tim) {
        notify(player, tprintf("%s last logged off at: %s.",
                              db[thing].name, mktm(tim, "D", player)));
    }
}

/**
 * Evaluate sweep status of an object
 */
char *eval_sweep(dbref thing)
{
    char sweep_str[MAX_SWEEP_BUFFER];
    
    if (!GoodObject(thing)) {
        return stralloc("");
    }
    
    sweep_str[0] = '\0';
    
    if (Live_Player(thing))
        strncat(sweep_str, " player", sizeof(sweep_str) - strlen(sweep_str) - 1);
    if (Live_Puppet(thing))
        strncat(sweep_str, " puppet", sizeof(sweep_str) - strlen(sweep_str) - 1);
    if (Commer(thing))
        strncat(sweep_str, " commands", sizeof(sweep_str) - strlen(sweep_str) - 1);
    if (Listener(thing))
        strncat(sweep_str, " messages", sizeof(sweep_str) - strlen(sweep_str) - 1);
    
    return stralloc(sweep_str);
}

