import type { ExpoConfig } from 'expo/config';

const config: ExpoConfig = {
  name: 'Tambra',
  slug: 'tambra-mobile',
  version: '0.0.1',
  orientation: 'portrait',
  scheme: 'tambra',
  userInterfaceStyle: 'dark',
  newArchEnabled: true,
  ios: {
    supportsTablet: false,
    bundleIdentifier: 'com.tambra.mobile',
  },
  android: {
    package: 'com.tambra.mobile',
    adaptiveIcon: {
      backgroundColor: '#0A0B0F',
    },
  },
  plugins: [
    'expo-router',
    [
      'expo-av',
      {
        microphonePermission:
          'Tambra uses the microphone to capture sound descriptions you speak.',
      },
    ],
  ],
  experiments: {
    typedRoutes: true,
  },
  extra: {
    apiBaseUrl: 'https://timbra-synth.netlify.app',
    eas: {
      projectId: 'tambra-mobile-v1',
    },
  },
};

export default config;
