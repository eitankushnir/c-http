#define STRING_VIEW_IMPLEMENTATION
#define COROUTINE_IMPLEMENTATION

#include "http.h"
#include "coroutine.h"
#include "string_view.h"
#include "tcp.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

HttpServer *http_server_create(void) {
  HttpServer *server = calloc(1, sizeof(HttpServer));
  if (!server)
    return NULL;

  TCPResult res = tcp_socket_create(&server->listen_sock);
  if (res != TCP_SUCCESS) {
    free(server);
    return NULL;
  }

  make_nonblocking(server->listen_sock);
  return server;
}

static bool sv_to_method_(StringView sv, HttpMethod *out) {
  if (sv_eq(sv, SV_LIT("GET"))) {
    *out = HTTP_GET;
    return true;
  }
  if (sv_eq(sv, SV_LIT("POST"))) {
    *out = HTTP_POST;
    return true;
  }
  if (sv_eq(sv, SV_LIT("PUT"))) {
    *out = HTTP_PUT;
    return true;
  }
  if (sv_eq(sv, SV_LIT("PATCH"))) {
    *out = HTTP_PATCH;
    return true;
  }
  if (sv_eq(sv, SV_LIT("DELETE"))) {
    *out = HTTP_DELETE;
    return true;
  }
  if (sv_eq(sv, SV_LIT("HEAD"))) {
    *out = HTTP_HEAD;
    return true;
  }
  if (sv_eq(sv, SV_LIT("OPTIONS"))) {
    *out = HTTP_OPTIONS;
    return true;
  }

  return false;
}

bool sv_to_http_ver_(StringView sv, HttpVersion *out) {
  if (sv_eq(sv, SV_LIT("HTTP/1.0"))) {
    *out = HTTP_1_0;
    return true;
  }
  if (sv_eq(sv, SV_LIT("HTTP/1.1"))) {
    *out = HTTP_1_1;
    return true;
  }
  return false;
}

StringView http_ver_to_sv_(HttpVersion ver) {
  switch (ver) {
  case HTTP_1_0:
    return SV_LIT("HTTP/1.0");
  case HTTP_1_1:
    return SV_LIT("HTTP/1.1");
  }

  return SV_LIT("Unknown");
}

StringView http_status_to_sv_(HttpStatus status) {
  switch (status) {
  case HTTP_OK:
    return SV_LIT("OK");
  case HTTP_CREATED:
    return SV_LIT("Created");
  case HTTP_NO_CONTENT:
    return SV_LIT("No Content");

  case HTTP_BAD_REQUEST:
    return SV_LIT("Bad Request");
  case HTTP_NOT_FOUND:
    return SV_LIT("Not Found");
  case HTTP_METHOD_NOT_ALLOWED:
    return SV_LIT("Method Not Allowed");
  case HTTP_REQUEST_TIMEOUT:
    return SV_LIT("Request Timeout");
  case HTTP_CONTENT_TOO_LARGE:
    return SV_LIT("Content Too Large");

  case HTTP_INTERNAL_SERVER_ERROR:
    return SV_LIT("Internal Server Error");
  }

  return SV_LIT("Unknown");
}

static void free_req_(HttpRequest *req) {
  free(req->headers);
  free(req->queries);
  free(req);
}

static StringView sock_read_crlf_(Coroutine *caller, HttpConnection *conn, HttpReqParseResult *out_result) {
  while (1) {
    if (conn->request_len >= HTTP_REQUEST_SIZE_LIMIT) {
      *out_result = HTTP_REQ_TOO_LARGE;
      return SV_EMPTY;
    }

    StringView buf_view = {.data = conn->request_buf + conn->request_parser_pos, .length = conn->request_len - conn->request_parser_pos};
    int64_t index = sv_indexof_sv(buf_view, SV_LIT("\r\n"));
    if (index >= 0) {
      buf_view.length = index;
      conn->request_parser_pos += index + 2;
      return buf_view;
    }

    ssize_t res = CoroutineRead(caller, conn->sock, conn->request_buf + conn->request_len, HTTP_REQUEST_SIZE_LIMIT - conn->request_len);
    if (res == 0) {
      *out_result = HTTP_REQ_CONN_CLOSED;
      return SV_EMPTY;
    }
    if (res < 0) {
      *out_result = HTTP_REQ_INTERNAL_FAIL;
      return SV_EMPTY;
    }

    conn->request_len += res;
  }
}

