#ifndef HTTP_H_
#define HTTP_H_

#include "tcp.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTTP_REQUEST_SIZE_LIMIT 8192
#define HTTP_RESPONSE_SIZE_LIMIT 8192

#ifndef STRING_VIEW_DEFINED
#define STRING_VIEW_DEFINED
typedef struct {
  const char *data;
  size_t length;
} StringView;
#endif

typedef enum {
  HTTP_GET,
  HTTP_POST,
  HTTP_PUT,
  HTTP_PATCH,
  HTTP_DELETE,
  HTTP_HEAD,
  HTTP_OPTIONS
} HttpMethod;

typedef enum {
  HTTP_OK = 200,
  HTTP_CREATED = 201,
  HTTP_NO_CONTENT = 204,

  HTTP_BAD_REQUEST = 400,
  HTTP_NOT_FOUND = 404,
  HTTP_METHOD_NOT_ALLOWED = 405,
  HTTP_REQUEST_TIMEOUT = 408,
  HTTP_CONTENT_TOO_LARGE = 413,

  HTTP_INTERNAL_SERVER_ERROR = 500,
} HttpStatus;

typedef enum {
  HTTP_1_0,
  HTTP_1_1,
} HttpVersion;

typedef struct {
  StringView name;
  StringView value;
} HttpHeader;

typedef struct {
  StringView key;
  StringView value;
} HttpQuery;

typedef struct {
  HttpMethod method;
  HttpVersion http_ver;

  StringView target;

  HttpQuery *queries;
  size_t query_count;
  size_t query_cap;

  HttpHeader *headers;
  size_t header_count;
  size_t header_cap;

  StringView body;
} HttpRequest;

typedef struct {
  TCPSocket sock;
  char request_buf[HTTP_REQUEST_SIZE_LIMIT];
  size_t request_len;
  size_t request_parser_pos;

  struct Coroutine *routine;
} HttpConnection;

typedef struct {
  HttpVersion http_ver;
  HttpStatus status;

  HttpHeader *headers;
  size_t header_count;
  size_t header_cap;

  StringView body;

  HttpConnection *conn;
} HttpResponse;

typedef void (*HttpRouteHandler)(HttpRequest *req, HttpResponse *res);

typedef struct {
  StringView target;

  HttpRouteHandler get;
  HttpRouteHandler post;
  HttpRouteHandler put;
  HttpRouteHandler patch;
  HttpRouteHandler delete;
  HttpRouteHandler head;
  HttpRouteHandler options;
} HttpRoute;

typedef struct {
  TCPSocket listen_sock;

  HttpConnection **connections;
  size_t connections_count;
  size_t connections_cap;

  HttpRoute *routes;
  size_t routes_count;
  size_t routes_cap;
} HttpServer;

typedef enum {
  HTTP_REQ_TOO_LARGE,
  HTTP_REQ_TIMEOUT,
  HTTP_REQ_BAD,
  HTTP_REQ_INTERNAL_FAIL,
  HTTP_REQ_CONN_CLOSED,
} HttpReqParseResult;

typedef enum {
  HTTP_RES_OK,
  HTTP_RES_TOO_LARGE,
  HTTP_RES_TIMEOUT,
  HTTP_RES_INTERNAL_FAIL,
  HTTP_RES_CONN_CLOSED,
} HttpResSendResult;

HttpServer *http_server_create(void);
void http_server_listen(HttpServer *server, uint16_t port);

void http_server_listen(HttpServer *server, uint16_t port);
void http_server_close(HttpServer *server);

void http_res_set_header(HttpResponse *res, StringView name, StringView value);
bool http_res_get_header(HttpResponse *res, StringView name, StringView *out_value);

void http_req_set_header(HttpRequest *req, StringView name, StringView value);
bool http_req_get_header(HttpRequest *req, StringView name, StringView *out_value);

void http_req_set_query(HttpRequest *req, StringView key, StringView value);
bool http_req_get_query(HttpRequest *req, StringView key, StringView *out_value);

HttpResSendResult http_res_send(HttpResponse *res);

HttpRoute *http_get_route(HttpServer *server, StringView target);
HttpRoute *http_set_route(HttpServer *server, StringView target);

#endif
