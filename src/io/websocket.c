/* websocket.c - WebSocket integration for deMUSE
 *
 * ============================================================================
 * OVERVIEW
 * ============================================================================
 * Provides WebSocket connectivity via libwebsockets, allowing browser-based
 * clients to connect using xterm.js. Uses the lws "foreign loop" integration
 * pattern: the game's existing select() loop drives lws via lws_service_fd()
 * rather than letting lws run its own event loop.
 *
 * WebSocket connections become standard descriptor_data entries, identical
 * to telnet connections after the initial handshake. The game sees text in,
 * text out — the WebSocket framing is handled entirely by lws.
 *
 * ============================================================================
 * ARCHITECTURE
 * ============================================================================
 * - lws manages the listener socket and WebSocket protocol handling
 * - The game's select() includes lws fds via websocket_add_fds()
 * - After select(), websocket_service_fds() dispatches ready fds to lws
 * - lws calls our protocol callback for connect/receive/writeable/close
 * - The callback creates/destroys descriptors and routes data through
 *   the existing text queue system (save_command for input, queue_write
 *   for output)
 */

#ifdef USE_WEBSOCKET

#include "config.h"
#include "externs.h"
#include "io_internal.h"
#include "net.h"
#include "sock.h"
#include "websocket.h"
#include "mariadb_lockout.h"

#include <libwebsockets.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

/* ============================================================================
 * LWS FD TRACKING FOR FOREIGN LOOP
 * ============================================================================
 * lws notifies us about its file descriptors via callback. We maintain a
 * table of these fds so websocket_add_fds() can include them in the game's
 * select() call, and websocket_service_fds() can dispatch ready events.
 */

#define MAX_LWS_FDS 64

static struct lws_pollfd lws_pollfds[MAX_LWS_FDS];
static int lws_pollfd_count = 0;

/* The lws context */
static struct lws_context *ws_context = NULL;

/* Per-connection user data stored by lws */
struct ws_session {
    struct descriptor_data *d;  /* Back-pointer to our descriptor */
};

/* Forward declarations */
static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len);

/* Protocol definition */
static struct lws_protocols ws_protocols[] = {
    {
        "demuse",                       /* Protocol name */
        ws_callback,                    /* Callback */
        sizeof(struct ws_session),      /* Per-session data size */
        4096,                           /* Max rx buffer size */
        0, NULL, 0                      /* id, user, tx_packet_size */
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 }  /* terminator */
};


/* ============================================================================
 * INITIALIZATION AND SHUTDOWN
 * ============================================================================ */

int websocket_init(int port)
{
    struct lws_context_creation_info info;

    if (port <= 0) {
        return 1;  /* WebSocket disabled — not an error */
    }

    memset(&info, 0, sizeof(info));
    info.port = port;
    info.protocols = ws_protocols;
    info.gid = -1;
    info.uid = -1;

    /* Use external event loop — we drive lws from our select() */
    info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
    info.fd_limit_per_thread = MAX_LWS_FDS;

    ws_context = lws_create_context(&info);
    if (!ws_context) {
        log_error("websocket_init: Failed to create lws context");
        return 0;
    }

    /* Create the vhost (listener) */
    struct lws_context_creation_info vhost_info;
    memset(&vhost_info, 0, sizeof(vhost_info));
    vhost_info.port = port;
    vhost_info.protocols = ws_protocols;
    vhost_info.options = 0;

    if (!lws_create_vhost(ws_context, &vhost_info)) {
        log_error("websocket_init: Failed to create lws vhost");
        lws_context_destroy(ws_context);
        ws_context = NULL;
        return 0;
    }

    log_important(tprintf("WebSocket listener started on port %d", port));
    return 1;
}

void websocket_shutdown(void)
{
    if (ws_context) {
        lws_context_destroy(ws_context);
        ws_context = NULL;
        lws_pollfd_count = 0;
    }
}


/* ============================================================================
 * SELECT() LOOP INTEGRATION
 * ============================================================================ */

