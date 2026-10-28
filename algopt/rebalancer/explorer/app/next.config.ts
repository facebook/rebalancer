import type {NextConfig} from 'next';
import path from 'path';
// OSS base config. Imported statically (relative) so Next's config loader never
// needs to resolve @nest/next-core in the OSS build.
import {baseConfig as ossBaseConfig} from './lib/platform/next-config';

// Internal-build detection. The platform seam is redirected to the @nest/*-backed
// adapters under lib/platform/fb/ only for the internal (fbsource) build.
// `nest dev` / `nest build` don't set an env var we can rely on, and the OSS
// export strips @nest/* from package.json — so the resolvability of @nest/next-core
// is the signal that this is the internal build. NEST_INTERNAL=1 forces it on as
// an explicit override. Externally neither holds, so the OSS adapters in
// lib/platform/* are used (resolved via tsconfig `paths`) and @nest/* is never
// referenced anywhere in the build graph.
function detectInternal(): boolean {
  if (process.env.NEST_INTERNAL === '1') {
    return true;
  }
  try {
    require.resolve('@nest/next-core');
    return true;
  } catch {
    return false;
  }
}

const isInternal = detectInternal();

// Internally use @nest/next-core's baseConfig (via the internal adapter, so no
// @nest/* string appears here); externally use the minimal OSS baseConfig.
const baseConfig: NextConfig = isInternal
  ? require('./lib/platform/fb/next-config').baseConfig
  : ossBaseConfig;

const PLATFORM_MODULES = [
  'auth',
  'auth-middleware',
  'relay',
  'graphql',
  'transport',
  'telemetry',
  'assistant-ui',
  'internal-links',
  'stickiness',
];

// Turbopack resolves alias targets relative to the project dir, so it needs
// project-relative paths; webpack wants absolute paths. Build both maps.
const turbopackPlatformAlias: Record<string, string> = isInternal
  ? Object.fromEntries(
      PLATFORM_MODULES.map(name => [
        `@platform/${name}`,
        `./lib/platform/fb/${name}`,
      ]),
    )
  : {};

const webpackPlatformAlias: Record<string, string> = isInternal
  ? Object.fromEntries(
      PLATFORM_MODULES.map(name => [
        `@platform/${name}`,
        path.join(__dirname, 'lib/platform/fb', name),
      ]),
    )
  : {};

const webpackFn: NonNullable<NextConfig['webpack']> = config => {
  config.resolve = config.resolve || {};
  config.resolve.alias = {
    ...(config.resolve.alias || {}),
    ...webpackPlatformAlias,
  };
  return config;
};

const nextConfig: NextConfig = {
  ...baseConfig,
  experimental: {...baseConfig.experimental, useTypeScriptCli: false},
  transpilePackages: [
    ...(baseConfig.transpilePackages ?? []),
    ...(isInternal ? ['@nest/eps'] : []),
  ],
  turbopack: {
    root: path.join(__dirname, '../..'),
    ...(isInternal ? {resolveAlias: turbopackPlatformAlias} : {}),
  },
  ...(isInternal ? {webpack: webpackFn} : {}),
  compiler: {
    relay: {
      src: './',
      artifactDirectory: './lib/components/__generated__/',
      language: 'typescript',
    },
  },
  // add config overrides here
};

export default nextConfig;
