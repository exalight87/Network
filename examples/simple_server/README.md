# Simple HTTP Server

A minimal "Hello World" HTTP server demonstrating the basic usage of the library.

## Features

- Single route handling (`/`)
- Returns "Hello, World!" response
- Configurable port via command line argument

## Building

From the **project root** directory:

```bash
# Build the simple server example
xmake build example_simple_server
```

## Running

```bash
# Run with default port (8080)
xmake run example_simple_server

# Run with custom port
xmake run example_simple_server 9090
```

## Testing

Once running, test with curl:

```bash
curl http://localhost:8080/
```

Expected output: `Hello, World!`

**Note:** For multiple requests, use HTTP/1.1 explicitly:
```bash
curl --http1.1 http://localhost:8080/ http://localhost:8080/
```

## Code Overview

```cpp
// 1. Create server instance
HttpServer server;
server.port(8080);

// 2. Add route handler
server.addRoute({
    .route = "",  // Matches root path "/"
    .callable = [](const HttpRequest &request, HttpResponse &response) -> bool {
        response.body = "Hello, World!";
        response.code = 200;
        return true;
    }
});

// 3. Start server
server.start();
```
