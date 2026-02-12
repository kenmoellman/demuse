/* output_handler.c - Output and notification handling
 * Extracted from bsd.c during modernization
 */

#include "config.h"
#include "externs.h"
#include "io_internal.h"
#include <errno.h>
#include <unistd.h>

/* Helper function for safe string operations */
void safe_string_copy(char *dest, const char *src, size_t dest_size)
{
    if (!dest || !src || dest_size == 0) {
        return;
    }
    
    size_t len = strlen(src);
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

int safe_string_cat(char *dest, const char *src, size_t dest_size)
{
    if (!dest || !src || dest_size == 0) {
        return -1;
    }
    
    size_t dest_len = strlen(dest);
    if (dest_len >= dest_size - 1) {
        return -1;  /* Already full */
    }
    
    size_t src_len = strlen(src);
    size_t available = dest_size - dest_len - 1;
    
    if (src_len > available) {
        memcpy(dest + dest_len, src, available);
        dest[dest_size - 1] = '\0';
        return -1;  /* Truncation occurred */
    }
    
    memcpy(dest + dest_len, src, src_len + 1);
    return 0;
}

/* Get short name (alias if shorter than regular name) */
char *short_name(dbref obj)
{
    const char *alias;
    size_t alias_len, name_len;

    if (!GoodObject(obj)) {
        return "?";
    }

    alias = atr_get(obj, A_ALIAS);
    if (!alias || !*alias) {
        return db[obj].name;
    }

    alias_len = strlen(alias);
    name_len = strlen(db[obj].name);

    return (alias_len > 0 && alias_len < name_len) ? 
           (char *)alias : db[obj].name;
}

/* Parse and format output with color codes */
char *format_player_output(dbref player, int color, const char *msg, int pueblo)
{
    char buffer[IO_BUFFER_SIZE];
    
    if (!msg) {
        return stralloc("");
    }
    
    /* Check buffer size */
    if (strlen(msg) >= sizeof(buffer)) {
        log_error("Message too long in format_player_output");
        safe_string_copy(buffer, msg, sizeof(buffer));
    } else {
        safe_string_copy(buffer, msg, sizeof(buffer));
    }
    
    if (!color) {
        return stralloc(buffer);
    }
    
    /* Apply color parsing based on player preferences */
    if (!GoodObject(player)) {
        return stralloc(buffer);
    }
    if (db[player].flags & PLAYER_NOBEEP) {
        if (db[player].flags & PLAYER_ANSI) {
            return stralloc(parse_color_nobeep(buffer, pueblo));
        } else {
            return stralloc(strip_color_nobeep(buffer));
        }
    } else {
        if (db[player].flags & PLAYER_ANSI) {
            return stralloc(parse_color(buffer, pueblo));
        } else {
            return stralloc(strip_color(buffer));
        }
    }
}

/* Add prefix and suffix to output */
char *add_pre_suf(dbref player, int color, char *msg, int pueblo)
{
    extern dbref as_from;
    char buf[IO_BUFFER_SIZE];
    char buf1[IO_BUFFER_SIZE];
    char buf2[IO_BUFFER_SIZE];
    char buf0[IO_BUFFER_SIZE];
    char *prefix;
    char *suffix;

    if (!msg) {
        return stralloc("");
    }

    if (!GoodObject(player)) {
        return stralloc(msg);
    }

    /* Check if player is actually connected */
    if (!(db[player].flags & CONNECT) && player != as_from) {
        return stralloc(msg);
    }

    /* Get and process prefix */
    pronoun_substitute(buf1, player, 
                      stralloc(atr_get(player, A_PREFIX)), player);
    prefix = buf1 + strlen(db[player].name) + 1;

    /* Get and process suffix */
    pronoun_substitute(buf2, player, 
                      stralloc(atr_get(player, A_SUFFIX)), player);
    suffix = buf2 + strlen(db[player].name) + 1;

    /* Format the main message */
    safe_string_copy(buf0, format_player_output(player, color, msg, pueblo),
                    sizeof(buf0));

    /* Add prefix if present */
    if (prefix && *prefix) {
        safe_string_copy(buf, 
                        format_player_output(player, color, prefix, pueblo),
                        sizeof(buf));
        safe_string_cat(buf, " ", sizeof(buf));
        if (safe_string_cat(buf, buf0, sizeof(buf)) < 0) {
            log_error("Buffer overflow prevented in add_pre_suf (prefix)");
        }
        safe_string_copy(buf0, buf, sizeof(buf0));
    }

    /* Add suffix if present */
    if (suffix && *suffix) {
        safe_string_cat(buf0, " ", sizeof(buf0));
        if (safe_string_cat(buf0, 
                format_player_output(player, color, suffix, pueblo),
                sizeof(buf0)) < 0) {
            log_error("Buffer overflow prevented in add_pre_suf (suffix)");
        }
    }

    return stralloc(buf0);
}

/* Internal notification function */
void raw_notify_internal(dbref player, char *msg, int color)
{
    struct descriptor_data *d;
    extern dbref speaker, as_from, as_to;
    char buf0[IO_BUFFER_SIZE];
    char ansi[ANSI_BUFFER_SIZE];
    char *html = NULL;
    char *temp;

    if (!msg) {
        return;
    }

    if (!GoodObject(player)) {
        return;
    }

    /* Check for obsolete WHEN flag */
    if (db[player].flags & PLAYER_WHEN) {
        db[player].flags &= ~PLAYER_WHEN;
        notify(player, "The WHEN flag is now obsolete. It has been removed. "
                      "See \"help WHEN\" for more information.");
    }

    /* Build message with speaker info if puppet */
    if (IS(player, TYPE_PLAYER, PUPPET)) {
        if (speaker != player) {
            temp = tprintf(" [#%" DBREF_FMT "/%s]", speaker,
                          short_name(real_owner(db[speaker].owner)));
        } else {
            temp = tprintf("");
        }
        
        size_t msg_len = strlen(msg);
        size_t temp_len = strlen(temp);
        if (msg_len + temp_len >= sizeof(buf0)) {
            safe_string_copy(buf0, msg, sizeof(buf0) - temp_len);
        } else {
            safe_string_copy(buf0, msg, sizeof(buf0));
        }
        safe_string_cat(buf0, temp, sizeof(buf0));
    } else {
        safe_string_copy(buf0, msg, sizeof(buf0));
    }

    /* Format ANSI output */
    safe_string_copy(ansi, add_pre_suf(player, color, msg, 0),
                    sizeof(ansi));

    /* Allocate and format HTML output if needed */
    SAFE_MALLOC(html, char, HTML_BUFFER_SIZE);
    if (!html) {
        log_error("Failed to allocate HTML buffer");
        return;
    }
    safe_string_copy(html, add_pre_suf(player, color, msg, 1),
                    HTML_BUFFER_SIZE);

    /* Handle as_from/as_to redirection */
    if (player == as_from) {
        player = as_to;
    }

    /* Send to all connected descriptors for this player */
    for (d = descriptor_list; d; d = d->next) {
        if (d->state == CONNECTED && d->player == player) {
            /* Check blacklist restrictions */
            if (((!strlen(atr_get(real_owner(d->player), A_BLACKLIST))) &&
                 (!strlen(atr_get(real_owner(player), A_BLACKLIST)))) ||
                !((could_doit(real_owner(player), real_owner(d->player),
                              A_BLACKLIST)) &&
                  (could_doit(real_owner(d->player), real_owner(player),
                              A_BLACKLIST)))) {
                if (!d->pueblo) {
                    queue_string(d, ansi);
                    queue_write(d, "\n", 1);
                } else {
                    queue_string(d, html);
                    queue_write(d, "\n", 1);
                }
            }
        }
    }

    if (html) {
        SMART_FREE(html);
    }
}

/* Public notification functions (maintain API compatibility) */
void raw_notify(dbref player, char *msg)
{
    raw_notify_internal(player, msg, 1);
}

void raw_notify_noc(dbref player, char *msg)
{
    raw_notify_internal(player, msg, 0);
}

/* Process output for a descriptor */
int process_output(struct descriptor_data *d)
{
    struct text_block **qp, *cur;
    int cnt;

    if (!d) {
        return 0;
    }

#ifdef USE_CID_PLAY
    if (d->cstatus & C_REMOTE) {
        char buf[10];
        char obuf[IO_BUFFER_SIZE];
        int buflen;
        int k, j;

        snprintf(buf, sizeof(buf), "%ld ", d->concid);
        buflen = strlen(buf);
        memcpy(obuf, buf, buflen);
        j = buflen;

        for (qp = &d->output.head; (cur = *qp);) {
            need_more_proc = 1;
            
            for (k = 0; k < cur->nchars && j < (int)sizeof(obuf) - 1; k++) {
                obuf[j++] = cur->start[k];
                if (cur->start[k] == '\n') {
                    if (d->parent) {
                        queue_write(d->parent, obuf, j);
                    }
                    if (buflen < (int)sizeof(obuf)) {
                        memcpy(obuf, buf, buflen);
                        j = buflen;
                    } else {
                        log_error("Buffer overflow prevented in process_output");
                        j = 0;
                    }
                }
            }
            
            d->output_size -= cur->nchars;
            if (!cur->nxt) {
                d->output.tail = qp;
            }
            *qp = cur->nxt;
            free_text_block(cur);
        }
        
        if (j > buflen && j < (int)sizeof(obuf)) {
            queue_write(d, obuf + buflen, j - buflen);
        }
        return 1;
    }
#endif

    /* Normal (non-remote) output processing */
    for (qp = &d->output.head; (cur = *qp);) {
        cnt = write(d->descriptor, cur->start, cur->nchars);
        if (cnt < 0) {
            if (errno == EWOULDBLOCK) {
                return 1;
            }
            return 0;
        }
        
        d->output_size -= cnt;
        if (cnt == cur->nchars) {
            if (!cur->nxt) {
                d->output.tail = qp;
            }
            *qp = cur->nxt;
            free_text_block(cur);
            continue;
        }
        
        cur->nchars -= cnt;
        cur->start += cnt;
        break;
    }
    
    return 1;
}

/* Flush all output for all descriptors */
void flush_all_output(void)
{
    struct descriptor_data *d;

    for (d = descriptor_list; d; d = d->next) {
        process_output(d);
    }
}
