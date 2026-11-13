# NaBTools

- Architecture maps: read docs/readme_semaps_ru.md. Start `semaps mcp` from this repository, verify project id `nabtools` before edits. Use MCP tools for registry/views, render after layout changes, save when requested and run `semaps check`. Never reuse a connection bound to another project's model.

Read Readme.md / Readme.ru.md, docs/readme_handoff_ru.md, agents/architecture.md and docs/readme_direction_ru.md before changes.

- Owner approved shared C/C++ formatting on 2026-10-30: read agents/code_style.md / agents/code_style.ru.md. Canonical tool is Uncrustify 0.83.0_f with uncrustify.cfg; use tools/format_cpp.py --list, --check or --apply. Do not run a second formatter or apply C++ rules to JS/JSON/Vue. Vendor/SIG/generated data are excluded; do not broaden scope through unsafe Git pathspecs.

- Keep the main research demo's own sources together under examples/research: firmware in main/, site assets in web/, pack/preview scripts in tools/, browser tests in tests/web/. Shared library research transport helpers remain in integrations/research. Read examples/research/README.md before changing demo paths.

- Owner required complete naming migration on 2026-10-29, superseding earlier compatibility exceptions. Read docs/readme_nabtools_migration_ru.md and docs/adr/ADR_20261029_complete_naming.md. All APIs, headers, schemas, storage keys, tools and flags use NaBTools/nabtools/NABTOOLS. Do not restore former aliases.
- Research web server dependency is NaWServer. The example currently requires the historical HTTPD API; see docs/readme_nawserver_ru.md. Preserve the WS/CBOR envelope structure; current naming rules govern all identifiers. The SmartTag experiment is retained in source; the owner stopped further hardware investigation of the tag.

- Owner authorized generic device commands/events on 2026-10-23. See docs/readme_device_interaction_ru.md for the implemented first mechanism and remaining active BLE work. Owner subsequently authorized universal GATT and live HRM on C6: see docs/readme_gatt_ru.md. Optional IGattClient is SDK/profile/CBOR independent; ESP-IDF owns bounded callback events, Heart Rate owns HRS protocol. Real Garmin notifications were received. Generic UI uses driver-declared capabilities; no device-name branches. SmartTag owner authentication, encrypted ring and decoded button are experimental module implementations; read docs/readme_smarttag_owner_ru.md. Existing credential retrieval, mutual authentication and audible ring were verified on EI-T5300; live button events and reliable Stop remain unverified. Credential acquisition/storage belongs to the application, never the engine or public journal.

- Before creating, changing or assessing UI, read and apply docs/ui-principles.md in full. It is the mandatory owner standard.
- The owner authorized implementation and C6 research firmware deployment on 2026-10-23. See docs/readme_runtime_ru.md for the implemented first slice, build commands and limits. New runtime APIs remain experimental; distinguish them from accepted high-level architecture.
- Latest owner refinement: docs/readme_output_levels_ru.md. Typed structures, direct CBOR, and optional semantic descriptions are different usage levels; the application composes one or more outputs. Do not require a generic intermediate object. Rich descriptor transport and whether descriptions are separate or attached remain proposals. Recognition does not auto-admit devices in the research application; Add explicitly creates a BT-owned instance. The UI must remain generic and display received CBOR, not contain TPMS/SmartTag field branches. See docs/readme_smarttag_ru.md.

- Latest working direction is docs/readme_sig_outputs_bt_ownership_ru.md: SIG is an optional config-selected module; protocol modules may produce STRUCT/CBOR/BOTH directly and call application Actions. BT subsystem owns runtime devices; application owns business configuration and integration callbacks. Normal operation is long-lived BT, not repeated shutdown. Earlier mandatory typed intermediate serialization and application DeviceOwner proposals are superseded; concrete APIs/profile lists are still proposals.

- Accepted direction is recorded in docs/adr/ADR_20261023_bt-engine-modules.md. Keep accepted decisions distinct from recommendations and implemented facts. Supersede accepted ADRs with a new ADR rather than rewriting them.

- The owner requested extraction from ESP32-BLETPMS-and-more (original Bluetooth library) on 2026-10-23. Bluetooth exploration, detailed descriptions and diagnostic output are first-class goals. Do not discard research tools just because production consumers need less output.
- The owner confirmed a central hardware-facing Bluetooth engine with attached modules: full analyzer, type recognizers, known-device handlers and future modules. This is the accepted direction; concrete interfaces and lifecycle/CBOR policies remain proposals.
- Owner approved feature folders on 2026-10-23: see docs/readme_layout_ru.md. Headers under include/NaBTools/{core,bt,profiles,devices,research,catalogs} mirror src/. Hardware SDK adapters belong in backends/; application transports belong in integrations/. Public include/NaBTools headers are canonical. On 2026-10-29 the owner approved migrating useful wrappers/catalogs and deleting the retired flat implementation, generators and duplicate tables. See docs/readme_retired_code_ru.md. Do not recreate a legacy implementation. The root ESP-IDF CMake component includes hardware adapters; library.json supplies only portable runtime sources for an injected backend. Both require C++20.
- Preserve detailed observations, raw unknown fields, provenance and parser uncertainty. Support explicit borrowed views and owned snapshots; never retain borrowed SDK callback buffers asynchronously.
- Keep Bluetooth mechanisms, protocol descriptions, device drivers, diagnostics and application adapters modular. CBOR is optional in protocol outputs and is used directly in the research application.
- Fleet/wheel positions, summer/winter sets, configuration persistence, 433 MHz/CAN integration, UI and web servers belong to applications. They motivate the device lifecycle design; they are not authorized implementation scope for extraction.
- Keep consumers consistent with the complete naming migration; do not restore former spellings. No hardware flashing, commit, push, remote configuration or publication without an explicit owner request.
- Research and future implementation should distinguish Bluetooth LE, Classic, GATT, SDP and audio capabilities. Do not promise unsupported hardware/stack features.
- Keep temporary outputs under ignored local/ or build directories. Keep source and upstream-data notices; do not silently change licensing.
- Owner decision 2026-10-30: own code is MIT, including previously GPL-licensed own code. Use short SPDX headers and confirmed copyright Oleg Kozhevnikov, 2026. Preserve third-party notices and specific adaptation exceptions listed in docs/third_party.md. Do not insert comments into strict JSON or generated/SIG/vendor data.
- Root Readme.md is English and Readme.ru.md is Russian; keep their feature/dependency/license claims aligned. Detailed guides are Russian; agent instructions are English. Use rg for source/config/doc exploration.
- Owner refinement 2026-10-23: document SDK-specific workarounds and sequencing decisions in English source comments with primary-source links. Distinguish measured behavior from an established low-level cause; see scan/open arbitration in backends/espidf/espidf_backend.cpp.

- Owner refinement: all research data exchange uses WS/CBOR, including reset snapshots and commands; HTTP only serves assets. Snapshots and live updates use one changes envelope. See docs/readme_websocket_ru.md.
- Public-repository requirement: documentation describes library architecture/API/examples only. Never include private workstation paths, network topology, real device IDs, host inventories or local deployment transcripts. Keep private validation evidence under ignored local/.

## Recommended development environment (owner clarification, 2026-10-26)

The intended reproducible baseline is documented in
[docs/readme_development_environment_ru.md](docs/readme_development_environment_ru.md).
Use username-independent paths and prefer shared versioned SDKs under the chosen
PlatformIO packages directory. This is a recommendation/default, not a mandatory
machine layout or a prohibition on other explicit configurations. Alternative
paths and version profiles are allowed when deliberately configured and validated.
Do not silently change versions based on historical verification reports.
