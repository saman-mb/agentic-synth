/** User-facing HTTP errors — mirrors apps/web/src/demo/generateFlow.ts */
export function friendlyError(status: number, detail?: string): string {
  const suffix = detail ? ` (${detail})` : '';
  switch (status) {
    case 400:
      return `The prompt was rejected${suffix}. Try rephrasing it.`;
    case 429:
      return (
        detail ??
        'Rate limited — too many generations in a row. Wait a moment and try again.'
      );
    case 502:
      return 'Patch service unavailable — the upstream model could not be reached.';
    case 503:
      return (
        detail ??
        'Generation is not configured on this server. Try again later or use text with a shorter prompt.'
      );
    default:
      return `Generation failed (HTTP ${status}).${suffix}`;
  }
}
