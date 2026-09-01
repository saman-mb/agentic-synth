const path = require('path');
const { getDefaultConfig } = require('expo/metro-config');

const projectRoot = __dirname;
const workspaceRoot = path.resolve(projectRoot, '../..');

const config = getDefaultConfig(projectRoot);

config.watchFolders = [workspaceRoot];
config.resolver.nodeModulesPaths = [
  path.resolve(projectRoot, 'node_modules'),
  path.resolve(workspaceRoot, 'node_modules'),
];
config.resolver.disableHierarchicalLookup = true;
config.resolver.extraNodeModules = {
  '@agentic-synth/shared-types': path.resolve(workspaceRoot, 'libs/shared-types/src/index.ts'),
  '@agentic-synth/engine-bridge': path.resolve(workspaceRoot, 'libs/engine-bridge/src/index.ts'),
  '@agentic-synth/data': path.resolve(workspaceRoot, 'libs/data/src/index.ts'),
  '@agentic-synth/prompt': path.resolve(workspaceRoot, 'libs/prompt/src/index.ts'),
  '@agentic-synth/modval': path.resolve(workspaceRoot, 'libs/modval/src/index.ts'),
  '@agentic-synth/codec': path.resolve(workspaceRoot, 'libs/codec/src/index.ts'),
};

module.exports = config;
