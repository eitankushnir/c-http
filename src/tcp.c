#include "tcp.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

TCPResult tcp_socket_create(TCPSocket *out_sock) {
  if (!out_sock)
    return TCP_FAIL;

  int res = socket(AF_INET, SOCK_STREAM, 0);
  if (res < 0)
    return TCP_FAIL;

  *out_sock = (TCPSocket)res;
  return TCP_SUCCESS;
}

TCPResult tcp_socket_bind(TCPSocket sock, uint16_t port) {
  int opt = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
    return TCP_FAIL;
  }

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  int res = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
  return res == 0 ? TCP_SUCCESS : TCP_FAIL;
}

TCPResult tcp_socket_listen(TCPSocket sock, int backlog) {
  return listen(sock, backlog) == 0 ? TCP_SUCCESS : TCP_FAIL;
}

TCPResult tcp_socket_accept(TCPSocket sock, TCPSocket *out_conn) {
  TCPSocket connection = accept(sock, NULL, NULL);
  if (connection < 0)
    return TCP_FAIL;

  *out_conn = connection;
  return TCP_SUCCESS;
}

TCPResult tcp_socket_connect(TCPSocket sock, const char *ip, uint16_t port) {
  struct sockaddr_in server_addr = {0};
  server_addr.sin_port = htons(port);
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(ip);

  return connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0 ? TCP_SUCCESS : TCP_FAIL;
}

void tcp_socket_close(TCPSocket sock) {
  shutdown(sock, SHUT_RDWR);
  close(sock);
}