/* Add lws-managed fds to the game's select() fd_sets */
void websocket_add_fds(fd_set *read_set, fd_set *write_set, int *maxfd)
{
    int i;

    if (!ws_context) {
        return;
    }

    for (i = 0; i < lws_pollfd_count; i++) {
        if (lws_pollfds[i].fd < 0) {
            continue;
        }
        if (lws_pollfds[i].events & POLLIN) {
            FD_SET(lws_pollfds[i].fd, read_set);
        }
        if (lws_pollfds[i].events & POLLOUT) {
            FD_SET(lws_pollfds[i].fd, write_set);
        }
        if (lws_pollfds[i].fd + 1 > *maxfd) {
            *maxfd = lws_pollfds[i].fd + 1;
        }
    }
}

/* Dispatch ready lws fds after select() returns */
void websocket_service_fds(fd_set *read_set, fd_set *write_set)
{
    int i;
    struct lws_pollfd pfd;

    if (!ws_context) {
        return;
    }

    for (i = 0; i < lws_pollfd_count; i++) {
        if (lws_pollfds[i].fd < 0) {
            continue;
        }

        pfd = lws_pollfds[i];
        pfd.revents = 0;

        if (FD_ISSET(pfd.fd, read_set)) {
            pfd.revents |= POLLIN;
        }
        if (FD_ISSET(pfd.fd, write_set)) {
            pfd.revents |= POLLOUT;
        }

        if (pfd.revents) {
            lws_service_fd(ws_context, &pfd);
        }
    }
}

/* Handle lws internal timers (ping/pong keepalives, etc.)
 * lws 4.x asserts that pollfd != NULL in lws_service_fd(), so we use
 * lws_service_tsi() with a 0ms timeout to process pending internal work
 * (timeouts, keepalives, etc.) without blocking. */
void websocket_service_timeout(void)
{
    if (!ws_context) {
        return;
    }
    lws_service_tsi(ws_context, 0, 0);
}


/* ============================================================================
 * OUTPUT HANDLING
 * ============================================================================ */

/* Request a writable callback from lws for this descriptor */
void websocket_request_write(struct descriptor_data *d)
{
    if (d && d->wsi) {
        lws_callback_on_writable((struct lws *)d->wsi);
    }
}

/* Close a WebSocket connection from the game side (e.g., idle boot).
 * Tells lws to close the connection; lws will fire LWS_CALLBACK_CLOSED
 * which calls shutdownsock() to do the actual descriptor cleanup.
 * The caller must NOT free the descriptor — lws still owns it. */
void websocket_close_connection(struct descriptor_data *d)
{
    if (d && d->wsi && !(d->cstatus & C_CLOSING)) {
        d->cstatus |= C_CLOSING;
        lws_set_timeout((struct lws *)d->wsi,
                        PENDING_TIMEOUT_CLOSE_SEND, 1);
    }
}

/* Write queued output to a WebSocket connection.
 * Called from process_output() when d->cstatus & C_WEBSOCKET.
 * Returns 1 on success, 0 on error (connection should be closed). */
int websocket_write_output(struct descriptor_data *d)
{
    struct text_block *cur;
    unsigned char *buf;
    int total_len, offset;

    if (!d || !d->wsi) {
        return 0;
    }

    /* Calculate total output size */
    total_len = 0;
    for (cur = d->output.head; cur; cur = cur->nxt) {
        total_len += cur->nchars;
    }

    if (total_len == 0) {
        return 1;
    }

    /* Allocate buffer with LWS_PRE padding */
    buf = (unsigned char *)safe_malloc(
        (size_t)(LWS_PRE + total_len), __FILE__, __LINE__);
    if (!buf) {
        log_error("websocket_write_output: allocation failed");
        return 0;
    }

    /* Copy all queued text blocks into the buffer */
    offset = LWS_PRE;
    for (cur = d->output.head; cur; cur = cur->nxt) {
        memcpy(buf + offset, cur->start, (size_t)cur->nchars);
        offset += cur->nchars;
    }

    /* Send via lws */
    int written = lws_write((struct lws *)d->wsi,
                            buf + LWS_PRE, (size_t)total_len,
                            LWS_WRITE_TEXT);

    SMART_FREE(buf);

    if (written < 0) {
        return 0;  /* Error — caller will shutdownsock */
    }

    /* Free all consumed text blocks */
    while (d->output.head) {
        cur = d->output.head;
        d->output_size -= cur->nchars;
        d->output.head = cur->nxt;
        free_text_block(cur);
    }
    d->output.tail = &d->output.head;

    return 1;
}


