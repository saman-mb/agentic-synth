// Sanitizer — parity with src/mapper/PromptSanitizer.cpp
//
// Order matches the C++ kMap table (longer keys first, though all keys
// here are disjoint so ordering is not load-bearing in JS either).
const WORD_MAP: ReadonlyArray<readonly [string, string]> = [
  ['menacing', 'intense'],
  ['violent', 'aggressive'],
  ['horror', 'uneasy'],
  ['scary', 'unsettling'],
  ['death', 'ending'],
  ['dread', 'tension'],
  ['evil', 'dark'],
  ['kill', 'drop'],
];

// CRITICAL: the C++ boundary is isalnum, NOT \b — JS \b counts "_" as a
// word character, so "kill_joy" would not match with \b but does in C++.
// Lookarounds replicate the isalnum boundary exactly.
function wordRegex(word: string): RegExp {
  return new RegExp(`(?<![A-Za-z0-9])${word}(?![A-Za-z0-9])`, 'gi');
}

// Preserve the FIRST character case from the input (C++ apply_case):
// "Horror" → "Uneasy", "HORROR" → "Uneasy" (not "UNEASY").
function matchCase(replacement: string, sampled: string): string {
  const first = sampled.charAt(0);
  if (first >= 'A' && first <= 'Z') {
    return replacement.charAt(0).toUpperCase() + replacement.slice(1);
  }
  return replacement;
}

export function sanitizePrompt(prompt: string): string {
  let out = prompt;
  for (const [from, to] of WORD_MAP) {
    out = out.replace(wordRegex(from), (sampled) => matchCase(to, sampled));
  }
  return out;
}
