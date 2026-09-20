import React from 'react';
import {
  KeyboardAvoidingView,
  Platform,
  StyleSheet,
  Text,
  TextInput,
  TouchableOpacity,
} from 'react-native';
import Animated, { useAnimatedStyle, withTiming } from 'react-native-reanimated';
import { colors, radius, space, typeScale } from '../theme/tokens';

export interface PromptBarProps {
  onSend: (text: string) => void;
  onMicPress: () => void;
  isRecording: boolean;
  micDisabled: boolean;
  isGenerating: boolean;
  opacity: number;
}

export const PromptBar: React.FC<PromptBarProps> = ({
  onSend,
  onMicPress,
  isRecording,
  micDisabled,
  isGenerating,
  opacity,
}) => {
  const [text, setText] = React.useState('');

  const handleSend = () => {
    if (!text.trim() || isGenerating) return;
    onSend(text);
    setText('');
  };

  const animatedStyle = useAnimatedStyle(() => ({
    opacity: withTiming(opacity, { duration: 150 }),
  }));

  return (
    <KeyboardAvoidingView
      behavior={Platform.OS === 'ios' ? 'padding' : undefined}
      style={styles.keyboardView}
    >
      <Animated.View style={[styles.container, animatedStyle]}>
        <TouchableOpacity
          style={[
            styles.micButton,
            isRecording && styles.micButtonRecording,
            micDisabled && styles.micButtonDisabled,
          ]}
          onPress={onMicPress}
          disabled={micDisabled}
          accessibilityLabel="Microphone"
        >
          <Text style={[styles.micIcon, isRecording && styles.micIconRecording]}>
            {isRecording ? '●' : '🎙'}
          </Text>
        </TouchableOpacity>
        
        <TextInput
          style={styles.input}
          placeholder="Describe a sound..."
          placeholderTextColor={colors.text.tertiary}
          value={text}
          onChangeText={setText}
          onSubmitEditing={handleSend}
          returnKeyType="send"
        />

        <TouchableOpacity
          style={[
            styles.sendButton,
            (text.trim().length === 0 || isGenerating) && styles.sendButtonDisabled,
          ]}
          onPress={handleSend}
          disabled={text.trim().length === 0 || isGenerating}
          accessibilityLabel="Send"
        >
          <Text style={styles.sendIcon}>↑</Text>
        </TouchableOpacity>
      </Animated.View>
    </KeyboardAvoidingView>
  );
};

const styles = StyleSheet.create({
  keyboardView: {
    width: '100%',
  },
  container: {
    flexDirection: 'row',
    alignItems: 'center',
    padding: space['3'],
    gap: space['2'],
    backgroundColor: 'transparent',
  },
  micButton: {
    width: 44,
    height: 44,
    borderRadius: 22,
    backgroundColor: colors.bg.raised,
    alignItems: 'center',
    justifyContent: 'center',
    borderWidth: 2,
    borderColor: 'transparent',
  },
  micButtonRecording: {
    borderColor: colors.accent.record,
  },
  micButtonDisabled: {
    opacity: 0.5,
  },
  micIcon: {
    fontSize: 20,
    color: colors.text.primary,
  },
  micIconRecording: {
    color: colors.accent.record,
  },
  input: {
    flex: 1,
    backgroundColor: colors.bg.inset,
    color: colors.text.primary,
    borderRadius: radius.md,
    padding: space['3'],
    fontSize: typeScale.body,
  },
  sendButton: {
    width: 44,
    height: 44,
    borderRadius: 22,
    backgroundColor: colors.accent.primary,
    alignItems: 'center',
    justifyContent: 'center',
  },
  sendButtonDisabled: {
    backgroundColor: colors.bg.raised,
    opacity: 0.5,
  },
  sendIcon: {
    fontSize: 24,
    color: colors.text.inverse,
    fontWeight: 'bold',
  },
});
