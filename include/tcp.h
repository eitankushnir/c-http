#ifndef TCP_H_
#define TCP_H_

#include <stddef.h>
#include <stdint.h>
typedef int TCPSocket;

typedef enum {
  TCP_SUCCESS = 0,
  TCP_FAIL,
} TCPResult;

TCPResult tcp_socket_create(TCPSocket *out_sock);
TCPResult tcp_socket_bind(TCPSocket sock, uint16_t port);
TCPResult tcp_socket_listen(TCPSocket sock, int backlog);
TCPResult tcp_socket_accept(TCPSocket sock, TCPSocket *out_conn);
TCPResult tcp_socket_connect(TCPSocket sock, const char *ip, uint16_t port);
void tcp_socket_close(TCPSocket sock);

#endif
