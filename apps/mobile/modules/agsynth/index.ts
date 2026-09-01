/**
 * Expo native module stub for JSI host install().
 *
 * Real device builds link src/jsi/ (C++ AgsynthHost + AudioStream).
 * install() attaches global.__AgsynthHost — see tests/jsi-harness/README.md.
 *
 * Residual for EAS dev-client native compile (#316 follow-up on hardware).
 */
export function install(): boolean {
  // Native implementation provided by ios/ and android/ when linked.
  return false;
}
