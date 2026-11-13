# Provenance and license boundaries

Initial audit date: 2026-10-30. This inventory records
observed notices and provenance; a Git author field alone is not proof of
exclusive ownership. On 2026-10-30 the rights holder explicitly chose MIT for
their own code, including their previously GPL-licensed code. The former GPL
text is retained in `LICENSES/GPL-3.0-only.txt` and in the LCD7 adaptation;
third-party notices/copyrights were not removed.

| Material | Location/source | Observed terms and treatment |
|---|---|---|
| Extracted own library and current own code | Root `LICENSE`; [extraction record](readme_extraction_ru.md) | MIT by the explicit rights holder decision. The old GPL choice is not evidence of foreign authorship. Confirmed identity/year: Oleg Kozhevnikov, 2026. |
| LCD7 board adaptation **exception** | `examples/research/display/research_lcd7/board.cpp` and `board.hpp`, its `LICENSE` and README | Retains GPL-3.0-only and adaptation provenance via NaCBORUIforLVGL/ESP32-yaidfws touch_lcd7. Specific adaptation conditions are preserved; separate foreign ownership was not established. Other independently authored display code is MIT. |
| Bluetooth SIG source | Optional submodule `bluetooth-SIG`, upstream `https://bitbucket.org/bluetooth-SIG/public.git` | The Git link pins the official source. Upstream terms and notices apply. The copied snapshot is retained in the private archive. |
| SIG-derived name tables | Locally generated `src/catalogs/sig/sig.generated.inc` or a consumer-selected directory | The local table is ignored by Git. Earlier generated tables are retained in the private archive. See [generation and profiles](readme_sig_update_ru.md). |
| Tabler icons/font | `examples/research/web/icons/LICENSE.txt` | MIT, copyright 2020–2024 Paweł Kuna. Notice preserved. |
| Own research shell styles | `examples/research/web/{shell,themes}.css`, [runtime provenance](readme_runtime_ru.md) | Owner's styles/adaptations use the current MIT grant. Existing provenance comments remain; third-party icon/font is excluded. |
| SmartTag implementation | `src/devices/samsung/smarttag/`, [sources and method](readme_smarttag_owner_ru.md) | Documented as an independently written implementation of published protocol observations; uTag/research artifact source code is not vendored. Referencing a protocol publication is not a claim to relicense its code. |
| ESP-IDF | External prepared SDK, official Espressif repository | SDK/components retain their own licenses (including component-specific third-party notices); not relicensed by NaBTools. |
| NaWServer | External sibling [NaBrIks/NaWServer](https://github.com/NaBrIks/NaWServer), research-only; pinned in [integration guide](readme_nawserver_ru.md) | Published revision `0d1492f174097a0cd51ddd668b4e1029c4ce4ec9` licenses its own code under MIT and preserves its third-party notices. Its repository terms apply independently of NaBTools. |
| LVGL, TinyCBOR, esp_lcd_touch/GT911 | Managed LCD7-only dependencies pinned in its `idf_component.yml` | Their upstream notices/licenses remain with those packages. They are not needed by portable AD parsing or C6/headless examples. |
| Browser and Python tooling | Root package-lock.json; tools/requirements.txt | cborg is Apache-2.0; jsdom/ws/PyYAML are MIT; transitive packages retain their own terms. Build/test dependencies are not bundled into the browser firmware assets. |

The historical extraction manifest records hashes of original files, including
removed ones. It is not a current file inventory or an ownership certificate.
The public repository starts from a source snapshot.

MIT applies to the owner's work outside the explicit exceptions above. The LCD7
example combines MIT parts with the retained GPL adapter; it must not be advertised
as an entirely MIT example/binary. Strict JSON receives valid metadata, not comments;
SIG derivatives and generated assets receive no fabricated first-party MIT header.
Older reports mentioning GPL describe the license at their date, not the current
grant for the owner's code. Rewriting history would not remove obligations.

Donations/charging for copies do not inherently require MIT. See the
[GNU FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#DoesTheGPLAllowMoney) and
[MIT text](https://opensource.org/license/mit). No donation link/account is configured.
