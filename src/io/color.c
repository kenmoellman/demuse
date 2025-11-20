/* color.c - ANSI and Pueblo color code support
 *
 * Originally from TinyMUSE 97, modified for deMUSE to support both
 * ANSI terminal colors and Pueblo HTML client colors.
 *
 * This file handles color markup in the format:
 *   |<codes>+<text>|
 * Example: |RB+Hello World!| (red text, blue background)
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "config.h"
#include "db.h"
#include "externs.h"
#include "interface.h"

/* ===================================================================
 * Color Attribute Flags
 * =================================================================== */

#define CA_BRIGHT    1
#define CA_REVERSE   2
#define CA_UNDERLINE 4
#ifdef BLINK
#define CA_BLINK     8
#endif

/* ===================================================================
 * ANSI Color Strings
 * =================================================================== */

/* Use \033 instead of \e for ISO C compliance */
static const char *normal_ansi = "\033[0m";

#ifdef PUEBLO_CLIENT
static const char *normal_pueblo = "<font fgcolor=\"FFFFFF\" bgcolor=\"000000\">";
#endif

/* ===================================================================
 * Forward Declarations
 * =================================================================== */

static int color2num(char c);
static int is_foreground(int num);
static int is_background(int num);
static void set_ca(int *attribs, int num);
static char *make_num_string(int fore, int back, int ca);
static char *color_escape(const char *s);

#ifdef PUEBLO_CLIENT
static char *make_font_string(const char *fore, const char *back, int ca);
static char *color_pueblo(const char *s);
static const char *pueblo_color(int num);
#endif

/* ===================================================================
 * Utility Functions
 * =================================================================== */

/**
 * Convert color code character to ANSI number
 * @param c Color code character
 * @return ANSI color code number, or -1 if invalid
 */
static int color2num(char c)
{
    switch (c) {
        case '!': return 1;  /* Bright */
        case 'u': return 4;  /* Underline */
#ifdef BLINK
        case 'b': return 5;  /* Blink */
#endif
        case 'r': return 7;  /* Reverse */
        case 'N': return 30; /* Black foreground */
        case 'R': return 31; /* Red foreground */
        case 'G': return 32; /* Green foreground */
        case 'Y': return 33; /* Yellow foreground */
        case 'B': return 34; /* Blue foreground */
        case 'M': return 35; /* Magenta foreground */
        case 'C': return 36; /* Cyan foreground */
        case 'W': return 37; /* White foreground */
        case '0': return 40; /* Black background */
        case '1': return 41; /* Red background */
        case '2': return 42; /* Green background */
        case '3': return 43; /* Yellow background */
        case '4': return 44; /* Blue background */
        case '5': return 45; /* Magenta background */
        case '6': return 46; /* Cyan background */
        case '7': return 47; /* White background */
        default:  return -1;
    }
}

/**
 * Check if number is a foreground color code
 */
static int is_foreground(int num)
{
    return (num >= 30 && num <= 37);
}

/**
 * Check if number is a background color code
 */
static int is_background(int num)
{
    return (num >= 40 && num <= 47);
}

/**
 * Set color attribute flags based on ANSI code
 */
static void set_ca(int *attribs, int num)
{
    switch (num) {
        case 1: *attribs |= CA_BRIGHT; break;
        case 7: *attribs |= CA_REVERSE; break;
        case 4: *attribs |= CA_UNDERLINE; break;
#ifdef BLINK
        case 5: *attribs |= CA_BLINK; break;
#endif
        default:
            log_error("Invalid attribute number in set_ca");
            break;
    }
}

/**
 * Build ANSI escape code string from color values
 * @param fore Foreground color code (or 0 for none)
 * @param back Background color code (or 0 for none)
 * @param ca Color attributes bitmap
 * @return Allocated string with semicolon-separated codes
 */
static char *make_num_string(int fore, int back, int ca)
{
    char buff[256];
    size_t pos = 0;

    buff[0] = '\0';

    if (fore > 0) {
        pos += snprintf(buff + pos, sizeof(buff) - pos, "%d;", fore);
    }

    if (back > 0) {
        pos += snprintf(buff + pos, sizeof(buff) - pos, "%d;", back);
    }

    if (ca > 0) {
        if (ca & CA_BRIGHT)
            pos += snprintf(buff + pos, sizeof(buff) - pos, "1;");
        if (ca & CA_REVERSE)
            pos += snprintf(buff + pos, sizeof(buff) - pos, "7;");
        if (ca & CA_UNDERLINE)
            pos += snprintf(buff + pos, sizeof(buff) - pos, "4;");
#ifdef BLINK
        if (ca & CA_BLINK)
            pos += snprintf(buff + pos, sizeof(buff) - pos, "5;");
#endif
    }

    /* Remove trailing semicolon */
    if (pos > 0 && buff[pos - 1] == ';') {
        buff[pos - 1] = '\0';
    }

    return stralloc(buff);
}

