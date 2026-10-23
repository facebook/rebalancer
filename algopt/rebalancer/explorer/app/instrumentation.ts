// OpenTelemetry instrumentation for Next.js
//
// This file is automatically loaded by Next.js on startup.
// Telemetry destinations are configured in .env via OTEL_RESOURCE_ATTRIBUTES:
//   • fb.traces.scuba.table (default: nest_traces)
//   • fb.metrics.scuba.table (default: nest_metrics)
//
// Telemetry is provided by the @platform/telemetry adapter: internally
// (NEST_INTERNAL=1) it wires @nest/otel; externally it is a no-op.
export async function register() {
  // Only load telemetry in the Node.js runtime (skip in the Edge runtime)
  if (
    process.env.NEXT_RUNTIME === 'nodejs' ||
    process.env.NEXT_RUNTIME === undefined
  ) {
    try {
      const {register: registerTelemetry} = await import('@platform/telemetry');
      await registerTelemetry();
    } catch (error) {
      console.warn(
        '[instrumentation] Failed to initialize telemetry, continuing without it:',
        error instanceof Error ? error.message : error,
      );
    }
  }
}
