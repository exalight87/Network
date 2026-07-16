# Curl Client Example

Minimal example for the optional libcurl integration.

## Building

From the project root:

```bash
xmake f --with_curl=y
xmake build example_curl_client
```

## Running

```bash
xmake run example_curl_client http://example.com
```

## Code Overview

```cpp
#include <test_curl/integrations/curl/NetworkCurl.hpp>

auto& client = NetworkCurl::GetInstance();
const auto response = client.Get("http://example.com");
```
