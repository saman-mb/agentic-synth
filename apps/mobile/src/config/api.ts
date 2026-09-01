const DEFAULT_API_BASE = 'https://timbra-synth.netlify.app';

/** Netlify-hosted brief + generate endpoints (#293 / #317). */
export function getApiBaseUrl(): string {
  if (typeof process !== 'undefined' && process.env.TAMBRA_API_BASE?.trim()) {
    return process.env.TAMBRA_API_BASE.trim().replace(/\/$/, '');
  }

  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const Constants = require('expo-constants').default as {
      expoConfig?: { extra?: { apiBaseUrl?: string } };
    };
    const fromExtra = Constants.expoConfig?.extra?.apiBaseUrl?.trim();
    if (fromExtra) return fromExtra.replace(/\/$/, '');
  } catch {
    // Node tests / no Expo runtime
  }

  return DEFAULT_API_BASE;
}

export function briefUrl(): string {
  return `${getApiBaseUrl()}/api/brief`;
}

export function generateUrl(): string {
  return `${getApiBaseUrl()}/api/generate`;
}
