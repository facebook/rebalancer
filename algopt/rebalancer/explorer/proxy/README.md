# Rebalancer Explorer JSON Proxy

A standalone **HTTP/JSON front door** for the Thrift `RebalancerExplorerService`.

External callers `POST /v2/<method>` with a JSON body; the proxy transcodes
JSON ⇄ Thrift (via the codegen's `SimpleJSONSerializer`) and forwards each call
to a **single configured Explorer backend** over fbthrift/Rocket. It exists so
the Explorer BFF can reach the backend from outside Meta, where the
nest-proxy / ServiceRouter path is unavailable.

## HTTP contract

### `POST /v2/<method>`

`<method>` is the exact Thrift method name. The request body is a JSON object
whose keys are the Thrift **argument names** for that method; each value is the
JSON encoding of the corresponding Thrift struct. The response body is the JSON
encoding of the Thrift return value (HTTP `200`), or `{"error": "..."}` on
failure.

All 16 service methods are proxied:

| `POST /v2/...`                   | Request body fields                                           |
| -------------------------------- | ------------------------------------------------------------ |
| `getHandle`                      | `request`                                                    |
| `getSandboxStatus`               | `handle`                                                     |
| `getProblemMetadataV2`           | `handle`                                                     |
| `getDataV2`                      | `handle`, `request`                                          |
| `evaluateV2`                     | `handle`, `request`                                          |
| `evaluateMetricCollection`       | `handle`, `request`, `evaluateRequestA`, `evaluateRequestB` |
| `getGoalSpecV2`                  | `handle`, `request`                                          |
| `getConstraintSpecV2`            | `handle`, `request`                                          |
| `getTypeaheadV2`                 | `handle`, `request`                                          |
| `getMovesBetweenAssignmentsV2`   | `handle`, `request`                                          |
| `getTreeNodeV2`                  | `handle`, `request`                                          |
| `getMetricDistributionV2`        | `handle`, `request`                                          |
| `getLocalSearchProfilesV2`       | `handle`                                                     |
| `getMoveSets`                    | `handle`, `request`                                          |
| `editProblemV2`                  | `handle`, `request`                                          |
| `exportTable`                    | `handle`, `request`                                          |

Authentication: every `/v2/*` request must carry an
`Authorization: Bearer <token>` header (unless `--disable_auth` is set).

Example (resolve a Manifold run id to a sandbox handle):

```bash
curl -sS http://localhost:8081/v2/getHandle \
  -H 'Authorization: Bearer '"$REBALANCER_PROXY_AUTH_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"request": {"manifoldId": "rebalancer/flat/solver_run_12345"}}'
# => {"handle":{"manifoldId":"...","host":"...","port":...,"taskId":...}}
```

A follow-up call forwards the returned `handle` straight back as an argument:

```bash
curl -sS http://localhost:8081/v2/getProblemMetadataV2 \
  -H 'Authorization: Bearer '"$REBALANCER_PROXY_AUTH_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"handle": {"manifoldId":"...","host":"...","port":...,"taskId":...}}'
```

### `GET /healthz`

Unauthenticated liveness probe. Returns `200` with `{"status":"ok"}`.

## Flags

| Flag                   | Default       | Description                                                              |
| ---------------------- | ------------- | ----------------------------------------------------------------------- |
| `--port`               | `8081`        | Port the proxy listens on (bound on `0.0.0.0`).                         |
| `--backend_host`       | `127.0.0.1`   | Host of the Explorer Thrift backend.                                    |
| `--backend_port`       | `8080`        | Thrift port of the Explorer backend.                                    |
| `--auth_token`         | _(empty)_     | Bearer token required on `/v2/*`. Falls back to env var (see below).    |
| `--disable_auth`       | `false`       | Disable bearer-token auth entirely. **Dangerous**; local dev only.      |
| `--http_threads`       | `0`           | HTTP server threads. `0` ⇒ hardware concurrency.                        |
| `--backend_threads`    | `4`           | IO threads used to issue backend Thrift calls.                          |
| `--backend_timeout_ms` | `30000`       | Per-call backend timeout (connect + receive), in milliseconds.         |

The auth token is read from `--auth_token` first, then from the
`REBALANCER_PROXY_AUTH_TOKEN` environment variable. The proxy **fails closed**:
it refuses to start if no token is configured unless `--disable_auth` is passed.

## Security properties

- **Bearer-token auth** on every `/v2/*` request, compared in constant time to
  avoid leaking the token via response timing.
- **Fail closed**: the binary will not start without a token unless auth is
  explicitly disabled.
- **SSRF-safe**: the proxy always dials the single configured backend
  (`--backend_host` / `--backend_port`). The caller-supplied `Handle` is only
  ever forwarded as an RPC argument; it never influences the connection target.
- **No detail leaked**: error responses are generic JSON
  (`{"error": "..."}`). Backend transport/connection failures (which may embed
  host:port) are logged server-side only and returned as a generic
  `502 upstream request failed`; backend application errors are forwarded
  as-is. No stack traces or host details are returned to callers.

## Build & run (fbcode / Buck2)

Build:

```bash
buck2 build fbcode//rebalancer/explorer/proxy:rebalancer_explorer_proxy
```

Run against a local Explorer backend. First start a backend (see
`rebalancer/explorer/CLAUDE.md` for options), e.g. the production service
listening on a local Thrift port:

```bash
buck2 run @fbcode//mode/opt \
  fbcode//rebalancer/explorer/cpp_server/service:rebalancer_explorer_service \
  -- --dev_thrift_server_port=8080
```

Then run the proxy in front of it:

```bash
export REBALANCER_PROXY_AUTH_TOKEN="$(openssl rand -hex 32)"
buck2 run @fbcode//mode/opt \
  fbcode//rebalancer/explorer/proxy:rebalancer_explorer_proxy \
  -- --port=8081 --backend_host=127.0.0.1 --backend_port=8080
```

Smoke test:

```bash
curl -sS http://localhost:8081/healthz
# => {"status":"ok"}

curl -sS http://localhost:8081/v2/getHandle \
  -H 'Authorization: Bearer '"$REBALANCER_PROXY_AUTH_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"request": {"manifoldId": "<your_run_id>"}}'
```

> **Note on deployment.** This binary is the server side of the `/v2/<method>`
> contract spoken by the Explorer BFF's external transport. Wiring it up as a
> deployed service (Tupperware job / FBPKG, TLS termination, token distribution)
> is out of scope for this directory; coordinate with the algopt oncall before
> exposing it outside a devserver.
