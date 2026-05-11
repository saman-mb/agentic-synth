#include "MainComponent.h"
#include "agent/AgentBridge.h"

#include <juce_gui_extra/juce_gui_extra.h>

//==============================================================================
// Phase 4: one AgentBridge per process for the standalone app; passed by
// reference into the MainWindow → MainComponent → WebUiComponent.
class AgenticSynthApplication : public juce::JUCEApplication {
public:
    AgenticSynthApplication() = default;

    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& /*commandLine*/) override {
        bridge_ = std::make_unique<agentic_synth::agent::AgentBridge>();
        mainWindow = std::make_unique<MainWindow>(getApplicationName(), *bridge_);
    }

    void shutdown() override {
        mainWindow = nullptr;
        bridge_ = nullptr;
    }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String& /*commandLine*/) override {}

    //==============================================================================
    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(juce::String name, agentic_synth::agent::AgentBridge& bridge)
            : DocumentWindow(std::move(name),
                             juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                 juce::ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(bridge), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<agentic_synth::agent::AgentBridge> bridge_;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(AgenticSynthApplication)
