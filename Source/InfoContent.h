#pragma once

// ============================================================================
// InfoContent.h — Structured content for the ECHO-TR info popup.
//
// All user-facing text (title, links, poem, attribution) lives here as
// a single XML document.  The popup builder in PluginEditor.cpp parses
// this at runtime and creates the appropriate JUCE components.
//
// Supported element types (children of <content>):
//   <heading>   — bold, centered, larger font
//   <text>      — normal weight, centered
//   <link url=".."">  — HyperlinkButton (centered)
//   <separator> — decorative line (centered, normal font)
//   <poem>      — italic, centered
//   <spacer/>   — vertical gap (empty line height)
//
// To change the popup content, edit ONLY the XML below.
// ============================================================================

namespace InfoContent
{
    // Plugin version — used both in the XML and by the paint() version label.
    // Single source of truth: update HERE, the XML references it too.
    static constexpr const char* version = "1.1";

    static constexpr const char* xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<info>
  <content>
    <heading>ECHO-TR v1.1</heading>
    <spacer/>
    <text>by Nemester</text>
    <link url="https://github.com/lmaser/ECHO-TR">Github Repository</link>
    <separator>&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;</separator>
    <link url="https://ko-fi.com/nemester">Support on Ko-fi</link>
  </content>
</info>
)xml";
}
