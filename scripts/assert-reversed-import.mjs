import { spawnSync } from 'node:child_process';
import { unlinkSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
// Must live inside the data sourceRoot and must NOT be gitignored /
// nxignored / eslint-ignored, or @nx/enforce-module-boundaries skips
// the file (CI then sees eslint exit 0). Unlinked in `finally`.
const probe = path.join(repoRoot, 'libs/data/src/lib/__boundary_probe__.ts');

let exitCode = 1;
try {
  // @nx/enforce-module-boundaries skips the file when no ProjectGraph
  // cache exists (CI after a fresh npm ci). Force Nx to write one.
  const graph = spawnSync('npx', ['nx', 'show', 'projects'], {
    cwd: repoRoot,
    encoding: 'utf8',
    env: { ...process.env, NX_DAEMON: 'false' },
  });
  if (graph.status !== 0) {
    console.error('nx show projects failed; cannot assert boundaries');
    console.error(`${graph.stdout ?? ''}${graph.stderr ?? ''}`);
    process.exit(1);
  }

  writeFileSync(probe, "import '../../../../apps/web/src/main.tsx';\n");
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
