# Security Policy

## Supported versions

Check the current Crystal Voice source and [releases](https://github.com/km4sh/crystal-voice/releases)
before reporting an issue. Earlier MicVST releases are maintained upstream.

## Scope

Crystal Voice is a small **local** Windows app: it processes audio between local devices and a virtual
audio cable. It has **no telemetry and no auto-installer**. The only network access is an
**optional, opt-in update check** (off by default): when enabled, on startup it makes a single
request to the public GitHub releases API to see whether a newer version exists. The main areas of
interest are third-party VST3 plugins it loads and the config/log files it writes under
`%APPDATA%\CrystalVoice\`.

## Reporting a vulnerability

Please report security issues **privately** via GitHub - do not open a public issue.

When enabled for this fork, use **"Report a vulnerability"** in the repository's **Security** tab
(*Security → Advisories → Report a vulnerability*). This opens a private security advisory that is
visible only to the maintainer.

Plugin scanning runs in a separate process. Plugin loading and real-time processing run in
the host, so third-party plugins are not sandboxed during use.
