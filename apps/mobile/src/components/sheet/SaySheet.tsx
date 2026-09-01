import {
  Linking,
  Pressable,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';

import type { useSayCapture } from '../hooks/useSayCapture';
import { colors, radius, space, typeScale } from '../theme/tokens';

type SayCapture = ReturnType<typeof useSayCapture>;

export interface SaySheetProps {
  capture: SayCapture;
  onSend: () => void;
  onCancel: () => void;
  generating?: boolean;
}

export function SaySheet({ capture, onSend, onCancel, generating = false }: SaySheetProps) {
  const {
    draftText,
    setDraft,
    tapRecord,
    cancelCapture,
    canSend,
    isRecording,
    micDisabled,
    statusLine,
    substate,
  } = capture;

  return (
    <View style={styles.root} accessibilityLabel="Say sheet">
      <View style={styles.grabber} />
      <Text style={styles.title}>Describe a sound</Text>
      <TextInput
        style={styles.input}
        value={draftText}
        onChangeText={setDraft}
        placeholder="warm pad with movement…"
        placeholderTextColor={colors.text.tertiary}
        multiline
        editable={!generating}
        accessibilityLabel="Sound description"
      />
      <View style={styles.row}>
        <Pressable
          style={[styles.mic, micDisabled && styles.micDisabled, isRecording && styles.micActive]}
          onPress={tapRecord}
          disabled={generating || micDisabled}
          accessibilityRole="button"
          accessibilityLabel={isRecording ? 'Stop recording' : 'Tap to record'}
        >
          <Text style={styles.micIcon}>{isRecording ? '■' : '🎙'}</Text>
        </Pressable>
        {micDisabled && (
          <Pressable
            onPress={() => void Linking.openSettings()}
            accessibilityRole="button"
            accessibilityLabel="Open settings"
          >
            <Text style={styles.link}>Open settings</Text>
          </Pressable>
        )}
        {substate === 'transcribing' && (
          <Text style={styles.hint}>Getting that…</Text>
        )}
      </View>
      {statusLine ? <Text style={styles.status}>{statusLine}</Text> : null}
      <View style={styles.actions}>
        <Pressable
          style={styles.secondary}
          onPress={() => {
            void cancelCapture();
            onCancel();
          }}
          disabled={generating}
          accessibilityRole="button"
        >
          <Text style={styles.secondaryText}>Cancel</Text>
        </Pressable>
        <Pressable
          style={[styles.primary, (!canSend || generating) && styles.primaryDisabled]}
          onPress={onSend}
          disabled={!canSend || generating}
          accessibilityRole="button"
          accessibilityLabel="Send"
        >
          <Text style={styles.primaryText}>{generating ? 'Building…' : 'Send'}</Text>
        </Pressable>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    backgroundColor: colors.bg.sheet,
    borderTopLeftRadius: radius.sheet,
    borderTopRightRadius: radius.sheet,
    paddingHorizontal: space.chromePadX,
    paddingTop: space['3'],
    paddingBottom: space.safeBottomMin,
    borderTopWidth: 1,
    borderColor: colors.border.subtle,
  },
  grabber: {
    alignSelf: 'center',
    width: 40,
    height: 4,
    borderRadius: 2,
    backgroundColor: colors.border.strong,
    marginBottom: space['3'],
  },
  title: {
    color: colors.text.primary,
    fontSize: typeScale.title.size,
    fontWeight: typeScale.title.weight as '600',
    marginBottom: space['2'],
  },
  input: {
    minHeight: 72,
    borderRadius: radius.md,
    borderWidth: 1,
    borderColor: colors.border.subtle,
    backgroundColor: colors.bg.inset,
    color: colors.text.primary,
    fontSize: typeScale.body.size,
    padding: space['3'],
    textAlignVertical: 'top',
  },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: space['3'],
    marginTop: space['3'],
  },
  mic: {
    width: 48,
    height: 48,
    borderRadius: radius.pill,
    backgroundColor: colors.bg.raised,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 1,
    borderColor: colors.border.subtle,
  },
  micDisabled: {
    opacity: 0.45,
  },
  micActive: {
    borderColor: colors.accent.record,
  },
  micIcon: {
    fontSize: 20,
  },
  link: {
    color: colors.accent.primary,
    fontSize: typeScale.label.size,
  },
  hint: {
    color: colors.text.secondary,
    fontSize: typeScale.caption.size,
  },
  status: {
    color: colors.text.tertiary,
    fontSize: typeScale.caption.size,
    marginTop: space['2'],
  },
  actions: {
    flexDirection: 'row',
    justifyContent: 'flex-end',
    gap: space['3'],
    marginTop: space['4'],
  },
  secondary: {
    paddingHorizontal: space['4'],
    paddingVertical: space['3'],
  },
  secondaryText: {
    color: colors.text.secondary,
    fontSize: typeScale.body.size,
  },
  primary: {
    paddingHorizontal: space['5'],
    paddingVertical: space['3'],
    borderRadius: radius.pill,
    backgroundColor: colors.accent.primary,
  },
  primaryDisabled: {
    opacity: 0.5,
  },
  primaryText: {
    color: colors.text.onAccent,
    fontSize: typeScale.bodyStrong.size,
    fontWeight: typeScale.bodyStrong.weight as '600',
  },
});
