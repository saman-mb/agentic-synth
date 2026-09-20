import ExpoModulesCore

// Stub: returns nil until C++ JSI host is linked (#316 follow-up).
public class AgsynthModule: Module {
  public func definition() -> ModuleDefinition {
    Name("Agsynth")

    Function("install") { () -> [String: Any]? in
      // Native JSI binding will be returned as a HostObject when wired.
      return nil
    }
  }
}
