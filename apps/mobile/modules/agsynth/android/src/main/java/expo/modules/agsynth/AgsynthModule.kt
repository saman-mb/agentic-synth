package expo.modules.agsynth

import expo.modules.kotlin.modules.Module
import expo.modules.kotlin.modules.ModuleDefinition

/** Stub until CMake/mobile JSI target is linked (#316 follow-up). */
class AgsynthModule : Module() {
  override fun definition() = ModuleDefinition {
    Name("Agsynth")

    Function("install") {
      // Returns null until native HostObject is attached.
      null
    }
  }
}