static bool sock_recv_exact(Coroutine *caller, HttpConnection *conn, size_t bytes, HttpReqParseResult *out_result) {
  size_t recieved = conn->request_len - conn->request_parser_pos;
  while (recieved < bytes) {
    if (conn->request_len >= HTTP_REQUEST_SIZE_LIMIT) {
      *out_result = HTTP_REQ_TOO_LARGE;
      return false;
    }

    ssize_t res = CoroutineRead(caller, conn->sock, conn->request_buf + conn->request_len, HTTP_REQUEST_SIZE_LIMIT - conn->request_len);
    if (res == 0) {
      *out_result = HTTP_REQ_CONN_CLOSED;
      return false;
    }

    if (res < 0) {
      *out_result = HTTP_REQ_INTERNAL_FAIL;
      return false;
    }

    recieved += res;
    conn->request_len += res;
  }

  return true;
}

static HttpRequest *recv_request_(HttpConnection *conn, HttpReqParseResult *out_result) {
  HttpRequest *req = calloc(1, sizeof(HttpRequest));

  HttpReqParseResult result;
  StringView request_line = sock_read_crlf_(conn->routine, conn, &result);
  if (request_line.length == 0) {
    free_req_(req);
    *out_result = result;
    return NULL;
  }

  StringView method = sv_chop_delim(&request_line, SV_LIT(" "));
  bool valid_method = sv_to_method_(method, &req->method);
  if (!valid_method) {
    free_req_(req);
    *out_result = HTTP_REQ_BAD;
    return NULL;
  }

  StringView target = sv_chop_delim(&request_line, SV_LIT(" "));
  if (target.length == 0) {
    free_req_(req);
    *out_result = HTTP_REQ_BAD;
    return NULL;
  }

  req->target = sv_chop_delim(&target, SV_LIT("?"));
  while (target.length > 0) {
    StringView query = sv_chop_delim(&target, SV_LIT("&"));

    StringView key = sv_chop_delim(&query, SV_LIT("="));
    StringView value = query;

    if (key.length == 0)
      continue;

    http_req_set_query(req, key, value);
  }

  StringView version = sv_trim(request_line);
  bool valid_version = sv_to_http_ver_(version, &req->http_ver);
  if (!valid_version) {
    free_req_(req);
    *out_result = HTTP_REQ_BAD;
    return NULL;
  }

  StringView header_line;
  while (1) {
    header_line = sock_read_crlf_(conn->routine, conn, &result);
    if (header_line.length == 0) {
      if (header_line.data == NULL && req->header_count > 0) {
        free_req_(req);
        *out_result = result;
        return NULL;
      }
      break;
    }
    int64_t colon_index = sv_indexof(header_line, ':');
    if (colon_index == -1) {
      free_req_(req);
      *out_result = HTTP_REQ_BAD;
      return NULL;
    }

    StringView header_name = {.data = header_line.data, .length = colon_index};
    StringView header_value = {.data = header_line.data + colon_index + 1, .length = header_line.length - colon_index - 1};
    header_value = sv_trim(header_value);
    http_req_set_header(req, header_name, header_value);
  }

  // Handle Content length and body
  StringView content_length_sv;
  bool has_content = http_req_get_header(req, SV_LIT("Content-Length"), &content_length_sv);
  if (!has_content)
    return req;

  uint64_t content_length;
  bool valid_content_length = sv_to_uint64(content_length_sv, &content_length);
  if (!valid_content_length) {
    *out_result = HTTP_REQ_BAD;
    free_req_(req);
    return NULL;
  }

  req->body = (StringView){
      .data = conn->request_buf + conn->request_parser_pos,
      .length = content_length,
  };
  bool ok = sock_recv_exact(conn->routine, conn, content_length, &result);
  if (!ok) {
    free_req_(req);
    *out_result = result;
    return NULL;
  }
  conn->request_parser_pos += content_length;

  return req;
}

