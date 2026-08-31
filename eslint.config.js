import { createRequire } from 'node:module';
import js from '@eslint/js';
import tsParser from '@typescript-eslint/parser';
import tsPlugin from '@typescript-eslint/eslint-plugin';
import globals from 'globals';
import react from 'eslint-plugin-react';
import reactHooks from 'eslint-plugin-react-hooks';

const require = createRequire(import.meta.url);
const nx = require('@nx/eslint-plugin');

const nxModuleBoundaries = [
  'error',
  {
    enforceBuildableLibDependency: false,
    allow: ['^.*/eslint(\\.base)?\\.config\\.[cm]?[jt]s$'],
    depConstraints: [
      { sourceTag: 'type:app', onlyDependOnLibsWithTags: ['type:lib'] },
      { sourceTag: 'layer:types', onlyDependOnLibsWithTags: ['layer:types'] },
      { sourceTag: 'layer:data', onlyDependOnLibsWithTags: ['layer:types', 'layer:data'] },
      { sourceTag: 'layer:engine', onlyDependOnLibsWithTags: ['layer:types', 'layer:data', 'layer:engine'] },
    ],
  },
];

export default [
  {
    ignores: [
      'build/**',
      'dist/**',
      'node_modules/**',
      'third_party/**',
      'apps/web/dist/**',
      'apps/web/node_modules/**',
    ],
  },
  js.configs.recommended,
  {
    files: ['apps/web/**/*.{js,jsx,ts,tsx}', 'libs/**/*.{js,jsx,ts,tsx}'],
    languageOptions: {
      ecmaVersion: 'latest',
      globals: globals.browser,
      parser: tsParser,
      parserOptions: {
        ecmaFeatures: {
          jsx: true,
        },
        sourceType: 'module',
      },
    },
    plugins: {
      '@typescript-eslint': tsPlugin,
      '@nx': nx,
    },
    rules: {
      ...tsPlugin.configs.recommended.rules,
      // TypeScript already checks undefined names; no-undef false-positives
      // on React namespace and DOM lib types (typescript-eslint FAQ).
      'no-undef': 'off',
      '@typescript-eslint/no-unused-vars': [
        'warn',
        { argsIgnorePattern: '^_', varsIgnorePattern: '^_' },
      ],
      '@nx/enforce-module-boundaries': nxModuleBoundaries,
    },
  },
  {
    files: ['apps/web/**/*.{js,jsx,ts,tsx}'],
    plugins: {
      react,
      'react-hooks': reactHooks,
    },
    rules: {
      ...react.configs.recommended.rules,
      'react/react-in-jsx-scope': 'off',
      'react/no-unescaped-entities': 'off',
      'react/prop-types': 'off',
    },
    settings: {
      react: {
        version: 'detect',
      },
    },
  },
];
