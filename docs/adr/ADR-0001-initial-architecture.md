# ADR-0001: Use JUCE 7 as Audio Plugin Framework

## Status

Accepted

## Date

2024-01-15

## Deciders

- Agentic Synth maintainers

## Context and Problem Statement

Agentic Synth needs to ship as a cross-platform audio plugin and standalone application. The project must support VST3 and AU plugin formats, integrate with DAW-hosted audio and MIDI callbacks, and provide a path to a standalone mode for development, testing, and users who do not want to run inside a DAW.

The framework choice must support low-latency real-time audio constraints while leaving room for a separate agent runtime and a React-based companion interface.

## Decision Drivers

- Cross-platform support for macOS, Windows, and Linux-oriented development workflows.
- Mature plugin API for VST3, AU, and standalone application targets.
- License compatibility with the intended project and distribution model.
- Active development, documentation, and community support.
- Practical CMake integration for CI and local builds.

## Considered Options

- JUCE 7
- iPlug2
- wdl-ol
- Custom solution

## Decision Outcome

Chosen option: `JUCE 7`

JUCE 7 is selected for its mature plugin API, cross-platform support, standalone application support, CMake integration, documentation, and active development. It provides the most direct path to delivering VST3, AU, and standalone targets while letting the team focus engineering effort on the synth engine and agent bridge rather than plugin-host compatibility infrastructure.

## Pros and Cons of the Options

### JUCE 7

- Pros: Mature VST3/AU support, standalone app support, broad platform coverage, active maintenance, strong documentation, widely used in commercial audio software, and practical CMake support.
- Cons: Licensing must be reviewed for the chosen distribution model, framework conventions influence project structure, and JUCE abstractions can add complexity when deeply customizing plugin behavior.

### iPlug2

- Pros: Lightweight plugin framework, open source, supports common plugin formats, and can be simpler for focused audio plugins.
- Cons: Smaller ecosystem than JUCE, less comprehensive application/UI infrastructure, and potentially more integration work for the desired standalone and companion UI workflow.

### wdl-ol

- Pros: Proven lineage in audio plugin development and a lightweight approach for experienced plugin developers.
- Cons: Less active and less aligned with modern CMake-based workflows, smaller support surface, and greater long-term maintenance risk.

### Custom solution

- Pros: Full control over host integration, binary layout, and runtime architecture.
- Cons: High implementation and maintenance cost, substantial plugin-format compatibility risk, slower delivery, and a larger testing burden across DAWs and operating systems.

## Links

- [Agentic Synth Architecture](../ARCHITECTURE.md)
- [JUCE](https://juce.com/)
