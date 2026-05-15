// Confidence-chip parser. Pure function, no React. Mirrors the keyword
// vocabulary the LLM enhancer + system-prompt already use so that the chips
// reflect what the agent will most likely latch onto.
//
// Brand decision (Phase 24): chips are a MIRROR — display the exact user
// word, never normalise. The matching set is the synth's known vocabulary;
// the rendered value is whatever casing/spelling the user typed.

export interface ParsedIntent {
  genre?: string;
  role?: string;
  character: string[];
}

const GENRE = new Set([
  'dubstep', 'dub', 'techno', 'house', 'trance', 'ambient', 'drone',
  'trap', 'jungle', 'breakbeat', 'hardstyle', 'lofi', 'vaporwave', 'idm',
  'gabber', 'garage', 'footwork', 'dnb', 'neuro', 'riddim', 'acid',
  'minimal', 'industrial', 'cinematic', 'orchestral',
]);

const ROLE = new Set([
  'bass', 'sub', 'lead', 'pad', 'pluck', 'stab', 'drone', 'perc',
  'kick', 'snare', 'hat', 'riser', 'swell', 'texture', 'atmosphere',
  'arp', 'sequence', 'keys',
]);

const CHARACTER = new Set([
  'heavy', 'deep', 'dark', 'bright', 'warm', 'cold', 'sharp', 'soft',
  'wobbly', 'wobble', 'gritty', 'grit', 'snarl', 'snarly', 'smooth',
  'glassy', 'breathy', 'airy', 'dirty', 'clean', 'fat', 'thin', 'wide',
  'narrow', 'lush', 'hollow', 'tense', 'mellow', 'harsh', 'dreamy',
  'watery', 'metallic', 'plastic', 'vintage', 'modern', 'ominous',
  'menacing', 'predatory', 'evil', 'sweet', 'melancholy', 'hopeful',
  'sad', 'happy', 'aggressive', 'gentle', 'punchy', 'swelling',
  'evolving', 'throbbing', 'pulsing', 'breathing', 'vocal', 'growling',
  'screaming', 'whispering', 'super', 'huge', 'massive', 'tiny',
  'plucky',
  'ethereal', 'glitchy', 'shimmery', 'crunchy', 'fuzzy', 'gnarly',
  'angelic', 'demonic', 'cinematic', 'spacey', 'liquid', 'crystal',
]);

const WORD_RE = /[a-zA-Z]+/g;

export function parseIntent(prompt: string): ParsedIntent {
  const out: ParsedIntent = { character: [] };
  if (!prompt || prompt.trim().length < 2) return out;

  const seen = new Set<string>();
  const words = prompt.match(WORD_RE) ?? [];

  for (const raw of words) {
    const key = raw.toLowerCase();
    if (seen.has(key)) continue;
    seen.add(key);

    if (!out.genre && GENRE.has(key)) {
      out.genre = raw;
      continue;
    }
    if (!out.role && ROLE.has(key)) {
      out.role = raw;
      continue;
    }
    if (CHARACTER.has(key) && out.character.length < 4) {
      out.character.push(raw);
    }
  }
  return out;
}

export function intentIsEmpty(intent: ParsedIntent): boolean {
  return !intent.genre && !intent.role && intent.character.length === 0;
}
