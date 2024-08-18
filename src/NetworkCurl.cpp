#include <NetworkCurl.hpp>
#include <stdexcept>
#include <span>

namespace
{
    // Debug resources
    // https://curl.se/libcurl/c/debug.html
    struct Data
    {
        char trace_ascii; /* 1 or 0 */
    };

    void dump(const char *text,
              FILE *stream, unsigned char *ptr, size_t size,
              bool isTraceAsciiEnable)
    {
        size_t i;
        size_t c;

        unsigned int width = 0x10;

        if (isTraceAsciiEnable)
            /* without the hex output, we can fit more on screen */
            width = 0x40;

        fprintf(stream, "%s, %10.10lu bytes (0x%8.8lx)\n",
                text, (unsigned long)size, (unsigned long)size);

        for (i = 0; i < size; i += width)
        {

            fprintf(stream, "%4.4lx: ", (unsigned long)i);

            if (!isTraceAsciiEnable)
            {
                /* hex not disabled, show it */
                for (c = 0; c < width; c++)
                    if (i + c < size)
                        fprintf(stream, "%02x ", ptr[i + c]);
                    else
                        fputs("   ", stream);
            }

            for (c = 0; (c < width) && (i + c < size); c++)
            {
                /* check for 0D0A; if found, skip past and start a new line of output */
                if (isTraceAsciiEnable && (i + c + 1 < size) && ptr[i + c] == 0x0D &&
                    ptr[i + c + 1] == 0x0A)
                {
                    i += (c + 2 - width);
                    break;
                }
                fprintf(stream, "%c",
                        (ptr[i + c] >= 0x20) && (ptr[i + c] < 0x80) ? ptr[i + c] : '.');
                /* check again for 0D0A, to avoid an extra \n if it's at width */
                if (isTraceAsciiEnable && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D &&
                    ptr[i + c + 2] == 0x0A)
                {
                    i += (c + 3 - width);
                    break;
                }
            }
            fputc('\n', stream); /* newline */
        }
        fflush(stream);
    }

    int my_trace(CURL *handle, curl_infotype type,
                 char *data, size_t size,
                 bool isTraceAsciiEnable)
    {
        const char *text;
        (void)handle; /* prevent compiler warning */

        switch (type)
        {
        case CURLINFO_TEXT:
            fprintf(stderr, "== Info: %s", data);
            return 0;
        case CURLINFO_HEADER_OUT:
            text = "=> Send header";
            break;
        case CURLINFO_DATA_OUT:
            text = "=> Send data";
            break;
        case CURLINFO_SSL_DATA_OUT:
            text = "=> Send SSL data";
            break;
        case CURLINFO_HEADER_IN:
            text = "<= Recv header";
            break;
        case CURLINFO_DATA_IN:
            text = "<= Recv data";
            break;
        case CURLINFO_SSL_DATA_IN:
            text = "<= Recv SSL data";
            break;
        default: /* in case a new one is introduced to shock us */
            return 0;
        }

        dump(text, stderr, (unsigned char *)data, size, isTraceAsciiEnable);
        return 0;
    }
    // End debug resources
}

NetworkCurl::NetworkCurl(/* args */)
{
    m_curl = curl_easy_init();
    if (m_curl == nullptr)
    {
        throw std::runtime_error("Impossible to init curl");
    }

    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &NetworkCurl::m_FillNetworkResponse);
}

NetworkCurl::~NetworkCurl()
{
    curl_easy_cleanup(m_curl);
}

void NetworkCurl::EnableDebug()
{
    curl_easy_setopt(m_curl, CURLOPT_DEBUGFUNCTION, my_trace);
    curl_easy_setopt(m_curl, CURLOPT_DEBUGDATA, true);

    /* the DEBUGFUNCTION has no effect until we enable VERBOSE */
    curl_easy_setopt(m_curl, CURLOPT_VERBOSE, 1L);

    /* example.com is redirected, so we tell libcurl to follow redirection */
    curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
}

void NetworkCurl::DisableDebug()
{
    /* the DEBUGFUNCTION has no effect until we enable VERBOSE */
    curl_easy_setopt(m_curl, CURLOPT_VERBOSE, 0L);

    /* example.com is redirected, so we tell libcurl to follow redirection */
    curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 0L);
}

size_t NetworkCurl::m_FillNetworkResponse(void *data, size_t size, size_t nmemb, void *networkResponse)
{
    size_t realsize = size * nmemb;
    NetworkResponse *response = static_cast<NetworkResponse *>(networkResponse);
    auto chunk = std::span(static_cast<uint8_t *>(data), realsize);
    response->memory.reserve(response->memory.size() + chunk.size());
    response->memory.insert(response->memory.end(), chunk.begin(), chunk.end());

    return realsize;
}

NetworkResponse NetworkCurl::Get(std::string_view URL)
{
    NetworkResponse response;
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, (void *)&response);
    curl_easy_setopt(m_curl, CURLOPT_URL, URL.data());
    CURLcode res = curl_easy_perform(m_curl);

    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &response.code);

    curl_header *prev = NULL;
    curl_header *h;
    /* extract the normal headers from the first request */
    while ((h = curl_easy_nextheader(m_curl, CURLH_HEADER, 0, prev)))
    {
        response.headers.emplace( h->name, h->value );
        prev = h;
    }

    /* extract the normal headers + 1xx + trailers from the last request */
    unsigned int origin = CURLH_HEADER | CURLH_1XX | CURLH_TRAILER;
    while ((h = curl_easy_nextheader(m_curl, origin, -1, prev)))
    {
        response.headers.emplace(h->name, h->value);
        prev = h;
    }

    /* Check for errors */
    if (res != CURLE_OK)
    {
        auto errorMsg = std::format("curl_easy_perform() failed: {}", curl_easy_strerror(res));
        response.memory = {errorMsg.begin(), errorMsg.end()};
    }

    return response;
}

NetworkCurl &NetworkCurl::GetInstance()
{
    static NetworkCurl instance;
    return instance;
}