static HttpResSendResult http_res_send_(Coroutine *caller, HttpConnection *conn, HttpResponse *res) {
  char response_buf[HTTP_RESPONSE_SIZE_LIMIT];
  size_t response_pos = 0;

#define res_buf_append_(src, len)                  \
  do {                                             \
    if (len + response_pos > sizeof(response_buf)) \
      return HTTP_RES_TOO_LARGE;                   \
    memcpy(response_buf + response_pos, src, len); \
    response_pos += len;                           \
  } while (0)

  StringView ver = http_ver_to_sv_(res->http_ver);
  res_buf_append_(ver.data, ver.length);
  res_buf_append_(" ", 1);

  char status_buf[4];
  int len = snprintf(status_buf, sizeof(status_buf), "%d", (int)res->status);
  if (len < 0)
    return HTTP_RES_INTERNAL_FAIL;

  res_buf_append_(status_buf, len);
  res_buf_append_(" ", 1);

  StringView status_sv = http_status_to_sv_(res->status);
  res_buf_append_(status_sv.data, status_sv.length);
  res_buf_append_("\r\n", 2);

  char len_buf[21];
  len = snprintf(len_buf, sizeof(len_buf), "%zu", res->body.length);
  if (len < 0)
    return HTTP_RES_INTERNAL_FAIL;

  StringView content_len_sv = {.data = len_buf, .length = (size_t)len};
  http_res_set_header(res, SV_LIT("Content-Length"), content_len_sv);

  for (size_t i = 0; i < res->header_count; i++) {
    StringView name = res->headers[i].name;
    StringView value = res->headers[i].value;

    res_buf_append_(name.data, name.length);
    res_buf_append_(": ", 2);
    res_buf_append_(value.data, value.length);
    res_buf_append_("\r\n", 2);
  }

  res_buf_append_("\r\n", 2);
  if (res->body.length > 0) {
    res_buf_append_(res->body.data, res->body.length);
  }

  CoroutineWrite(caller, conn->sock, response_buf, response_pos);
  return HTTP_RES_OK;
}

void http_res_set_header(HttpResponse *res, StringView name, StringView value) {
  for (size_t i = 0; i < res->header_count; i++) {
    if (sv_eq_ignorecase(name, res->headers[i].name)) {
      res->headers[i].value = value;
      return;
    }
  }

  if (res->header_count >= res->header_cap) {
    size_t new_cap = res->header_cap == 0 ? 8 : res->header_cap * 2;
    if (new_cap < res->header_count + 1)
      new_cap = res->header_count + 1;
    res->headers = realloc(res->headers, sizeof(HttpHeader) * new_cap);
    res->header_cap = new_cap;
  }

  res->headers[res->header_count++] = (HttpHeader){
      .name = name,
      .value = value,
  };
}
bool http_res_get_header(HttpResponse *res, StringView name, StringView *out_value) {
  for (size_t i = 0; i < res->header_count; i++) {
    if (sv_eq_ignorecase(name, res->headers[i].name)) {
      *out_value = res->headers[i].value;
      return true;
    }
  }
  return false;
}

void http_req_set_header(HttpRequest *req, StringView name, StringView value) {
  for (size_t i = 0; i < req->header_count; i++) {
    if (sv_eq_ignorecase(name, req->headers[i].name)) {
      req->headers[i].value = value;
      return;
    }
  }

  if (req->header_count >= req->header_cap) {
    size_t new_cap = req->header_cap == 0 ? 8 : req->header_cap * 2;
    if (new_cap < req->header_count + 1)
      new_cap = req->header_count + 1;
    req->headers = realloc(req->headers, sizeof(HttpHeader) * new_cap);
    req->header_cap = new_cap;
  }

  req->headers[req->header_count++] = (HttpHeader){
      .name = name,
      .value = value,
  };
}
bool http_req_get_header(HttpRequest *req, StringView name, StringView *out_value) {
  for (size_t i = 0; i < req->header_count; i++) {
    if (sv_eq_ignorecase(name, req->headers[i].name)) {
      *out_value = req->headers[i].value;
      return true;
    }
  }
  return false;
}

