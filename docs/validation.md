# Crystal Voice 1.2.0 validation

Validated on 2026-10-04 with Windows 11 Pro x64 (build 26200), Visual Studio 2022 Build Tools and JUCE 8.0.13.

## Automated checks

Release application and test builds succeed. CTest reports **62 cases, 705 assertions passed, 0 failed**. Coverage includes:

- Actual audio graph processing, including mono fan-out, converter bypass, missing effects and stereo averaging.
- Plugin scanning, timeouts, crash recovery, bundle/binary cache invalidation and forced retry.
- State validation, VST3 class identity, failed device changes, retained missing-plugin presets and backup recovery.
- Stable effect identities after reorder/removal and picker search, empty results, keyboard selection and cancellation.
- Driver errors marshalled to the message thread, queued notifications discarded on shutdown and failed retries retaining the requested route.

The documentation image is rendered from the real application components with a disconnected development profile. It validates layout and component rendering; it does not replace interactive desktop testing.

## Local hardware smoke test

The test uses MiniFuse 2 input channel 1 → RNNoise mono → TDR Nova → mono-to-stereo → VB-CABLE. WASAPI opens at **48 kHz / 480 samples**, with one active physical input and two output channels. The cable capture returns 48,000 stereo frames in one second; bypass carries the physical mic signal and mute reduces the capture to the 16-bit mixer noise floor. No audio recording is saved.

Both hosts were measured in the tray with the same saved effect chain, over 30 seconds each, on a Ryzen 7 9700X with 16 logical processors. There was no continuous speech.

| Host | Mean working set | CPU, percentage of whole machine |
| --- | ---: | ---: |
| MicVST 1.1.1 | 111.30 MiB | 0.428% |
| Crystal Voice 1.2.0 | 98.04 MiB | 0.437% |

The memory reduction is about 12%; CPU usage is effectively similar in this quiet background workload. These figures are a local smoke measurement, not a sustained-speech benchmark. The UI's “audio CPU” measures audio callback time and is a different metric.

The original host was restored after testing. Its configuration hash and all three default microphone roles were unchanged.

## Checks still required

The development desktop was locked. Manual clicking, plugin editor windows, high-DPI resizing, tray interactions and physical unplug/reconnect are therefore unverified. Further coverage should include Windows 10 and additional audio interfaces/VST3 effects. Scanning is isolated in child processes; plugin loading and real-time processing run in the host, so a faulty third-party plugin can still terminate it.
