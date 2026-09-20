import React, { useEffect } from 'react';
import { View, Text, StyleSheet, FlatList } from 'react-native';
import Animated, { useAnimatedStyle, withRepeat, withTiming, useSharedValue, withSequence, Easing } from 'react-native-reanimated';
import { PatchCard } from './PatchCard';
import type { ChatMessage } from '../state/mobileState';
import { colors, radius, space, typeScale } from '../theme/tokens';

export interface ChatThreadProps {
  messages: ChatMessage[];
  activePatchCardId: string | null;
  isGenerating: boolean;
  opacity: number;  // 0–1, controlled by parent (fades when playing)
  onMacroChange: (messageId: string, index: number, value: number) => void;
  onActivatePatch: (messageId: string) => void;
  onSavePatch: (messageId: string) => void;
}

const TypingIndicator = () => {
  const dot1 = useSharedValue(0.3);
  const dot2 = useSharedValue(0.3);
  const dot3 = useSharedValue(0.3);

  useEffect(() => {
    const animateDot = (dot: Animated.SharedValue<number>, delay: number) => {
      setTimeout(() => {
        dot.value = withRepeat(
          withSequence(
            withTiming(1, { duration: 400, easing: Easing.inOut(Easing.ease) }),
            withTiming(0.3, { duration: 400, easing: Easing.inOut(Easing.ease) })
          ),
          -1,
          true
        );
      }, delay);
    };

    animateDot(dot1, 0);
    animateDot(dot2, 200);
    animateDot(dot3, 400);
  }, []);

  return (
    <View style={styles.typingContainer}>
      <Animated.View style={[styles.dot, { opacity: dot1 }]} />
      <Animated.View style={[styles.dot, { opacity: dot2 }]} />
      <Animated.View style={[styles.dot, { opacity: dot3 }]} />
    </View>
  );
};

export const ChatThread: React.FC<ChatThreadProps> = ({
  messages,
  activePatchCardId,
  isGenerating,
  opacity,
  onMacroChange,
  onActivatePatch,
  onSavePatch,
}) => {
  
  const containerStyle = useAnimatedStyle(() => {
    return {
      opacity: opacity,
    };
  });

  const renderItem = ({ item }: { item: ChatMessage }) => {
    if (item.role === 'system') {
      return (
        <View style={styles.systemMessage}>
          <Text style={styles.systemText}>{item.text}</Text>
        </View>
      );
    }

    const isUser = item.role === 'user';
    
    return (
      <View style={[styles.messageWrapper, isUser ? styles.messageWrapperUser : styles.messageWrapperAgent]}>
        {item.text ? (
          <View style={[styles.bubble, isUser ? styles.bubbleUser : styles.bubbleAgent]}>
            <Text style={[styles.messageText, isUser ? styles.messageTextUser : styles.messageTextAgent]}>
              {item.text}
            </Text>
          </View>
        ) : null}
        {item.patchCard && (
          <PatchCard 
            macros={item.patchCard.macros}
            isActive={item.id === activePatchCardId}
            onMacroChange={(index, value) => onMacroChange(item.id, index, value)}
            onActivate={() => onActivatePatch(item.id)}
            onSave={() => onSavePatch(item.id)}
          />
        )}
      </View>
    );
  };

  return (
    <Animated.View style={[styles.container, containerStyle]}>
      <FlatList
        data={messages}
        keyExtractor={(item) => item.id}
        renderItem={renderItem}
        inverted
        showsVerticalScrollIndicator={false}
        contentContainerStyle={styles.listContent}
        ListHeaderComponent={isGenerating ? (
          <View style={[styles.messageWrapper, styles.messageWrapperAgent]}>
            <View style={[styles.bubble, styles.bubbleAgent]}>
              <TypingIndicator />
            </View>
          </View>
        ) : null}
      />
    </Animated.View>
  );
};

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: 'transparent',
  },
  listContent: {
    padding: space.md,
    flexGrow: 1,
  },
  messageWrapper: {
    marginBottom: space.md,
    maxWidth: '85%',
  },
  messageWrapperUser: {
    alignSelf: 'flex-end',
  },
  messageWrapperAgent: {
    alignSelf: 'flex-start',
  },
  bubble: {
    padding: space.md,
    borderRadius: radius.lg,
  },
  bubbleUser: {
    backgroundColor: colors.bg.chatBubbleUser,
    borderBottomRightRadius: radius.sm,
  },
  bubbleAgent: {
    backgroundColor: colors.bg.chatBubbleAgent,
    borderBottomLeftRadius: radius.sm,
  },
  systemMessage: {
    alignItems: 'center',
    marginVertical: space.md,
  },
  systemText: {
    ...typeScale.body,
    color: colors.text.tertiary,
    fontStyle: 'italic',
  },
  messageText: {
    ...typeScale.body,
  },
  messageTextUser: {
    color: colors.text.primary,
  },
  messageTextAgent: {
    color: colors.text.primary,
  },
  typingContainer: {
    flexDirection: 'row',
    alignItems: 'center',
    height: 20,
    width: 40,
    justifyContent: 'space-between',
    paddingHorizontal: space.xs,
  },
  dot: {
    width: 6,
    height: 6,
    borderRadius: 3,
    backgroundColor: colors.text.primary,
  },
});
