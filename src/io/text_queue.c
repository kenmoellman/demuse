/* text_queue.c - Text block and queue management for network I/O
 * Extracted from bsd.c during modernization
 */

#include "config.h"
#include "externs.h"
#include "net.h"
#include <string.h>
#include <stdlib.h>

/* Global statistics */
size_t text_block_size = 0;
size_t text_block_num = 0;

/* Create a new text block with the given data */
struct text_block *make_text_block(const char *s, int n)
{
    struct text_block *p;

    if (!s || n <= 0) {
        return NULL;
    }

//    p = malloc(sizeof(struct text_block));
    SAFE_MALLOC(p, struct text_block, 1);
    if (!p) {
        log_error("Failed to allocate text_block structure");
        return NULL;
    }

//    p->buf = malloc(n);
    p->buf = safe_malloc(n, __FILE__, __LINE__);
    if (!p->buf) {
        log_error("Failed to allocate text_block buffer");
        SMART_FREE(p);
        return NULL;
    }

    memcpy(p->buf, s, n);
    p->nchars = n;
    p->start = p->buf;
    p->nxt = NULL;
    
    text_block_size += n;
    text_block_num++;
    
    return p;
}

/* Free a text block */
void free_text_block(struct text_block *t)
{
    if (!t) {
        return;
    }

    if (t->buf) {
        text_block_size -= t->nchars;
        SMART_FREE(t->buf);
    }
    
    SMART_FREE(t);
    text_block_num--;
}

/* Add data to a text queue */
//static void add_to_queue(struct text_queue *q, const char *b, int n)
void add_to_queue(struct text_queue *q, const char *b, int n)
{
    struct text_block *p;

    if (!q || !b || n == 0) {
        return;
    }

    p = make_text_block(b, n);
    if (!p) {
        return;
    }

    p->nxt = NULL;
    *q->tail = p;
    q->tail = &p->nxt;
}

/* Internal flush implementation */
static int flush_queue_int(struct text_queue *q, int n, int pueblo)
{
    struct text_block *p;
    int really_flushed = 0;

    if (!q) {
        return 0;
    }

    n += strlen(FLUSHED_MESSAGE);

    while (n > 0 && (p = q->head)) {
        n -= p->nchars;
        really_flushed += p->nchars;
        q->head = p->nxt;
        free_text_block(p);
    }

    /* Add flush message */
    p = make_text_block(FLUSHED_MESSAGE, strlen(FLUSHED_MESSAGE));
    if (p) {
        p->nxt = q->head;
        q->head = p;
        if (!p->nxt) {
            q->tail = &p->nxt;
        }
        really_flushed -= p->nchars;
    }

    return really_flushed;
}

/* Flush queue (non-Pueblo version) */
static int flush_queue(struct text_queue *q, int n)
{
    return flush_queue_int(q, n, 0);
}

/* Flush queue (Pueblo version) */
static int flush_queue_pueblo(struct text_queue *q, int n)
{
    return flush_queue_int(q, n, 1);
}

/* Write data to a descriptor's output queue */
int queue_write(struct descriptor_data *d, const char *b, int n)
{
    int space;

    if (!d || !b || n <= 0) {
        return 0;
    }

#ifdef USE_CID_PLAY
    if (d->cstatus & C_REMOTE) {
        need_more_proc = 1;
    }
#endif

    if (d->pueblo == 0) {
        space = max_output - d->output_size - n;
        if (space < 0) {
            d->output_size -= flush_queue(&d->output, -space);
        }
    } else {
        space = max_output_pueblo - d->output_size - n;
        if (space < 0) {
            d->output_size -= flush_queue_pueblo(&d->output, -space);
        }
    }

    add_to_queue(&d->output, b, n);
    d->output_size += n;
    
    return n;
}

/* Queue a string for output */
int queue_string(struct descriptor_data *d, const char *s)
{
    if (!d || !s) {
        return 0;
    }
    return queue_write(d, s, strlen(s));
}

///* Free all queued data for a descriptor */
//void freeqs(struct descriptor_data *d)
//{
//    struct text_block *cur, *next;
//
//    if (!d) {
//        return;
//    }
//
//    /* Free output queue */
//    cur = d->output.head;
//    while (cur) {
//        next = cur->nxt;
//        free_text_block(cur);
//        cur = next;
//    }
//    d->output.head = NULL;
//    d->output.tail = &d->output.head;
//
//    /* Free input queue */
//    cur = d->input.head;
//    while (cur) {
//        next = cur->nxt;
//        free_text_block(cur);
//        cur = next;
//    }
//    d->input.head = NULL;
//    d->input.tail = &d->input.head;
//
//    /* Free raw input buffer */
//    if (d->raw_input) {
//        SMART_FREE(d->raw_input);
//        d->raw_input = NULL;
//    }
//    d->raw_input_at = NULL;
//}
