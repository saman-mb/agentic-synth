// Cross-engine audio guarantee.
//
// Chromium, Gecko and WebKit are the only three engines behind every
// browser the demo targets (Chrome/Edge/Android, Firefox, Safari/iOS), so
// exercising all three here is what stops "works on my machine" audio
// regressions. The assertion is on rendered sample data, not on the
// absence of exceptions: an engine that starts cleanly and outputs
// silence is the exact failure this is here to catch.
import { after, before, describe, it } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const appRoot = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const START_TIMEOUT_MS = 60_000;
const MIN_PEAK = 0.01;

let vite;
let playwright;
let server;
let baseUrl;

before(async () => {
  vite = await import('vite');
  playwright = await import('playwright');
  server = await vite.createServer({
    root: appRoot,
    configFile: path.join(appRoot, 'vite.config.ts'),
    server: { port: 0, host: '127.0.0.1' },
    logLevel: 'error',
  });
  await server.listen();
  const { port } = server.httpServer.address();
  baseUrl = `http://127.0.0.1:${port}/e2e/harness.html`;
});

after(async () => {
  await server?.close();
});

describe('web demo audio', () => {
  for (const engineName of ['chromium', 'firefox', 'webkit']) {
    it(`renders audible output in ${engineName}`, async () => {
      const browser = await playwright[engineName].launch();
      try {
        const page = await browser.newPage();
        const pageErrors = [];
        page.on('pageerror', (err) => pageErrors.push(err.message));

        await page.goto(baseUrl);
        // A real click, so the autoplay policy lets the context start.
        await page.click('#start');
        await page.waitForFunction(() => globalThis.__HARNESS_RESULT__ !== undefined, null, {
          timeout: START_TIMEOUT_MS,
        });
        const result = await page.evaluate(() => globalThis.__HARNESS_RESULT__);

        assert.equal(result.error, undefined, `engine reported: ${result.error}`);
        assert.equal(result.sampleCount, 1024);
        assert.ok(
          result.peak > MIN_PEAK,
          `${engineName} produced silence (peak ${result.peak}); page errors: ${pageErrors.join('; ') || 'none'}`,
        );
        // Whichever engine won, the demo must know which one it is.
        assert.ok(
          result.kind === 'wasm' || result.kind === 'webaudio',
          `unexpected engine kind: ${result.kind}`,
        );
      } finally {
        await browser.close();
      }
    });
  }
});
