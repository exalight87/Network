# API Server Example

A RESTful API server with JSON endpoints and HTML pages.

## Features

- **JSON API endpoints** with proper content-type headers
- **HTML pages** using the HttpPage helper
- **Route parameters** for dynamic paths
- **Multiple endpoints** demonstrating different use cases

## Endpoints

| Method | Path | Description | Response Type |
|--------|------|-------------|---------------|
| GET | `/` | Home page | HTML |
| GET | `/api/users` | List all users | JSON |
| GET | `/api/health` | Health check | JSON |

## Building

From the **project root** directory:

```bash
# Build the API server example
xmake build example_api_server
```

## Running

```bash
# Run with default port (8080)
xmake run example_api_server

# Run with custom port
xmake run example_api_server 9090
```

## Testing

Start the server, then test with curl:

```bash
# Test HTML home page
curl http://localhost:8080/

# Test HTML about page
curl http://localhost:8080/about

# Test JSON API endpoints
curl http://localhost:8080/api/users
curl http://localhost:8080/api/users/1
curl http://localhost:8080/api/health
```

Or open in a web browser:
- http://localhost:8080/ (Home page)
- http://localhost:8080/about (About page)

## Code Structure

### API Endpoint Example

```cpp
server.addRoute({
    .route = "api/users",
    .callable = [](const HttpRequest &request, HttpResponse &response) -> bool
    {
        response.body = R"({"users": [...]})";
        response.headers["Content-Type"] = "application/json";
        response.code = 200;
        return true;
    }
});
```

### HTML Page Example

```cpp
server.addRoute({
    .route = "",
    .callable = [](const HttpRequest &request, HttpResponse &response) -> bool
    {
        HttpPage page;
        page.setTitle("API Server");
        page.setBody("<h1>Welcome</h1>");
        response.body = page;
        response.code = 200;
        return true;
    }
});
```

## Route Parameters

The library supports route parameters using `{name}` syntax:

```cpp
// Matches: /api/users/123, /api/users/abc, etc.
server.addRoute({
    .route = "api/users/{id}",
    .callable = [](const HttpRequest &request, HttpResponse &response) -> bool
    {
        // Access path parameters from request.url
        // (Implementation depends on library's parameter extraction)
        return true;
    }
});
```
