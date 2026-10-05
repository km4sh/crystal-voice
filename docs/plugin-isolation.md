# Plugin process isolation

Crystal Voice hosts each VST3 instance in its own Windows process, following the scope of Bitwig's [Individually mode](https://www.bitwig.com/userguide/latest/vst_plug-in_handling_and_options/). Duplicate instances of one class have independent processes and parameter state. Built-in channel converters stay in the host.

## Process ownership

`IsolatedPlugin` is the graph's proxy, with no plugin DLL or editor in the main process. It launches the same portable executable with a private `--cvplugin:` pipe identifier. `PluginWorker` loads one real processor, prepares it, runs its DSP, serializes state and owns its editor. Workers never open audio devices, session files, recovery markers or tray icons.

The host holds the worker's process handle and a dedicated Windows job with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. It uses that owned handle rather than a PID for termination. Removing/replacing a proxy terminates its job when the old graph render sequence retires; closing or killing the host closes all job handles. IPC callbacks stop before their owners' queues and state are destroyed. See Microsoft's [job object lifetime rules](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects).

## Audio transport

Each instance has a preallocated shared mapping of about 1 MiB and two auto-reset events. The mapping has input/output arrays, frame shape, transport position and aligned sequence/state counters using [Windows Interlocked operations](https://learn.microsoft.com/en-us/windows/win32/sync/interlocked-variable-access). Only the parent writes input and only the worker writes output; publishing a sequence transfers ownership. No allocation or control IPC is performed in a proxy's audio callback.

The host publishes the current block and waits for its completion within a single absolute deadline shared by the entire chain. It reserves 0.25 ms for output protection and metering. Every worker processes on a separate audio thread registered with MMCSS's Pro Audio task when available. The design does not deliberately pipeline a previous block or add a block of latency. Scheduling and event synchronization still cost time; tiny buffers and long chains can exceed the callback budget.

There is at most one outstanding block per worker. If a deadline expires, the host leaves that input untouched. A late completion is discarded before sending the next current block, so old voice audio cannot be replayed as current audio. Missing/busy/failed output fades from the last bounded sample to zero over 5 ms; valid output fades back in. An outstanding DSP request older than 250 ms marks the instance hung, even if it has since been bypassed and no more audio is submitted. A message-thread timer terminates the failed job. Healthy workers keep their own threads and processes, although downstream effects in a serial chain receive the preceding failed effect's silence.

## Control, state and editors

Control packets use JUCE's named-pipe coordinator/worker connection with request IDs and bounded payloads. Replies arrive on a background thread; plugin operations execute on the child's message thread. General requests have a 2-second response timeout; initial load has 15 seconds, with additional launch/connection time. The host UI may wait for these bounded control operations. Slow or native-crashing plugin functions cannot hold it indefinitely.

State operations lock only that child's processor callback lock. Its audio thread uses a try-lock and returns silence while state is busy; the main graph and healthy workers remain active. Parameter revisions are polled by the proxy and fed into the existing debounced session/preset capture. A successful state read or restore updates the parent cache; failure returns the last good state. Revisions arriving during capture remain visible for a later snapshot. Native editor close schedules capture after about one second of inactivity; explicit Save, Export, window hide and Quit request a fresh capture.

The editor lives in a separate child-owned native window, including JUCE's generic editor when needed. Opening it again raises the existing window. An editor fault therefore has the same process boundary as a DSP fault. Cross-process window embedding is intentionally avoided.

Reload constructs a new worker and restores its cached parameters before replacing only the failed slot. The stable row ID, chain position and bypass flag remain intact; other workers are retained. A failed replacement leaves the original slot available for another retry. Automatic scan completion cannot restart failed workers. Preset loading stages all new instances and commits only after every state restore succeeds. Startup keeps output silent while restoring, and an unavailable active slot keeps the microphone muted for review.

## Limits

- Windows x64; one process per VST3 instance. The main process and each worker retain the user's normal permissions. This is crash containment, not a restricted security sandbox.
- Main audio buses only, at most 16 channels and 8192 samples per block; mandatory auxiliary buses are rejected. MIDI hosting, sidechains and arbitrary runtime bus-layout changes are outside this voice-host path.
- Plugin state payloads are limited to 48 MiB. A malfunctioning state operation retains the last successful snapshot, so unsaved edits made immediately before a fault can be lost.
- Resource use increases per instance. The UI's audio CPU reports elapsed callback time including waits, while operating-system CPU usage measures actual CPU time across all processes.
- Saving state can briefly interrupt wet audio in a serial chain. Master bypass explicitly passes latency-aligned dry audio while keeping healthy DSP warm; its compensation remains capped at one second.

Tests exercise native access violations and deliberate process exits during creation/DSP/state restore, hung DSP/state functions, independent duplicate instances, other workers continuing during slow serialization, discarded late blocks, single-slot reload, startup failure muting and job cleanup after a host exits without running destructors. Real RNNoise/Nova checks and resource measurements are recorded in [validation](validation.md).
