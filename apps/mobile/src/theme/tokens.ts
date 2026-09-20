import type { TextStyle } from 'react-native';
import raw from './tokens.json';

type TokenJson = typeof raw;

export const tokens = raw as TokenJson;

export const colors = tokens.color;

export const space = {
  ...tokens.space,
  xs: tokens.space['1'], // 4
  sm: tokens.space['2'], // 8
  md: tokens.space['3'], // 12
  lg: tokens.space['4'], // 16
  xl: tokens.space['6'], // 24
};

export const radius = {
  ...tokens.radius,
  full: tokens.radius.pill, // 999
};

function toTextStyle(entry: { size: number; lineHeight: number; weight: number; tracking: number }): TextStyle {
  return {
    fontSize: entry.size,
    lineHeight: entry.lineHeight,
    fontWeight: String(entry.weight) as TextStyle['fontWeight'],
    letterSpacing: entry.tracking,
  };
}

export const typeScale = {
  ...tokens.type.scale,
  displayStyle: toTextStyle(tokens.type.scale.display),
  titleStyle: toTextStyle(tokens.type.scale.title),
  bodyStyle: toTextStyle(tokens.type.scale.body),
  bodyStrongStyle: toTextStyle(tokens.type.scale.bodyStrong),
  labelStyle: toTextStyle(tokens.type.scale.label),
  captionStyle: toTextStyle(tokens.type.scale.caption),
  macroLabelStyle: toTextStyle(tokens.type.scale.macroLabel),
};
