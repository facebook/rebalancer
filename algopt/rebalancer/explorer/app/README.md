# Rebalancer Explorer

The Rebalancer Explorer web UI — a Next.js Backend-for-Frontend (BFF) for
inspecting and analyzing Rebalancer runs. It reaches the C++ backend only
through the JSON proxy (`POST /v2/<method>`), so it needs nothing beyond the
backend and the proxy to run.

## Run with docker-compose (recommended)

From the repository root, this brings up the backend, the proxy, and this app:

```bash
REBALANCER_PROXY_AUTH_TOKEN=secret docker compose up --build
```

Then open [http://localhost:3000](http://localhost:3000).

## Run locally with Node

```bash
# package.oss.json is this app's dependency manifest — use it as package.json.
cp package.oss.json package.json
yarn install
REBALANCER_PROXY_URL=http://localhost:8081 \
REBALANCER_PROXY_TOKEN=secret \
yarn build && yarn start
# or for development: ... yarn dev
```

> **Note:** if `yarn install` cannot resolve a package, regenerate `yarn.lock`
> against the public npm registry. The `Dockerfile` handles this automatically
> for the container build.

## Configuration

| Env var | Purpose |
| --- | --- |
| `REBALANCER_PROXY_URL` | Base URL of the JSON proxy (e.g. `http://localhost:8081`). Required — the app reaches the backend through the proxy. |
| `REBALANCER_PROXY_TOKEN` | Bearer token for `/v2/*` (omit if the proxy runs with `--disable_auth`). |
| `NEXT_PUBLIC_OSS_USER` | Display name shown in the UI (build-time; default `Explorer User`). |
