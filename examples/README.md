# Examples

This directory contains small examples demonstrating the HTTP kernel and optional integrations.

## Available Examples

### 1. Simple Server (`simple_server/`)
**A minimal "Hello World" HTTP server.**

- Single route handling
- Returns plain text response
- Best for: Learning the basics

**Quick Start:**
```bash
# From project root
xmake build example_simple_server
xmake run example_simple_server
```

### 2. File Server (`file_server/`)
**Static file server for serving web pages and assets.**

- Serves files from a `public/` directory
- Automatic `index.html` for root path
- 404 error handling
- Best for: Hosting static websites

**Quick Start:**
```bash
# From project root
mkdir -p public
echo '<h1>Hello!</h1>' > public/index.html
xmake build example_file_server
xmake run example_file_server
```

### 3. API Server (`api_server/`)
**RESTful API server with JSON endpoints and HTML pages.**

- Multiple API endpoints
- JSON responses with proper content-type headers
- HTML page generation
- Route parameters
- Best for: Building web APIs

**Quick Start:**
```bash
# From project root
xmake build example_api_server
xmake run example_api_server
```

### 4. Curl Client (`curl_client/`)
**Minimal client using the optional libcurl integration.**

- Builds only when `with_curl` is enabled
- Demonstrates `NetworkCurl::Get`
- Best for: Testing or learning the integration layer

**Quick Start:**
```bash
# From project root
xmake f --with_curl=y
xmake build example_curl_client
xmake run example_curl_client http://example.com
```

## Building Examples

Build from the **project root** directory:

```bash
# Build specific example
xmake build example_simple_server
xmake run example_simple_server

# Build all kernel-only examples
xmake build example_simple_server example_file_server example_api_server
```

## Testing Examples

After starting any server, test with curl:

```bash
# Test home page
curl http://localhost:8080/

# Test specific endpoint (API server)
curl http://localhost:8080/api/users
```

## Next Steps

1. **Start Simple**: Begin with `simple_server` to understand the basics
2. **Add Files**: Move to `file_server` to serve static content
3. **Build APIs**: Use `api_server` as a template for REST APIs
4. **Customize**: Copy and modify examples for your own projects

## Project Structure

```
examples/
├── README.md              # This file
├── simple_server/         # Minimal example
│   ├── main.cpp
│   ├── xmake.lua
│   └── README.md
├── file_server/           # Static file serving
│   ├── main.cpp
│   ├── xmake.lua
│   └── README.md
├── api_server/            # REST API example
│   ├── main.cpp
│   ├── xmake.lua
│   └── README.md
├── curl_client/           # Optional libcurl integration example
│   ├── main.cpp
│   └── README.md
└── showcase_server/       # Main demo target used by http_server
    └── main.cpp
```

## Library Features Demonstrated

- [x] Basic server setup
- [x] Route handling
- [x] Static file serving
- [x] HTML page generation
- [x] JSON API responses
- [x] Content-Type headers
- [x] Error handling (404)
- [x] Command-line port configuration
- [x] Optional libcurl client integration

## Documentation

For more details, see:
- [Main Project README](../README.md)
- [Public headers](../include/test_curl/)
