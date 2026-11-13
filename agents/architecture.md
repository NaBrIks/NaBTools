# Current state and proposed direction

Advertising classification update 2026-10-29: read docs/readme_advertising_classification_ru.md.
The SDK-free classifier exposes bounded typed claims and evidence, including uncertainty,
conflicts and requested-versus-provided services. ResearchModule combines recent advertising
and scan-response facts; classification never connects or creates devices. Modules may set
Detection::kind. Engine/backend must not contain vendor classification logic. ScanMode is
generic; ESP-IDF waits for asynchronous stop/configure completions and preserves scan intent.
All commands/state use existing WS/CBOR. This change was validated without hardware access.

Active GATT update 2026-10-23: read docs/readme_gatt_ru.md. Optional SDK-free
IGattClient routes owned events through engine to devices. Heart Rate protocol
lives only in profiles/heart_rate; no HRS UUIDs/vendor knowledge in engine/backend.
C6 received real Garmin pulse via CBOR. SmartTag now has module-owned GATT
discovery/info reads and raw button subscriptions; see docs/readme_smarttag_gatt_ru.md.
Owner authentication, encrypted ring and decoded buttons are implemented in the
Samsung module (owner.cpp / owner_gatt.cpp); read docs/readme_smarttag_owner_ru.md.
Existing owner credential retrieval, mutual auth and audible ring were verified
on EI-T5300. Its proof includes a trailing NUL; accept two exact full blocks.
Live button events / reliable Stop remain unverified after link losses. Application
owns credential loading/storage; injected crypto keeps the module SDK-free.
Research WebSocket/CBOR uses boot_id and bounded snapshot buffers.

Implementation update 2026-10-23: docs/readme_runtime_ru.md records the first
experimental implementation. The notes below describe the earlier proposal
history, not a claim that the new runtime is absent. The root CMake component
builds NaBTools::runtime sources with SDK adapters; library.json supplies the
portable runtime for an injected backend. Both use C++20. On 2026-10-29 the owner
approved transferring remaining useful wrappers/SIG categories and deleting the
flat implementation. See docs/readme_retired_code_ru.md and the single-runtime ADR.

Latest working direction: docs/readme_sig_outputs_bt_ownership_ru.md supersedes
the previous ownership/SIG budget and mandatory typed-output proposals. SIG is
an optional config-selected module (proposed COMMON/EXTENDED/FULL; OFF allowed).
Protocol modules can emit STRUCT, CBOR or BOTH directly through application
Actions without a mandatory generic intermediate payload or serialization layer.
BT subsystem owns runtime devices; application owns business configuration and
callback integration. Application DeviceOwner is no longer the main proposal.
Normal operation is long-lived BT; per-device removal and callback lifetime
still need safe cancellation/generation checks. Detailed APIs/lists remain proposals.

Start a resumed session with docs/readme_handoff_ru.md. The accepted central
engine/module direction is recorded in docs/adr/ADR_20261023_bt-engine-modules.md;
the detailed design in docs/readme_direction_ru.md remains editable. A request
to document decisions does not promote every design recommendation to an
owner-approved API or implemented capability.

The 2026-10-23 extraction is a historical provenance record. The retired flat
implementation, duplicate generated SIG tables, old Python generators and Doc/
notes were removed after feature migration. The official SIG snapshot/notices
and dated extraction manifest remain. There is no static device registration or
nlohmann-json dependency. External consumers must adopt the feature headers and
explicit module composition; their migration is not implied by this cleanup.

This is an independent local project, not a completed architectural rewrite.
See docs/readme_extraction_ru.md for provenance and docs/readme_retired_code_ru.md
for current limitations. docs/readme_known_issues_ru.md records deleted-code defects.
Do not call a build a runtime
or hardware acceptance test.

Owner goals: encapsulate Bluetooth mechanisms in useful classes; inspect types,
services, characteristics and values with rich descriptions; compare borrowed
views and owned observations; support device type recognition and per-device
instances; expose diagnostic/state/measurement hooks for application transports.

Owner-confirmed direction: a central hardware-facing Bluetooth engine with
attached analyzer, type-recognition, known-device and future modules. Only this
high-level direction is accepted; detailed interfaces below remain proposals.

Recommended, still proposed: core value/view types; SIG descriptions; BLE/Classic
backends; runtime device registry and session manager; optional protocol drivers;
structured diagnostics and capture/replay; optional CBOR/web adapters. Application
composition injects drivers, identity/admission policy and event sinks. Runtime
owns instances, drivers own decoding/protocol state, application owns business
identity/configuration and durable storage. Known configured devices exist before
first sighting; arbitrary observations need not allocate a stateful driver.

CBOR is optional, but a protocol module can provide a direct CBOR output and
invoke an application Action. Do not require a typed struct/JSON/reflection
pipeline for CBOR-only users. Keep diagnostics useful without a web server.

Owner clarification later on 2026-10-23: use the useful Bluetooth SIG materials,
plan periodic upstream refreshes, and make research operate by names. See
docs/readme_interfaces_proposal_ru.md for the concrete proposed C++ interfaces
and named-catalog/SIG servicing design. These interfaces are not implemented or
ratified. The present tree remains a pinned snapshot, not an active submodule;
do not run the historical update commands against it or claim automatic sync.