/* ============================================================================
 * LWS PROTOCOL CALLBACK
 * ============================================================================
 * This is the heart of the integration. lws calls this function for all
 * protocol events on WebSocket connections.
 */

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len)
{
    struct ws_session *session = (struct ws_session *)user;

    switch (reason) {

    /* ----- fd tracking for foreign loop integration ----- */

    case LWS_CALLBACK_ADD_POLL_FD:
    {
        struct lws_pollargs *pa = (struct lws_pollargs *)in;
        if (lws_pollfd_count < MAX_LWS_FDS) {
            lws_pollfds[lws_pollfd_count].fd = pa->fd;
            lws_pollfds[lws_pollfd_count].events = (short)pa->events;
            lws_pollfds[lws_pollfd_count].revents = 0;
            lws_pollfd_count++;
        } else {
            log_error("websocket: MAX_LWS_FDS exceeded");
        }
        break;
    }

    case LWS_CALLBACK_DEL_POLL_FD:
    {
        struct lws_pollargs *pa = (struct lws_pollargs *)in;
        int i;
        for (i = 0; i < lws_pollfd_count; i++) {
            if (lws_pollfds[i].fd == pa->fd) {
                /* Swap with last entry */
                lws_pollfds[i] = lws_pollfds[lws_pollfd_count - 1];
                lws_pollfd_count--;
                break;
            }
        }
        break;
    }

    case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
    {
        struct lws_pollargs *pa = (struct lws_pollargs *)in;
        int i;
        for (i = 0; i < lws_pollfd_count; i++) {
            if (lws_pollfds[i].fd == pa->fd) {
                lws_pollfds[i].events = (short)pa->events;
                break;
            }
        }
        break;
    }

    /* ----- Connection lifecycle ----- */

    case LWS_CALLBACK_ESTABLISHED:
    {
        struct descriptor_data *d;
        struct sockaddr_in addr;
        char addr_str[50];
        char xff_buf[128];
        int fd;

        /* Get peer address (direct connection IP) */
        memset(&addr, 0, sizeof(addr));
        lws_get_peer_simple(wsi, addr_str, sizeof(addr_str));

        /* Check for X-Forwarded-For header from reverse proxy.
         * If present, use the first (leftmost) IP as the real client. */
        if (lws_hdr_copy(wsi, xff_buf, sizeof(xff_buf),
                         WSI_TOKEN_X_FORWARDED_FOR) > 0) {
            /* X-Forwarded-For may contain: "client, proxy1, proxy2"
             * Extract just the first IP (the original client) */
            char *comma = strchr(xff_buf, ',');
            if (comma) {
                *comma = '\0';
            }
            /* Trim whitespace */
            char *p = xff_buf;
            while (*p == ' ') p++;
            if (*p) {
                strncpy(addr_str, p, sizeof(addr_str) - 1);
                addr_str[sizeof(addr_str) - 1] = '\0';
            }
        }

        /* Get the raw fd for logging/identification */
        fd = lws_get_socket_fd(wsi);

        /* Create a new descriptor */
        ndescriptors++;
        SAFE_MALLOC(d, struct descriptor_data, 1);
        if (!d) {
            log_error("websocket: Failed to allocate descriptor");
            ndescriptors--;
            return -1;
        }

        /* Initialize descriptor fields */
        d->descriptor = fd;
        d->concid = make_concid();
        d->cstatus = C_WEBSOCKET;
        d->parent = NULL;
        d->state = WAITCONNECT;
        d->player = 0;
        d->output_prefix = NULL;
        d->output_suffix = NULL;
        d->output_size = 0;
        d->output.head = NULL;
        d->output.tail = &d->output.head;
        d->input.head = NULL;
        d->input.tail = &d->input.head;
        d->raw_input = NULL;
        d->raw_input_at = NULL;
        d->quota = command_burst_size;
        d->last_time = now;
        d->connected_at = now;
        d->snag_input = 0;
        d->pueblo = 0;
        d->emergency_bypass = 0;
        d->wsi = wsi;
        d->charname = NULL;
        memset(d->user, 0, sizeof(d->user));
        strncpy(d->addr, addr_str, sizeof(d->addr) - 1);
        d->addr[sizeof(d->addr) - 1] = '\0';

        /* Populate sockaddr_in from peer address string for lockout checks */
        memset(&d->address, 0, sizeof(d->address));
        d->address.sin_family = AF_INET;
        inet_aton(addr_str, &d->address.sin_addr);

        /* Link into descriptor list */
        if (descriptor_list) {
            descriptor_list->prev = &d->next;
        }
        d->next = descriptor_list;
        d->prev = &descriptor_list;
        descriptor_list = d;

        /* Store back-pointer in session */
        session->d = d;

        log_io(tprintf("|G+WS CONNECT|: concid: %ld host: %s fd: %d",
                       d->concid, addr_str, fd));

        /* Check for IP lockout */
        if (lockout_check_ip(d->address.sin_addr)) {
            send_message_text(d, welcome_lockout_msg, 0);
            /* Flush output before closing */
            if (d->output.head) {
                lws_callback_on_writable(wsi);
            }
            /* Will be cleaned up on next callback */
            return -1;
        }

        /* Send welcome message */
        welcome_user(d);

        /* Trigger initial output flush */
        if (d->output.head) {
            lws_callback_on_writable(wsi);
        }

        break;
    }

    case LWS_CALLBACK_RECEIVE:
    {
        struct descriptor_data *d = session ? session->d : NULL;
        char *text;
        char *line, *next;

        if (!d || !in || len == 0) {
            break;
        }

        /* Update activity timestamp */
        d->last_time = now;

        /* Copy to null-terminated string */
        text = (char *)safe_malloc(len + 1, __FILE__, __LINE__);
        if (!text) {
            break;
        }
        memcpy(text, in, len);
        text[len] = '\0';

        /* Split on newlines and queue each line as a command.
         * xterm.js sends \r or \r\n on Enter. */
        for (line = text; line && *line; line = next) {
            next = NULL;

            /* Find next newline */
            char *cr = strchr(line, '\r');
            char *lf = strchr(line, '\n');
            char *nl = NULL;

            if (cr && lf) {
                nl = (cr < lf) ? cr : lf;
            } else if (cr) {
                nl = cr;
            } else if (lf) {
                nl = lf;
            }

            if (nl) {
                *nl = '\0';
                next = nl + 1;
                /* Skip \n after \r in \r\n sequence */
                if (*nl == '\r' && next && *next == '\n') {
                    next++;
                }
            }

            /* Queue non-empty lines (empty lines are valid commands too) */
            save_command(d, line);
        }

        SMART_FREE(text);
        break;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE:
    {
        struct descriptor_data *d = session ? session->d : NULL;

        if (!d || !d->output.head) {
            break;
        }

        if (!websocket_write_output(d)) {
            return -1;  /* Error — lws will close the connection */
        }

        /* If more data remains, request another write callback */
        if (d->output.head) {
            lws_callback_on_writable(wsi);
        }

        break;
    }

    case LWS_CALLBACK_CLOSED:
    {
        struct descriptor_data *d = session ? session->d : NULL;

        if (d) {
            log_io(tprintf("|R+WS DISCONNECT|: concid: %ld host: %s",
                           d->concid, d->addr));

            /* Clear wsi before shutdownsock so it doesn't try to
             * close the fd — lws owns it */
            d->wsi = NULL;
            d->cstatus &= ~C_WEBSOCKET;

            /* Mark the descriptor fd as invalid so shutdownsock
             * doesn't try to shutdown/close it */
            d->descriptor = -1;

            shutdownsock(d);
            session->d = NULL;
        }
        break;
    }

    default:
        break;
    }

    return 0;
}

#endif /* USE_WEBSOCKET */
