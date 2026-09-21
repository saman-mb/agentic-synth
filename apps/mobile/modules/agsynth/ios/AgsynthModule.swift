import ExpoModulesCore
import AVFoundation

public class AgsynthModule: Module {
  public func definition() -> ModuleDefinition {
    Name("Agsynth")

    Function("install") { () -> Bool in
      do {
        let session = AVAudioSession.sharedInstance()
        try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
        try session.setActive(true)
      } catch {
        print("[Agsynth] AVAudioSession configuration warning: \(error)")
      }

      if let context = self.appContext {
        return AgsynthBridge.install(context)
      }
      return false
    }
  }
}
