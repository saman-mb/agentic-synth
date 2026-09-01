import { Audio } from 'expo-av';
import * as Linking from 'expo-linking';

export type CaptureSubstate = 'idle' | 'recording' | 'transcribing' | 'denied';

export type MicPermission = 'granted' | 'denied' | 'undetermined';

export async function queryMicPermission(): Promise<MicPermission> {
  const { status } = await Audio.getPermissionsAsync();
  if (status === 'granted') return 'granted';
  if (status === 'denied') return 'denied';
  return 'undetermined';
}

export async function requestMicPermission(): Promise<MicPermission> {
  const { status } = await Audio.requestPermissionsAsync();
  if (status === 'granted') return 'granted';
  if (status === 'denied') return 'denied';
  return 'undetermined';
}

export function openMicSettings(): void {
  void Linking.openSettings();
}

/** Tap-to-record STT hook — inject mock in tests (#317, docs/mobile/input.md). */
export interface SpeechTranscriber {
  start(): Promise<void>;
  stop(): Promise<string>;
  cancel(): Promise<void>;
}

let recording: Audio.Recording | null = null;

/** Default transcriber: expo-av record + placeholder STT until cloud STT ships. */
export function createDefaultTranscriber(
  onTranscript?: (text: string) => string,
): SpeechTranscriber {
  return {
    async start() {
      await Audio.setAudioModeAsync({
        allowsRecordingIOS: true,
        playsInSilentModeIOS: true,
      });
      const rec = new Audio.Recording();
      await rec.prepareToRecordAsync(Audio.RecordingOptionsPresets.HIGH_QUALITY);
      await rec.startAsync();
      recording = rec;
    },
    async stop() {
      if (!recording) return '';
      try {
        await recording.stopAndUnloadAsync();
        const uri = recording.getURI();
        recording = null;
        if (!uri) return '';
        // v1: no cloud STT wired — return calm empty so user edits text (input.md soft-fail)
        const fallback = onTranscript?.(uri) ?? '';
        return fallback;
      } finally {
        recording = null;
      }
    },
    async cancel() {
      if (!recording) return;
      try {
        await recording.stopAndUnloadAsync();
      } catch {
        // already stopped
      }
      recording = null;
    },
  };
}
