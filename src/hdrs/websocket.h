/* websocket.h - WebSocket integration for deMUSE
 *
 * Provides WebSocket connectivity via libwebsockets, allowing browser-based
 * clients to connect using xterm.js. WebSocket connections become standard
 * descriptors in the select() loop, identical to telnet after handshake.
 *
 * Uses the same void* pattern as mariadb.h to avoid requiring
 * libwebsockets.h in every compilation unit.
 */

#ifndef _WEBSOCKET_H_
#define _WEBSOCKET_H_

#include <sys/select.h>

/* Forward declaration */
struct descriptor_data;

#ifdef USE_WEBSOCKET

int websocket_init(int port);
void websocket_shutdown(void);
void websocket_add_fds(fd_set *read_set, fd_set *write_set, int *maxfd);
void websocket_service_fds(fd_set *read_set, fd_set *write_set);
void websocket_service_timeout(void);
void websocket_request_write(struct descriptor_data *d);
int websocket_write_output(struct descriptor_data *d);

#else
/* Stubs when WebSocket not compiled in */
#define websocket_init(port) (1)
#define websocket_shutdown() ((void)0)
#define websocket_add_fds(r, w, m) ((void)0)
#define websocket_service_fds(r, w) ((void)0)
#define websocket_service_timeout() ((void)0)
#define websocket_request_write(d) ((void)0)
#define websocket_write_output(d) (1)
#endif

#endif /* _WEBSOCKET_H_ */
