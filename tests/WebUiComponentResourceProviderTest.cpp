// Phase 3: validate that the bundled React UI is reachable via the
// WebUiComponent resource provider lookup. We deliberately do NOT construct
// a WebUiComponent here — that would require a windowed parent and a live
// system WebView at test time. Instead we exercise the pure lookup function
// `WebUiComponent::serveResource`, which is the same code path the JUCE
// WebBrowserComponent invokes for every request.
//
// What this proves end-to-end:
//   1. `juce_add_binary_data(agentic_synth_ui_assets ...)` produced symbols
//      that the test can link against (UiBinaryData.h is on the include path).
//   2. The Vite build emitted an `index.html` and at least one CSS asset
//      into `ui/dist/`, and those got picked up by the GLOB.
//   3. Path normalisation, basename extraction, and MIME selection match
//      the spec in the migration plan.

#include "ui/WebUiComponent.h"

#include "UiBinaryData.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

using agentic_synth::ui::WebUiComponent;

namespace {

// Find the hashed CSS asset name that Vite emitted, by scanning the binary
// data namespace. Vite produces names like "index-D1HomtRC.css". We rely on
// `getNamedResourceOriginalFilename` to recover the original name from the
// mangled identifier.
juce::String findCssAssetOriginalName() {
    for (int i = 0; i < UiBinaryData::namedResourceListSize; ++i) {
        const char* mangled = UiBinaryData::namedResourceList[i];
        const char* original = UiBinaryData::getNamedResourceOriginalFilename(mangled);
        if (original != nullptr && juce::String(original).endsWithIgnoreCase(".css")) {
            return juce::String(original);
        }
    }
    return {};
}

} // namespace

TEST_CASE("serveResource: root path returns index.html with text/html", "[WebUiComponent][ResourceProvider]") {
    auto resource = WebUiComponent::serveResource("/");
    REQUIRE(resource.has_value());
    CHECK(resource->mimeType == "text/html");
    CHECK_FALSE(resource->data.empty());

    // Sanity-check that we actually got HTML, not some other asset.
    const auto* bytes = reinterpret_cast<const char*>(resource->data.data());
    juce::String head(bytes, juce::jmin<size_t>(resource->data.size(), 4096u));
    CHECK(head.containsIgnoreCase("<!doctype html"));
    CHECK(head.containsIgnoreCase("<div id=\"root\""));
}

TEST_CASE("serveResource: empty path also returns index.html", "[WebUiComponent][ResourceProvider]") {
    auto resource = WebUiComponent::serveResource("");
    REQUIRE(resource.has_value());
    CHECK(resource->mimeType == "text/html");
}

TEST_CASE("serveResource: CSS asset is reachable with text/css MIME", "[WebUiComponent][ResourceProvider]") {
    const juce::String cssName = findCssAssetOriginalName();
    REQUIRE(cssName.isNotEmpty()); // Vite must have emitted at least one CSS file.

    juce::String requestPath = "/assets/" + cssName;
    auto resource = WebUiComponent::serveResource(requestPath);
    REQUIRE(resource.has_value());
    CHECK(resource->mimeType == "text/css");
    CHECK_FALSE(resource->data.empty());
}

TEST_CASE("serveResource: unknown path returns std::nullopt", "[WebUiComponent][ResourceProvider]") {
    CHECK_FALSE(WebUiComponent::serveResource("/nonexistent").has_value());
    CHECK_FALSE(WebUiComponent::serveResource("/assets/does-not-exist.js").has_value());
}

TEST_CASE("serveResource: query strings and fragments are stripped", "[WebUiComponent][ResourceProvider]") {
    auto resource = WebUiComponent::serveResource("/?cachebust=42");
    REQUIRE(resource.has_value());
    CHECK(resource->mimeType == "text/html");
}
