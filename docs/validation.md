# Crystal Voice 1.2.0 validation

Validated on 2026-10-04 with Windows 11 Pro x64 (build 26200), Visual Studio 2022 Build Tools and JUCE 8.0.13.

## Automated checks

Release application and test builds succeed. CTest reports **108 cases, 1046 assertions passed, 0 failed**. Coverage includes:

- Actual audio graph processing, including mono fan-out, converter bypass, missing effects and stereo averaging.
- Plugin scanning, timeouts, crash recovery, bundle/binary cache invalidation and forced retry.
- State validation, VST3 class identity, failed device changes, retained missing-plugin presets and backup recovery.
- Stable effect identities after reorder/removal and picker search, empty results, keyboard selection and cancellation.
- Driver errors marshalled to the message thread, queued notifications discarded on shutdown and failed retries retaining the requested route.
- Old-stream errors discarded after switching/retrying, automatic restart of stopped devices, and clearing warnings after driver-managed recovery.
- Low-latency to shared-mode fallback using only the selected endpoints, mode-aware rollback, rollback failure reporting and input-only output selection.
- Live picker updates retaining searches and exact VST3 class selection, including multiple classes in one bundle and repeated updates after a selected class disappears.
- Disconnected sample-rate preferences remaining visible, the initial selected effect staying on screen, and non-overlapping routing/meter/effect/footer layouts at minimum and larger window sizes.
- Both embedded JetBrains Mono weights loading with the expected family, cached typeface reuse and equal character advance widths.
- VU sine calibration, 300 ms rise/fall and small overshoot at 44.1/48/96/192 kHz, suppression of short bursts, separate peak retention, callback-size/GUI-polling independence, channel polarity and reset behavior.
- Actual drag animation intermediate positions, reversal/cancellation, stable controls after drop, one persistence callback per change, no persistence for unchanged drops, scrolled coordinates, edge scrolling and safe refresh during a drag.
- Sample peaks holding for 500 ms and releasing at 20 dB/s across sample rates, newly retriggered peaks, lower ongoing tones, negative transients on the loudest channel, over-range samples and visible full-scale peak rendering.
- Windows environment paths retaining drive letters, spaces, quoted semicolons and UNC roots; case/trailing-separator/dot-component deduplication; invalid relative-path rejection; conventional/user/portable locations and nested VST3 discovery excluding DLLs.

Audio engine tests use injected in-memory device types, with WASAPI disabled in the test executable. No real audio device is enumerated or opened by these regression fixtures.

The documentation image is rendered from the real application components with a disconnected development profile. It validates layout and component rendering; it does not replace interactive desktop testing.

## VU display calculation

The bars and numbers now follow a full-wave rectified channel average with an idealized second-order meter movement: approximately 300 ms to 99% of a steady level and 1.2% overshoot. The [ITU-R BS.645-2 metering description](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.645-2-199203-I!!PDF-E.pdf) provides the mean-level/300 ms VU reference. This is a software VU-style display on the existing digital dBFS scale, rather than a change to analog 0-VU calibration.

Detection runs over every audio sample with coefficients prepared at the device's actual sample rate. The waveform itself is unchanged; block RMS and sample-peak measurements remain available independently. The existing thin peak line holds maxima for 500 ms, then releases at 20 dB/s instead of dropping to an arbitrary sample at hold expiry. The peak detector uses the loudest channel without averaging, and does not oversample for true peaks. Full-scale peaks are drawn inside the frame in red, including on the input meter. Numerical tooltips expose the held sample peak. Bar motion remains 24 Hz and numerical VU readings update at 6 Hz.

Rendering the same disconnected profile before and after this change produces an identical PNG SHA-256 (`c1f20b00232042ad468ccb01b0cd3f37939f01b2e064afdd0c3b59ca61324758`), confirming the static styling/layout is unchanged.

## Current interface smoke test

The Release executable was launched with a separate development profile after the user authorized desktop testing. Its console design follows threshold-34's square panels, green/cyan accents and terminal labels. JetBrains Mono Regular and Medium are embedded along with their OFL license, so a system font installation is unnecessary. The background grid is static.

Verified on Windows 11 at the current display scale:

- The integrated dark title bar and two-column workspace render in the actual native window.
- Dragging the resize border reaches the 928 × 678 window minimum. Device selectors, both meters, footer controls and the three-effect chain remain accessible; long effect names elide in the narrow list.
- The effect picker opens with the first selected built-in effect visible and focused search, and uses the same console typography and palette.
- Escape closes the picker. The TDR Nova native editor opens, draws correctly beneath the console title bar and closes back to the host.
- Showing the initially hidden window restarts meter/status updates. With MiniFuse 2 channel 1 and VB-CABLE at 48 kHz / 480 samples, input levels update; dry bypass makes input/output readings match and the header reports DRY MIC. This is a live UI/route check, not a sustained-speech quality measurement.
- TDR Nova was dragged between the second and third slots, with animated settling and stable live routing; the current RNNoise → mono-to-stereo → TDR Nova order was restored afterwards.
- Library's automatic-path submenu opens and lists all eight conventional/user/portable candidates with installed/not-installed status. Native x64 Program Files/Common Files paths and the custom voice-plugin folder are preserved; VST2 support is explicitly distinguished from searching mixed folders for VST3 files.

No installed host, original MicVST configuration, system audio defaults or startup entry was changed. The existing cached Gullfoss Live scan failure remains visible as a retryable library notice.

## Earlier local hardware smoke test

These measurements were taken before the current interface redesign. They describe the earlier preview and have not been repeated for the latest code.

The test uses MiniFuse 2 input channel 1 → RNNoise mono → TDR Nova → mono-to-stereo → VB-CABLE. WASAPI opens at **48 kHz / 480 samples**, with one active physical input and two output channels. The cable capture returns 48,000 stereo frames in one second; bypass carries the physical mic signal and mute reduces the capture to the 16-bit mixer noise floor. No audio recording is saved.

Both hosts were measured in the tray with the same saved effect chain, over 30 seconds each, on a Ryzen 7 9700X with 16 logical processors. There was no continuous speech.

| Host | Mean working set | CPU, percentage of whole machine |
| --- | ---: | ---: |
| MicVST 1.1.1 | 111.30 MiB | 0.428% |
| Crystal Voice 1.2.0 | 98.04 MiB | 0.437% |

The memory reduction is about 12%; CPU usage is effectively similar in this quiet background workload. These figures are a local smoke measurement, not a sustained-speech benchmark. The UI's “audio CPU” measures audio callback time and is a different metric.

The original host was restored after testing. Its configuration hash and all three default microphone roles were unchanged.

## Checks still required

Additional display scales, tray hide/show interactions and physical unplug/reconnect remain unverified for the console build. An earlier interactive preview verified picker search/Enter/cancel; broader plugin editor coverage is still needed. Further coverage should include Windows 10 and additional audio interfaces/VST3 effects. Scanning is isolated in child processes; plugin loading and real-time processing run in the host, so a faulty third-party plugin can still terminate it.