/**
 * Convert color code string to ANSI escape sequence
 * @param s String of color codes (e.g., "RB" for red on blue)
 * @return Allocated ANSI escape string
 */
static char *color_escape(const char *s)
{
    char escape[256];
    int foreground = 37; /* Default to white */
    int background = 40; /* Default to black */
    int attribs = 0;
    int valid = 0;

    for (; *s; s++) {
        int num = color2num(*s);
        if (num > 0) {
            valid = 1;
            if (is_foreground(num)) {
                foreground = num;
            } else if (is_background(num)) {
                background = num;
            } else {
                set_ca(&attribs, num);
            }
        }
    }

    if (valid) {
        /* Prevent same color for foreground and background */
        if (foreground == (background - 10)) {
            if (foreground == 30 && !(attribs & CA_BRIGHT)) {
                background = 47; /* White background */
            } else {
                background = 40; /* Black background */
            }
        }

        char *num_str = make_num_string(foreground, background, attribs);
        snprintf(escape, sizeof(escape), "\033[%sm", num_str);
        SMART_FREE(num_str); 
    } else {
        escape[0] = '\0';
    }

    return stralloc(escape);
}

/**
 * Remove beep characters from string
 * @param str Input string
 * @return Allocated string without beeps
 */
char *strip_beep(char *str)
{
    char buf[BUFFER_LEN];
    char *p = buf;
    const char *s = str;

    while (*s && (p - buf) < (BUFFER_LEN - 1)) {
        if (*s != '\a') {
            *p++ = *s;
        }
        s++;
    }
    *p = '\0';

    return stralloc(buf);
}

/* ===================================================================
 * Main Color Processing Functions
 * =================================================================== */

/**
 * Process color markup in string
 * @param str Input string with |code+text| markup
 * @param strip 1 to strip colors, 0 to convert to ANSI/Pueblo
 * @param pueblo 1 for Pueblo HTML output, 0 for ANSI
 * @return Allocated string with colors processed
 */
