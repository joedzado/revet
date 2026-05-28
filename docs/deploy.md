# Deploying revet

revet produces a single, static-ish executable at `build/revet`. Deployment is "copy the binary, give it config, run it." This page covers the boring infrastructure around that.

## Container build

```dockerfile
# syntax=docker/dockerfile:1.6

# ── Stage 1: build ───────────────────────────────────────────────────────────
FROM debian:bookworm AS build
ARG REVET_VERSION=0.1.5

RUN apt-get update && apt-get install -y --no-install-recommends \
    git curl ca-certificates python3 python3-pip cmake ninja-build \
    clang-19 libc++-19-dev libc++abi-19-dev \
    libsodium-dev libcurl4-openssl-dev libsqlite3-dev libpq-dev \
 && rm -rf /var/lib/apt/lists/* \
 && pip3 install --break-system-packages conan

ENV CC=clang-19 CXX=clang++-19

# Install revet framework
RUN curl -fsSL https://raw.githubusercontent.com/joedzado/revet/main/install.sh | sh

# Copy app, build
WORKDIR /src
COPY . .
RUN revet build

# ── Stage 2: runtime ─────────────────────────────────────────────────────────
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    libsodium23 libcurl4 libsqlite3-0 libpq5 ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 revet

COPY --from=build /src/build/revet     /usr/local/bin/revet-app
COPY --from=build /src/app/public      /app/public
COPY --from=build /src/revet.toml      /app/revet.toml

WORKDIR /app
USER revet:revet
EXPOSE 8080
ENV LOG_LEVEL=info REVET_FORCE_SECURE_COOKIES=1
# REVET_COOKIE_KEY must be injected at run time, never baked in.
ENTRYPOINT ["/usr/local/bin/revet-app"]
```

Build + run:

```bash
docker build -t myapp:$(git rev-parse --short HEAD) .
docker run --rm -p 8080:8080 \
    -e REVET_COOKIE_KEY=$(openssl rand -hex 32) \
    -e DATABASE_URL=postgres://... \
    myapp:abcdef0
```

## systemd unit

`/etc/systemd/system/revet-app.service`:

```ini
[Unit]
Description=revet app
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=revet
Group=revet
WorkingDirectory=/srv/myapp
ExecStart=/srv/myapp/revet --port 8080
Restart=on-failure
RestartSec=2s

# Secrets — owned by root, mode 0640, group `revet`. See `man systemd.exec`.
EnvironmentFile=/etc/revet/myapp.env

# Hardening
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
PrivateDevices=true
ReadWritePaths=/srv/myapp/build/data /srv/myapp/uploads
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictSUIDSGID=true
LockPersonality=true
MemoryDenyWriteExecute=true
RestrictRealtime=true
SystemCallArchitectures=native
CapabilityBoundingSet=
AmbientCapabilities=

[Install]
WantedBy=multi-user.target
```

`/etc/revet/myapp.env`:

```env
REVET_COOKIE_KEY=<64 hex chars>
REVET_FORCE_SECURE_COOKIES=1
LOG_LEVEL=info
DATABASE_URL=postgres://app:...@db.internal/myapp
```

```bash
sudo chmod 0640 /etc/revet/myapp.env
sudo chown root:revet /etc/revet/myapp.env
sudo systemctl daemon-reload
sudo systemctl enable --now revet-app
```

## Reverse proxy (nginx)

revet does not speak TLS. Terminate it upstream.

```nginx
server {
    listen 443 ssl http2;
    server_name myapp.example.com;

    ssl_certificate     /etc/letsencrypt/live/myapp.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/myapp.example.com/privkey.pem;

    # Strip client-supplied forwarded headers — never trust them.
    proxy_set_header X-Real-IP        $remote_addr;
    proxy_set_header X-Forwarded-For  $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Host $host;
    proxy_set_header X-Forwarded-Proto https;
    proxy_set_header Host             $host;

    # SSE / HTMX streaming: disable buffering, allow long timeouts.
    proxy_buffering    off;
    proxy_read_timeout 1h;

    location / {
        proxy_pass http://127.0.0.1:8080;
    }

    # Static assets served by revet itself today — no need to bypass.
}

server {
    listen 80;
    server_name myapp.example.com;
    return 301 https://$host$request_uri;
}
```

