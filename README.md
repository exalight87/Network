# HTTP Server Library

A high-performance C++ HTTP server library with HTTP/2 support, connection pooling, and modern C++ features.

## Features

- **High Performance**: Thread pool with hardware concurrency, epoll-based I/O
- **HTTP/2 Support**: Protocol detection and frame handling
- **Connection Pooling**: Efficient reuse of connections
- **Aggressive Keep-Alive**: 5-minute timeout, 10,000 requests per connection
- **Modern C++**: C++23 with RAII, smart pointers, and functional patterns

## Quick Start

### Build

The build is done with [Xmake](https://xmake.io/#/getting_started)

```bash
# Build the library and main server
xmake
```

### Run

```bash
# Run the main server (default port: 9090)
xmake run http_server

# Run with custom port
xmake run http_server 8080
```

### Test

The project includes comprehensive tests that run automatically after building:
- **Server Tests**: Basic functionality
- **Performance Tests**: Benchmarking
- **Robustness Tests**: Stress and resilience testing
- **Route Duplicate Tests**: Route registration validation

## Examples

The `examples/` directory contains ready-to-use examples:

| Example | Description | Best For |
|---------|-------------|----------|
| [simple_server](examples/simple_server/) | Minimal "Hello World" server | Learning basics |
| [file_server](examples/file_server/) | Static file server | Hosting websites |
| [api_server](examples/api_server/) | RESTful API server | Building APIs |

### Quick Example

Try the simple server from the project root:

```bash
xmake build example_simple_server
xmake run example_simple_server
```

Then test with:
```bash
curl http://localhost:8080/
```

**Note:** The examples demonstrate basic usage. For production use, consider:
- Using HTTP/1.1 explicitly for keep-alive: `curl --http1.1 http://localhost:8080/ http://localhost:8080/`
- Checking the example README files for specific limitations

## Architecture

### Components

- **HttpServer**: Main server class
- **NetworkSocket**: Socket management with epoll
- **ConnectionPool**: Thread pool for concurrent requests
- **HttpRoute**: Hierarchical route system
- **HttpRequest/HttpResponse**: Request/response handling
- **Http2**: HTTP/2 protocol support
- **NetworkCurl**: HTTP client functionality
- **RouteRegistry**: Route registration and lookup
- **Result/Error**: Error handling utilities
- **ScopeGuard**: RAII resource management

### Performance Optimizations

1. **Thread Pool**: Uses `std::thread::hardware_concurrency()` workers
2. **Epoll I/O**: Non-blocking accept loop on Linux
3. **Connection Reuse**: Aggressive keep-alive (5min timeout, 10k requests)
4. **Buffer Optimization**: Pre-allocated response buffers
5. **O(1) Header Lookup**: `std::unordered_map` instead of `std::map`

## Building

### Dependencies

- C++23 compiler (GCC 13+, Clang 16+, MSVC 2022+)
- libcurl (for HTTP client functionality)
- pthread (for threading)
- Xmake build system

### Build Commands

```bash
# Build everything
xmake

# Build specific target
xmake build http_server
xmake build server_tests
xmake build route_duplicate_tests

# Build in debug mode
xmake f -m debug
xmake

# Build in release mode
xmake f -m release
xmake
```

## API Usage

### Basic Server

```cpp
#include <HttpServer.hpp>
#include <HttpRoute.hpp>

int main() {
    HttpServer server;
    server.port(8080);
    
    server.addRoute({
        .route = "",
        .callable = [](const HttpRequest& req, HttpResponse& res) {
            res.body = "Hello, World!";
            res.code = 200;
            return true;
        }
    });
    
    server.start();
    return 0;
}
```

### Multiple Routes

```cpp
// Simple route
server.addRoute({
    .route = "about",
    .callable = [](auto& req, auto& res) {
        res.body = "About page";
        res.code = 200;
        return true;
    }
});

// API endpoint
server.addRoute({
    .route = "api/users",
    .callable = [](auto& req, auto& res) {
        res.body = R"({"users": [...]})";
        res.headers["Content-Type"] = "application/json";
        res.code = 200;
        return true;
    }
});
```

### File Serving

```cpp
server.addRoute({
    .route = "resources",
    .callable = [](const HttpRequest& req, HttpResponse& res) {
        if (res.loadFile(req.url.path)) {
            res.code = 200;
            return true;
        }
        res.code = 404;
        return true;
    }
});
```

## Testing

Run all tests after building:

```bash
# Run individual test suites
xmake run server_tests
xmake run performance_tests
xmake run robustness_tests
xmake run route_duplicate_tests
```

All tests run automatically after a successful build.

## Project Structure

```
test_curl/
├── src/                 # Library source files
│   ├── HttpServer.*     # Main server
│   ├── NetworkSocket.*  # Socket management
│   ├── ConnectionPool.* # Thread pool
│   ├── HttpRoute.*      # Route handling
│   ├── HttpRequest.*    # Request parsing
│   ├── HttpResponse.*   # Response building
│   ├── Http2.*          # HTTP/2 support
│   ├── NetworkCurl.*    # HTTP client
│   ├── RouteRegistry.*  # Route registration
│   ├── Result.hpp       # Result type
│   ├── Error.hpp        # Error handling
│   └── ...
├── tests/               # Test files
│   ├── server_test.cpp
│   ├── performance_test.cpp
│   ├── robustness_test.cpp
│   └── route_duplicate_test.cpp
├── examples/            # Example servers
│   ├── simple_server/
│   ├── file_server/
│   └── api_server/
├── public/              # Public static files
├── website/             # Example website files
├── resources/           # Static resources
├── xmake.lua            # Build configuration
└── README.md            # This file
```

## License

This project is provided as-is for educational and development purposes.

## Contributing

Contributions are welcome! Please ensure all tests pass before submitting changes.