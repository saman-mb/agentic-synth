# Contributing

Thanks for contributing to Agentic Synth. This guide covers the local workflow, coding standards, and review expectations for changes to the C++ engine, agent bridge, and React companion UI.

## Code of Conduct

Be respectful, constructive, and specific. Assume good intent, avoid personal attacks, and keep technical discussions focused on the behavior, maintainability, and user impact of the work.

## Reporting Issues

Before opening an issue, search existing issues to avoid duplicates. Include:

- A clear summary of the problem or request.
- Steps to reproduce bugs, including platform, compiler, Node, and CMake versions where relevant.
- Expected behavior and actual behavior.
- Logs, screenshots, crash output, or minimal examples when they help explain the issue.
- Any workaround you found.

Security-sensitive reports should not be posted publicly. Contact a maintainer privately so the issue can be triaged responsibly.

## Development Setup

Install the core toolchain:

- CMake 3.24 or newer.
- A C++ compiler with C++20 support for the current build, even though formatting is configured for C++17 syntax compatibility.
- clang-format for local formatting checks.
- Node.js 20 or newer and npm for UI work.
- Python 3 with `pre-commit` installed.

Set up hooks after cloning:

```sh
python -m pip install pre-commit
pre-commit install --install-hooks
pre-commit install --hook-type commit-msg
```

Build and test the C++ project:

```sh
cmake -S . -B build -DAGENTIC_SYNTH_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Install and lint the UI:

```sh
cd ui
npm ci
npm run lint
```

Run all configured pre-commit checks before opening a pull request:

```sh
pre-commit run --all-files
```

## Coding Standards

C++ code lives under `src/` and `tests/`. Format it with clang-format using the root `.clang-format` file. Keep changes small, prefer clear ownership and value semantics, and avoid formatting vendored code under `third_party/`.

TypeScript and React code lives under `ui/`. Lint it with ESLint using the root flat config and the UI package scripts. Prefer typed, explicit component boundaries and keep browser-specific logic isolated from reusable UI code.

YAML, Markdown, JavaScript, TypeScript, and C++ files should not contain trailing whitespace and should end with a single newline. The pre-commit hooks fix these automatically where possible.

## Commit Messages

Use Conventional Commits:

```text
type(scope): short imperative summary
```

Common types include `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `build`, and `chore`. Examples:

```text
feat(ui): add patch preview panel
fix(engine): clamp oscillator gain
docs: document pre-commit workflow
```

Use the body to explain why a change is needed when the summary is not enough. Commitlint checks commit messages through the `commit-msg` hook.

## Pull Request Process

Keep pull requests focused on one behavior change or maintenance task. Each PR should include:

- A concise summary of the change and why it is needed.
- Related issue links, when applicable.
- Test or verification notes, including commands run.
- Screenshots or recordings for visible UI changes.
- Migration notes for build, configuration, or developer workflow changes.

Before requesting review, make sure CMake tests, UI linting, and `pre-commit run --all-files` pass locally. Respond to review comments with either a code update or a clear explanation of the tradeoff.