void http_req_set_query(HttpRequest *req, StringView key, StringView value) {
  for (size_t i = 0; i < req->query_count; i++) {
    if (sv_eq(key, req->queries[i].key)) {
      req->queries[i].value = value;
      return;
    }
  }

  if (req->query_count >= req->query_cap) {
    size_t new_cap = req->query_cap == 0 ? 8 : req->query_cap * 2;
    if (new_cap < req->query_count + 1)
      new_cap = req->query_count + 1;
    req->queries = realloc(req->queries, sizeof(HttpQuery) * new_cap);
    req->query_cap = new_cap;
  }

  req->queries[req->query_count++] = (HttpQuery){
      .key = key,
      .value = value,
  };
}

bool http_req_get_query(HttpRequest *req, StringView key, StringView *out_value) {
  for (size_t i = 0; i < req->query_count; i++) {
    if (sv_eq(key, req->queries[i].key)) {
      *out_value = req->queries[i].value;
      return true;
    }
  }
  return false;
}

HttpResSendResult http_res_send(HttpResponse *res) {
  return http_res_send_(res->conn->routine, res->conn, res);
}

HttpRoute *http_get_route(HttpServer *server, StringView target) {
  for (size_t i = 0; i < server->routes_count; i++) {
    if (sv_eq(target, server->routes[i].target)) {
      return &server->routes[i];
    }
  }

  return NULL;
}

HttpRoute *http_set_route(HttpServer *server, StringView target) {
  for (size_t i = 0; i < server->routes_count; i++) {
    if (sv_eq(target, server->routes[i].target)) {
      return &server->routes[i];
    }
  }

  if (server->routes_count >= server->routes_cap) {
    size_t new_cap = server->routes_cap == 0 ? 8 : server->routes_cap * 2;
    if (new_cap < server->routes_count + 1)
      new_cap = server->routes_count + 1;
    server->routes = realloc(server->routes, sizeof(HttpRoute) * new_cap);
    server->routes_cap = new_cap;
  }

  server->routes[server->routes_count++] = (HttpRoute){
      .target = target,
  };

  return &server->routes[server->routes_count - 1];
}

HttpRouteHandler http_route_get_handler_(HttpRoute *route, HttpMethod method) {
  switch (method) {
  case HTTP_GET:
    return route->get;
  case HTTP_POST:
    return route->post;
  case HTTP_PUT:
    return route->put;
  case HTTP_PATCH:
    return route->patch;
  case HTTP_DELETE:
    return route->delete;
  case HTTP_HEAD:
    return route->head;
  case HTTP_OPTIONS:
    return route->options;
  }

  return NULL;
}

void http_server_close_connection_(HttpServer *server, HttpConnection *conn) {
  for (size_t i = 0; i < server->connections_count; i++) {
    if (conn == server->connections[i]) {
      tcp_socket_close(conn->sock);
      free(conn);
      memmove(server->connections + i, server->connections + i + 1, sizeof(HttpConnection *) * (server->connections_count - i - 1));
      server->connections_count--;
      break;
    }
  }
}

HttpConnection *http_server_add_connection_(HttpServer *server, TCPSocket sock) {
  HttpConnection *conn = calloc(1, sizeof(HttpConnection));
  conn->sock = sock;

  if (server->connections_count >= server->connections_cap) {
    size_t new_cap = server->connections_cap == 0 ? 8 : server->connections_cap * 2;
    if (new_cap < server->connections_count + 1)
      new_cap = server->connections_count + 1;

    server->connections = realloc(server->connections, sizeof(HttpConnection *) * new_cap);
    server->connections_cap = new_cap;
  }

  server->connections[server->connections_count++] = conn;
  return conn;
}

