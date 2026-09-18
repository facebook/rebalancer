---
sidebar_position: 7
---

import useBaseUrl from '@docusaurus/useBaseUrl';

# Rebalancer Explorer

Rebalancer Explorer is a web UI for inspecting and analyzing solver runs. It lets
you browse a problem's objects, containers, constraints, and goals, and see how a
solution scores against them. This makes it a handy way to understand and debug
solver behavior.

<video
  aria-label="Rebalancer Explorer demo"
  controls
  playsInline
  preload="metadata"
  style={{width: '100%', maxWidth: '960px'}}>
  <source
    src={useBaseUrl('/videos/rebalancer-explorer-demo.mp4')}
    type="video/mp4"
  />
  Your browser does not support embedded videos.
</video>

## How It Works

The Explorer web UI is a Next.js Backend-for-Frontend (BFF). Under the hood, its
C++ Thrift backend serves run data directly from the solver. A small JSON proxy
sits in front of the backend and exposes the Thrift API over plain HTTP
(`POST /v2/<method>`). The web UI talks only to this proxy, so the frontend needs
no Thrift toolchain and runs anywhere Node runs.

## Run with Docker Compose

The quickest way to try Explorer is the bundled `docker-compose.yml`, which
builds and wires up the C++ backend, the JSON proxy, and the Next.js app. From
the repository root, run:

```bash
REBALANCER_PROXY_AUTH_TOKEN=secret docker compose up --build
```

Then open [http://localhost:3000](http://localhost:3000).

Compose also seeds a shared volume with example problem bundles, so you can load
one from the UI by name. For example, load the
[`sudoku.py`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/examples/sudoku/sudoku.py)
example as `sudoku.bundle`, or the
[`EightQueens.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/examples/eightqueens/EightQueens.cpp)
example as `eightqueens.bundle`. This lets you explore a sample run without
setting up your own.

## Run Locally with Node

From `algopt/rebalancer/explorer/app`, prepare the open-source package manifest,
install the dependencies, and point the app at a running JSON proxy:

```bash
# package.oss.json is this app's dependency manifest; use it as package.json.
cp package.oss.json package.json
yarn install
REBALANCER_PROXY_URL=http://localhost:8081 \
REBALANCER_PROXY_TOKEN=secret \
yarn build && yarn start
# Or, for development, replace `yarn build && yarn start` with `yarn dev`.
```

If `yarn install` cannot resolve a package, regenerate `yarn.lock` against the
public npm registry. The Explorer `Dockerfile` handles this automatically for
the container build.

## Configuration

| Environment variable | Purpose |
| --- | --- |
| `REBALANCER_PROXY_URL` | Base URL of the JSON proxy, such as `http://localhost:8081`. This is required because the app reaches the backend through the proxy. |
| `REBALANCER_PROXY_TOKEN` | Bearer token for `/v2/*`. Omit it only when the proxy runs with `--disable_auth`. |
| `NEXT_PUBLIC_OSS_USER` | Display name shown in the UI. This is set at build time and defaults to `Explorer User`. |

See the
[`algopt/rebalancer/explorer/app` README](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/explorer/app/README.md)
for source-level details about the Explorer frontend.
