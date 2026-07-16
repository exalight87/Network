# File Server Example

A static file server that serves files from a `public/` directory.

## Features

- Serves static files (HTML, CSS, JS, images, etc.)
- Automatic `index.html` for root path
- 404 error handling for missing files
- Creates `public/` directory if it doesn't exist

## Building

From the **project root** directory:

```bash
# Build the file server example
xmake build example_file_server
```

## Running

```bash
# Run with default port (8080)
xmake run example_file_server

# Run with custom port
xmake run example_file_server 9090
```

## Testing

1. Create a simple HTML file:

```bash
mkdir -p public
echo '<h1>Hello from File Server!</h1>' > public/index.html
```

2. Start the server:

```bash
xmake run example_file_server
```

3. Access in browser or with curl:

```bash
curl http://localhost:8080/
```

## Directory Structure

```
file_server/
├── main.cpp          # Server implementation
├── xmake.lua         # Build configuration
├── README.md         # This file
└── public/           # Static files directory (auto-created)
    └── index.html    # Example HTML file
```

## How It Works

1. The server listens on the specified port
2. When a request comes in, it constructs a file path:
   - Root path (`/`) → `public/index.html`
   - `/about.html` → `public/about.html`
   - `/css/style.css` → `public/css/style.css`
3. The `response.loadFile()` method reads the file from disk
4. If the file exists, it's served with 200 status
5. If the file doesn't exist, a 404 error is returned
