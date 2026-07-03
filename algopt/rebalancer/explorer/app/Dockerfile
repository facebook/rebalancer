# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Multi-stage image for the Rebalancer Explorer Next.js app (the BFF).
#
# The build context is this directory (set by docker-compose). At runtime the
# app reaches the C++ backend through the JSON proxy: set REBALANCER_PROXY_URL
# (e.g. http://proxy:8081) and, if the proxy enforces auth, REBALANCER_PROXY_TOKEN.

ARG NODE_VERSION=22

FROM node:${NODE_VERSION}-slim AS builder
WORKDIR /app
ENV NEXT_TELEMETRY_DISABLED=1
# Playwright is a dev-only e2e dependency; its browsers aren't needed to build.
ENV PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1

# package.json's @nest/* (file:) deps don't resolve outside fbsource, so install
# from package.oss.json. yarn.lock is .dockerignored (pins the internal mirror);
# yarn regenerates a public-registry lock here.
COPY package.oss.json ./
RUN cp package.oss.json package.json \
 && yarn install --network-timeout 600000

COPY . .
# COPY . . brought the internal package.json back; restore the OSS one (yarn.lock
# is .dockerignored, so the regenerated public-registry lock is preserved).
RUN cp package.oss.json package.json
RUN yarn build
# Prune devDependencies so the runner ships production deps only.
RUN yarn install --production --frozen-lockfile --network-timeout 600000

FROM node:${NODE_VERSION}-slim AS runner
WORKDIR /app
ENV NODE_ENV=production
ENV NEXT_TELEMETRY_DISABLED=1

# next start re-evaluates next.config.ts and its local imports, so keep the source
# tree alongside .next and the pruned production node_modules.
COPY --from=builder /app ./

EXPOSE 3000
CMD ["yarn", "start"]
