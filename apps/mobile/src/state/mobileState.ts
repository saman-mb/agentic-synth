/** Canonical MobileState enum — see docs/mobile/state-machine.md */
export type MobileState =
  | 'idle'
  | 'say'
  | 'hear'
  | 'shape'
  | 'variations'
  | 'keep'
  | 'error';

export interface MobileSession {
  state: MobileState;
  returnState?: MobileState;
  statusMessage: string;
  isPlaying: boolean;
}

export const INITIAL_SESSION: MobileSession = {
  state: 'idle',
  statusMessage: '',
  isPlaying: false,
};
