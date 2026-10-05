# Contributing to Crystal Voice

Crystal Voice is a small, focused Windows tool - bug reports, ideas and
pull requests are all welcome.

## Reporting bugs / requesting features

Open an [issue](https://github.com/km4sh/crystal-voice/issues). For bugs, please include:

- your Windows version,
- which virtual cable you use (VB-Cable / VoiceMeeter / Virtual Audio Cable / …),
- the plugins in your chain,
- the relevant lines from the log: `%APPDATA%\CrystalVoice\log.txt`.

## Building

Requires **Windows x64** (tested on Windows 11; Windows 10 should work but is untested) and
**Visual Studio 2022** with the *Desktop development with C++* workload (MSVC + Windows SDK +
CMake). JUCE is fetched automatically via CMake `FetchContent` - nothing else to install.

```powershell
# Configure (fetches JUCE)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build app + tests
cmake --build build --config Release --target CrystalVoice MicVSTTests --parallel 6

# Run the unit tests (exit code 0 = ok)
ctest --test-dir build -C Release --output-on-failure
```

See the README for the full build/run details.

## Pull requests

- Branch off `main` and keep changes focused.
- Match the surrounding code style (JUCE conventions, 4-space indentation).
- Make sure it **builds cleanly with no new warnings** and the **unit tests pass**.
- The GitHub Actions CI builds the app and runs the tests on every push and PR.

## Project layout

```
src/
  Main.cpp                 app lifecycle, tray wiring, silent autostart
  audio/   AudioEngine, PluginChain, GraphConnections, Metering, MicVSTDeviceManager
  state/   Persistence (config.xml), AutostartRegistry
  ui/      MainComponent, DevicePanel, PluginListView, LevelMeterComponent, TrayIcon
tests/     JUCE regression tests for routing, scanning, state and picker interactions
resources/ icon SVG and generated PNGs
tools/     icon regeneration script
docs/      validation results and outstanding manual checks
```

## License

By contributing, you agree that your contributions are licensed under the project's
**GPL-3.0** license (see [LICENSE](LICENSE)).
