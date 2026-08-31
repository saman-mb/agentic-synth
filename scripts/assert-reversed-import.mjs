import { spawnSync } from 'node:child_process';
import { unlinkSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const probe = path.join(repoRoot, 'libs/data/src/.boundary-probe.ts');

let exitCode = 1;
try {
  writeFileSync(probe, "import '../../../apps/web/src/main.tsx';\n");
  const result = spawnSync('npx', ['eslint', '--no-ignore', probe], {
    cwd: repoRoot,
    encoding: 'utf8',
  });
  const output = `${result.stdout ?? ''}${result.stderr ?? ''}`;
  const detected =
    result.status !== 0 && output.includes('enforce-module-boundaries');
  if (detected) {
    exitCode = 0;
  } else {
    console.error(
      'Expected eslint to fail the reversed-import probe with enforce-module-boundaries',
    );
    console.error(`eslint exit: ${result.status}`);
    console.error(output);
  }
} finally {
  try {
    unlinkSync(probe);
  } catch {
    // probe may not have been written
  }
}

process.exit(exitCode);
