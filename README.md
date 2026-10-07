# C Library For Making HTTP Servers
Single Thread HTTP Server with concurrency achieved using my [coroutines library](https://github.com/eitankushnir/c-coroutines).  
Basically like ExpressJS but written in C to be used in C. Only works on linux probably.

## Installation
First compile the project into a static library with the Makefile.
```bash
git clone https://github.com/eitankushnir/c-http.git
cd c-http
make
```

Then simply link `libhttp.a` to your project using `-lhttp` and `-L<path_to_libhttp_directory>` compiler flags.

## Using in a project
The header you want is [http.h](/include/http.h). Use this header to access library functions.  
In order to manipulate StringView(s) use my [StringView library](https://github.com/eitankushnir/c-string_view).

## Example
Here is a simple example for making a server that sends a simple hello world response to the client.
```c
#include "http.h"
#include "string_view.h"
#include <stdio.h>

void handle_get(HttpRequest *req, HttpResponse *res) {
  res->body = SV_LIT("Hello, World");
  http_res_send(res);
}

int main() {
  HttpServer *s = http_server_create();
  http_set_route(s, SV_LIT("/"))->get = handle_get;

  http_server_listen(s, 8080);
}
```