char *colorize(const char *str, int strip, int pueblo)
{
    char buf[65535];
    char *s, *colorend, *colorstr;
    size_t pos = 0;

    if (!str || pos >= sizeof(buf) - 1) {
        return stralloc("");
    }

    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (s = buf; *s && pos < sizeof(buf) - 1; s++) {
        if (*s != '|') {
            continue;
        }

        /* Look for color markup: |codes+text| */
        colorstr = strpbrk(s + 1, "+|");
        if (!colorstr || *colorstr == '|') {
            continue;
        }

        /* Handle curly braces: |codes+{text with | bars}| */
        if (*(colorstr + 1) == '{') {
            char *end_curl = strchr(colorstr + 2, '}');
            if (end_curl && *(end_curl + 1) == '|') {
                colorend = end_curl + 1;
            } else {
                colorend = strchr(colorstr + 1, '|');
            }
        } else {
            colorend = strchr(colorstr + 1, '|');
        }

        if (!colorend || !*colorend) {
            continue;
        }

        /* Split the markup into pieces */
        char buf2[65535];
        *s++ = '\0';
        *colorstr++ = '\0';
        *colorend++ = '\0';

        /* Copy everything before the color code */
        strncpy(buf2, buf, sizeof(buf2) - 1);
        buf2[sizeof(buf2) - 1] = '\0';

        if (!strip) {
#ifdef PUEBLO_CLIENT
            char *markup;
            if (!pueblo) {
                markup = color_escape(s);
            } else {
                markup = color_pueblo(s);
            }
            strncat(buf2, markup, sizeof(buf2) - strlen(buf2) - 1);
            SMART_FREE(markup);
            
            strncat(buf2, colorstr, sizeof(buf2) - strlen(buf2) - 1);
            
            if (!pueblo) {
                strncat(buf2, normal_ansi, sizeof(buf2) - strlen(buf2) - 1);
            } else {
                strncat(buf2, normal_pueblo, sizeof(buf2) - strlen(buf2) - 1);
            }
#else
            char *escape = color_escape(s);
            strncat(buf2, escape, sizeof(buf2) - strlen(buf2) - 1);
            SMART_FREE(escape); 
            strncat(buf2, colorstr, sizeof(buf2) - strlen(buf2) - 1);
            strncat(buf2, normal_ansi, sizeof(buf2) - strlen(buf2) - 1);
#endif
        } else {
            /* Just append the text without color codes */
            strncat(buf2, colorstr, sizeof(buf2) - strlen(buf2) - 1);
        }

        strncat(buf2, colorend, sizeof(buf2) - strlen(buf2) - 1);
        strncpy(buf, buf2, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        
        /* Adjust pointer position */
        s = buf + strlen(buf2) - strlen(colorend) - 1;
    }

    return stralloc(buf);
}

/**
 * Truncate string to specific length, preserving color codes
 * @param str Input string with color codes
 * @param num Maximum visible characters (not counting codes)
 * @return Allocated truncated string
 */
char *truncate_color(char *str, int num)
{
    char buf[BUFFER_LEN];
    char *s;
    int count = 0;

    if (!str) {
        return stralloc("");
    }

    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (s = buf; *s && count < num; s++) {
        if (*s == '|') {
            char *color = strchr(s, '+');
            if (!color) {
                continue;
            }

            if (*(color + 1) == '{') {
                char *end_curl = strchr(color + 2, '}');
                if (end_curl && *(end_curl + 1) == '|') {
                    /* Count characters inside braces */
                    for (s = color + 2; *s != '}' && count < num; s++, count++)
                        ;
                    if (count >= num) {
                        *s++ = '}';
                        *s = '|';
                    } else {
                        s++;
                    }
                } else if (strchr(color + 1, '|')) {
                    for (s = color + 1; *s != '|' && count < num; s++, count++)
                        ;
                    if (count >= num) {
                        *s = '|';
                    }
                }
            } else if (strchr(color + 1, '|')) {
                for (s = color + 1; *s != '|' && count < num; s++, count++)
                    ;
                if (count >= num) {
                    *s = '|';
                }
            }
        } else {
            count++;
        }
    }

    *s = '\0';
    return stralloc(buf);
}

/* ===================================================================
 * Public API Functions
 * =================================================================== */

/**
 * Strip all color codes from string
 */
char *strip_color(const char *str)
{
    return colorize(str, 1, 0);
}

/**
 * Strip color codes and beep characters
 */
char *strip_color_nobeep(char *str)
{
    char *temp = strip_beep(str);
    char *result = colorize(temp, 1, 0);
    SMART_FREE(temp);
    return result;
}

/**
 * Parse color codes for display
 * @param str Input string
 * @param pueblo 1 for Pueblo/HTML, 0 for ANSI
 */
char *parse_color(const char *str, int pueblo)
{
    return colorize(str, 0, pueblo);
}

/**
 * Parse color codes with beep stripping
 */
char *parse_color_nobeep(char *str, int pueblo)
{
    char *temp = strip_beep(str);
    char *result = colorize(temp, 0, pueblo);
    SMART_FREE(temp);
    return result;
}

/* ===================================================================
 * Pueblo Client Support
 * =================================================================== */

#ifdef PUEBLO_CLIENT

/**
 * Convert HTML special characters for Pueblo
 */
char *html_conversion(dbref player, char *oldmsg)
{
    char buildmsg[BUFFER_LEN];
    size_t pos = 0;
    
    /* Suppress unused parameter warning - kept for API compatibility */
    (void)player;
    
    buildmsg[0] = '\0';

    /* Check if player has Pueblo enabled - would need descriptor lookup */
    /* For now, always convert */
    for (size_t i = 0; oldmsg[i] && pos < sizeof(buildmsg) - 6; i++) {
        switch (oldmsg[i]) {
            case '"':
                pos += snprintf(buildmsg + pos, sizeof(buildmsg) - pos, "&quot;");
                break;
            case '&':
                pos += snprintf(buildmsg + pos, sizeof(buildmsg) - pos, "&amp;");
                break;
            case '<':
                pos += snprintf(buildmsg + pos, sizeof(buildmsg) - pos, "&lt;");
                break;
            case '>':
                pos += snprintf(buildmsg + pos, sizeof(buildmsg) - pos, "&gt;");
                break;
            default:
                buildmsg[pos++] = oldmsg[i];
                buildmsg[pos] = '\0';
                break;
        }
    }

    return stralloc(buildmsg);
}

/**
 * Convert exit name to clickable Pueblo link
 */
char *html_exit(dbref player, char *exit_name)
{
    char newname[BUFFER_LEN];
    char name[BUFFER_LEN];
    char alias[BUFFER_LEN];
    int flag = 0;

    name[0] = '\0';
    alias[0] = '\0';

    /* Parse exit name and aliases separated by semicolons */
    for (size_t i = 0; exit_name[i]; i++) {
        if (flag == 0 && exit_name[i] != ';') {
            size_t len = strlen(name);
            if (len < sizeof(name) - 1) {
                name[len] = exit_name[i];
                name[len + 1] = '\0';
            }
        } else if (exit_name[i] == ';') {
            flag = (flag == 1) ? 2 : 1;
        } else if (flag == 1) {
            size_t len = strlen(alias);
            if (len < sizeof(alias) - 1) {
                alias[len] = exit_name[i];
                alias[len + 1] = '\0';
            }
        }
    }

    if (*alias) {
        char *converted = html_conversion(player, name);
        snprintf(newname, sizeof(newname), 
                "<a xch_cmd=\"%s\">%s</a>", alias, converted);
        SMART_FREE(converted);
    } else {
        char *converted = html_conversion(player, name);
        snprintf(newname, sizeof(newname), "%s", converted);
        SMART_FREE(converted);
    }

    return stralloc(newname);
}

/**
 * Remove HTML tags from message
 */
char *html_remove(dbref player, char *msg)
{
    char buildmsg[BUFFER_LEN];
    int flag = 0;
    size_t pos = 0;

    /* Suppress unused parameter warning - kept for API compatibility */
    (void)player;

    buildmsg[0] = '\0';

    /* Check if Pueblo is enabled - for now assume we need to filter */
    for (size_t i = 0; msg[i] && pos < sizeof(buildmsg) - 1; i++) {
        if (flag == 0 && msg[i] != '<' && msg[i] != '>') {
            buildmsg[pos++] = msg[i];
            buildmsg[pos] = '\0';
        } else if (msg[i] == '<') {
            flag = 1;
        } else if (msg[i] == '>' && flag == 1) {
            flag = 0;
        }
    }

    return stralloc(buildmsg);
}

/**
 * Convert color code to Pueblo color name
 */
static const char *pueblo_color(int num)
{
    switch (num) {
        case 4:  return "underline";
#ifdef BLINK
        case 5:  return "blink";
#endif
        case 30:
        case 40: return "black";
        case 31:
        case 41: return "red";
        case 32:
        case 42: return "green";
        case 33:
        case 43: return "yellow";
        case 34:
        case 44: return "blue";
        case 35:
        case 45: return "magenta";
        case 36:
        case 46: return "cyan";
        case 37:
        case 47: return "white";
        default: return NULL;
    }
}

/**
 * Build Pueblo font tag string
 */
static char *make_font_string(const char *fore, const char *back, int ca)
{
    char buff[512];
    size_t pos = 0;

    buff[0] = '\0';

    if (fore) {
        pos += snprintf(buff + pos, sizeof(buff) - pos, "fgcolor=\"%s\" ", fore);
    }

    if (back) {
        pos += snprintf(buff + pos, sizeof(buff) - pos, "bgcolor=\"%s\" ", back);
    }

    if (ca > 0) {
        if (ca & CA_UNDERLINE) {
            pos += snprintf(buff + pos, sizeof(buff) - pos, "style=\"text-decoration:underline\" ");
        }
    }

    return stralloc(buff);
}

/**
 * Convert color codes to Pueblo HTML tags
 */
static char *color_pueblo(const char *s)
{
    char escape[512];
    const char *foreground = "FFFFFF";
    const char *background = "000000";
    int attribs = 0;
    int valid = 0;

    for (; *s; s++) {
        int num = color2num(*s);
        if (num > 0) {
            valid = 1;
            if (is_foreground(num)) {
                foreground = pueblo_color(num);
            } else if (is_background(num)) {
                background = pueblo_color(num);
            } else {
                set_ca(&attribs, num);
            }
        }
    }

    if (valid) {
        char *font_str = make_font_string(foreground, background, attribs);
        snprintf(escape, sizeof(escape), "<font %s>", font_str);
        SMART_FREE(font_str);
    } else {
        escape[0] = '\0';
    }

    return stralloc(escape);
}

#endif /* PUEBLO_CLIENT */
