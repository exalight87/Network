FROM ubuntu:24.04 AS builder
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates curl g++ make \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL https://xmake.io/shget.text | bash -s v2.9.9
ENV PATH="/root/.local/bin:${PATH}"
WORKDIR /src
COPY xmake.lua ./
COPY include ./include
COPY src ./src
COPY examples/showcase_server ./examples/showcase_server
RUN xmake f -m release -y && xmake -y http_server

FROM ubuntu:24.04 AS runtime
RUN groupadd --system demo && useradd --system --gid demo --no-create-home demo
COPY --from=builder /src/build/linux/*/release/http_server /usr/local/bin/http-demo
USER demo:demo
EXPOSE 9090
ENTRYPOINT ["/usr/local/bin/http-demo", "9090"]
