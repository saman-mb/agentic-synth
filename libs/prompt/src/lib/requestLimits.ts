/**
 * Shared request body size cap for /api/brief and /api/generate (#311).
 *
 * Prompt (2k) + brief (4k) + JSON envelope fit well under this; oversized
 * payloads are rejected before Gemini spend. Measured in UTF-8 bytes.
 */
export const MAX_REQUEST_BODY_BYTES = 32_768;