coroutine(handle_connection_, HttpServer *server; HttpConnection * conn) {
  get_args(handle_connection_);
  args.conn->routine = routine;

  bool keep_connection = true;
  while (keep_connection) {
    if (args.conn->request_parser_pos) {
      memmove(args.conn->request_buf, args.conn->request_buf + args.conn->request_parser_pos, args.conn->request_len - args.conn->request_parser_pos);
      args.conn->request_len -= args.conn->request_parser_pos;
      args.conn->request_parser_pos = 0;
    }

    HttpResponse res = {.http_ver = HTTP_1_1, .conn = args.conn};
    time_t current_time;
    struct tm *gmt_time;
    time(&current_time);
    gmt_time = gmtime(&current_time);
    char time_buf[30];

    strftime(time_buf, sizeof(time_buf), "%a, %d %b %Y %H:%M:%S GMT", gmt_time);
    http_res_set_header(&res, SV_LIT("Date"), SV_STR(time_buf));

    HttpReqParseResult parse_res;
    HttpRequest *req = recv_request_(args.conn, &parse_res);

    if (!req) {
      if (parse_res == HTTP_REQ_TOO_LARGE)
        res.status = HTTP_CONTENT_TOO_LARGE;
      if (parse_res == HTTP_REQ_TIMEOUT)
        res.status = HTTP_REQUEST_TIMEOUT;
      if (parse_res == HTTP_REQ_BAD)
        res.status = HTTP_BAD_REQUEST;
      if (parse_res == HTTP_REQ_INTERNAL_FAIL)
        res.status = HTTP_INTERNAL_SERVER_ERROR;
      if (parse_res == HTTP_REQ_CONN_CLOSED)
        break;

      http_res_send(&res);
      free(res.headers);
      break;
    }

    keep_connection = req->http_ver == HTTP_1_1;
    StringView connection_val;
    bool has_connection_header = http_req_get_header(req, SV_LIT("Connection"), &connection_val);
    if (has_connection_header) {
      if (!sv_eq(connection_val, SV_LIT("keep-alive")) && !sv_eq(connection_val, SV_LIT("close"))) {
        res.status = HTTP_BAD_REQUEST;
        http_res_send(&res);

        free(res.headers);
        free_req_(req);
        break;
      }
      if (sv_eq(connection_val, SV_LIT("keep-alive")))
        keep_connection = true;
      else if (sv_eq(connection_val, SV_LIT("close")))
        keep_connection = false;
    }

    if (keep_connection) {
      http_res_set_header(&res, SV_LIT("Connection"), SV_LIT("keep-alive"));
    } else {
      http_res_set_header(&res, SV_LIT("Connection"), SV_LIT("close"));
    }

    HttpRoute *route = http_get_route(args.server, req->target);
    if (!route) {
      res.status = HTTP_NOT_FOUND;
      http_res_send(&res);
      free(res.headers);
      free_req_(req);
      continue;
    }

    HttpRouteHandler handler = http_route_get_handler_(route, req->method);
    if (!handler) {
      res.status = HTTP_METHOD_NOT_ALLOWED;
      http_res_send(&res);

      free(res.headers);
      free_req_(req);
      continue;
    }

    res.status = HTTP_OK;
    handler(req, &res);
    free_req_(req);
    free(res.headers);
  }

  // cleanup and connection termination.
  http_server_close_connection_(args.server, args.conn);
}

void http_server_listen(HttpServer *server, uint16_t port) {
  setup_coroutines();
  tcp_socket_bind(server->listen_sock, port);
  tcp_socket_listen(server->listen_sock, 5);

  while (1) {
    TCPSocket conn_sock = cr_accept(server->listen_sock, NULL, NULL);
    make_nonblocking(conn_sock);
    HttpConnection *conn = http_server_add_connection_(server, conn_sock);

    coroutine_spawn(handle_connection_, server, conn);
  }
  teardown_coroutines();
}
