import raw from './tokens.json';

type TokenJson = typeof raw;

export const tokens = raw as TokenJson;

export const colors = tokens.color;
export const space = tokens.space;
export const radius = tokens.radius;
export const typeScale = tokens.type.scale;