## Signals and shutdown

Today revet does not handle `SIGTERM` gracefully — `systemctl stop` will send SIGTERM, the process exits, in-flight requests are cut. Acceptable for most server-rendered apps where requests are sub-second; if you stream SSE or run long async API calls, drain explicitly behind the reverse proxy before stopping.

This is a [known limitation](./troubleshooting.md#graceful-shutdown) tracked for a future release.

## Running as non-root

Always. Both Docker and systemd snippets above run as a dedicated user. revet does not need root for any default operation — file uploads land in `uploads/files/*` (owned by the app user), the SQLite DB is in `build/data/`, the listen port is 8080 (unprivileged). Bind to 443 only via the reverse proxy or `setcap CAP_NET_BIND_SERVICE=+ep` on the binary if you must skip nginx.

## Logging in production

Logs go to **stderr** as `[timestamp] LEVEL [tag] message`. The framework does not write files or rotate logs. Use the platform's log collector:

- **systemd**: `journalctl -u revet-app -f`. Rotation handled by journald.
- **Docker**: `docker logs` + a logging driver (`json-file` + logrotate, or ship to a collector via `--log-driver fluentd`/`gelf`/etc.).
- **Kubernetes**: stdout/stderr → node-level fluent-bit → your log backend.

Set `LOG_LEVEL=info` in prod (default), `debug` only when chasing a bug. There is no built-in request log — add one via middleware if you need one.

## Database backups

revet does not back up your database. Standard practice:

- **SQLite**: stop the app or use `sqlite3 db.sqlite ".backup '/path/to/backup.db'"` (online-safe). Snapshot the result.
- **Postgres**: `pg_dump` or your provider's snapshot/PITR feature. There is no first-class `revet` command for this.

For schema migrations between deploys, run a separate migration tool (e.g. `goose`, raw `psql -f migrations/N.sql`) before starting the new binary. revet has no built-in migration runner — schema lives in `app/queries/*.sql` with `@revet:name: create_schema`, which the template runs once at boot via `site_init.cpp`. That's idempotent (CREATE TABLE IF NOT EXISTS), not versioned.

## Third-party license attribution

Your `build/revet` binary statically links against `libsodium`, `libcurl`, `simdjson`, `ctre`, optionally `libsqlite3` and `libpq`, and vendors htmx. Several of those licenses (Apache-2.0, the curl license, the PostgreSQL License, ISC) require you to **preserve the upstream copyright notices** when you distribute the binary to a third party.

`THIRD_PARTY_LICENSES` at the framework root aggregates the notices. Ship it alongside your binary:

```Dockerfile
# In your Dockerfile, after copying the binary:
COPY --from=build /root/.revet/current/THIRD_PARTY_LICENSES /app/THIRD_PARTY_LICENSES
```

```bash
# Or with a tarball release:
tar czf myapp.tar.gz build/revet THIRD_PARTY_LICENSES revet.toml app/public
```

If you `revet add` additional Conan dependencies, you must extend the file with their licenses. The framework does not yet auto-generate per-project license files; it's on you.

For build-time-only tools (CMake, Ninja, Conan, Catch2, LLVM, Bun): their licenses do not flow through to your binary, so you don't need to ship them. They are still listed in `THIRD_PARTY_LICENSES` for completeness.

## Static linking caveats

`build/revet` links against `libsodium`, `libcurl`, `libsqlite3`, optionally `libpq`, plus the platform's `libc++` / `libstdc++`. Verify with:

```bash
otool -L build/revet     # macOS
ldd build/revet          # Linux
```

A "static-ish" deploy strategy: build in the same Debian version you'll run on, use the matching dynamic libraries. True fully-static (musl) is not yet supported because some Conan packages don't ship musl builds.
