FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc pkg-config libsqlite3-dev libssl-dev libpq-dev openssl ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . /src
RUN ./cforge build
ENV PORT=8080 \
    TLS_PORT=8443 \
    CFORGE_BIND=0.0.0.0 \
    CFORGE_DB=/data/users.db
EXPOSE 8080 8443
VOLUME /data
CMD ["./build/cforge-users"]
