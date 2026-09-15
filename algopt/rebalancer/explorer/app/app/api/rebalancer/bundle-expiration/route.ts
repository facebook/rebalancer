import {NextRequest, NextResponse} from 'next/server';
import {z} from 'zod';

import {extendBundleExpiration, getBundleExpiration} from '@/lib/client/handle';
import {createLogger} from '@platform/telemetry';
import {
  getExpirationExtensionSecondsFromNow,
  isValidExpirationDays,
} from '@/lib/rebalancer-explorer-api';
import type {
  BundleExpirationResponse,
  Handle,
} from '@/lib/rebalancer-explorer-types';

const MAX_BODY_BYTES = 4096;
const ROUTE = '/api/rebalancer/bundle-expiration';
const logger = createLogger('api.rebalancer.bundle-expiration');

function toError(caught: unknown): Error {
  return caught instanceof Error ? caught : new Error(String(caught));
}

const requestSchema = z
  .object({
    handle: z.object({
      manifoldId: z.string().min(1),
      host: z.string().min(1),
      port: z.number().int().positive(),
      taskId: z.number().int(),
    }),
    extensionDays: z.number().int().min(0),
  })
  .refine(({extensionDays}) => isValidExpirationDays(extensionDays));

const handleQuerySchema = z.object({
  manifoldId: z.string().min(1),
  host: z.string().min(1),
  port: z.coerce.number().int().positive(),
  taskId: z.coerce.number().int(),
});

async function extendExpirationByDays(
  handle: Handle,
  extensionDays: number,
): Promise<BundleExpirationResponse | null> {
  const {expiresAt} = await getBundleExpiration(handle);
  const secondsFromNow = getExpirationExtensionSecondsFromNow(
    expiresAt,
    extensionDays,
  );
  return secondsFromNow == null
    ? null
    : extendBundleExpiration(handle, secondsFromNow);
}

export async function GET(request: NextRequest) {
  const parsed = handleQuerySchema.safeParse(
    Object.fromEntries(request.nextUrl.searchParams),
  );
  if (!parsed.success) {
    return NextResponse.json({error: 'Invalid request'}, {status: 400});
  }

  try {
    return NextResponse.json(await getBundleExpiration(parsed.data), {
      headers: {'Cache-Control': 'no-store'},
    });
  } catch (caught) {
    logger.error('Failed to fetch bundle expiration', toError(caught), {
      method: 'GET',
      route: ROUTE,
    });
    return NextResponse.json(
      {error: 'Failed to fetch expiration'},
      {status: 500},
    );
  }
}

export async function POST(request: NextRequest) {
  if (request.headers.get('X-Requested-With') !== 'fetch') {
    return NextResponse.json({error: 'CSRF validation failed'}, {status: 403});
  }

  const contentLength = Number(request.headers.get('content-length') ?? 0);
  if (contentLength > MAX_BODY_BYTES) {
    return NextResponse.json(
      {error: 'Request body is too large'},
      {status: 413},
    );
  }

  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({error: 'Invalid JSON body'}, {status: 400});
  }

  const parsed = requestSchema.safeParse(body);
  if (!parsed.success) {
    return NextResponse.json({error: 'Invalid request'}, {status: 400});
  }

  const {handle, extensionDays} = parsed.data;
  try {
    const response = await extendExpirationByDays(handle, extensionDays);
    if (response == null) {
      return NextResponse.json({error: 'Invalid request'}, {status: 400});
    }
    return NextResponse.json(response);
  } catch (caught) {
    logger.error('Failed to extend bundle expiration', toError(caught), {
      method: 'POST',
      route: ROUTE,
    });
    return NextResponse.json(
      {error: 'Failed to extend expiration'},
      {status: 500},
    );
  }
}
