# Project guidance

The user accepted the plan and authorized implementation and local Git initialization. Read README.md and docs/DECISIONS.md. Keep unmeasured targets and proposed behavior clearly distinguished from working features. Subagents are authorized; the integrating agent must review their results.

## Product

- Target the stock M5Stack Cardputer ADV with ESP32-S3FN8. Do not assume PSRAM or original-Cardputer codec/keyboard compatibility.
- Music must be composed and rendered on the device, offline. Bundled instrument samples, if selected, are sound sources; they must not replace generation with prerecorded songs or backing loops.
- Keep the pure-synthesis versus sample-assisted choice open until the documented comparison and user decision.
- Prioritize uninterrupted music over animation frame rate or background work.
- Create an original pixel-art cat and location for the 240 × 135 screen. Use the existing Lofi Cat as an experience/structure reference; its existing character, room images and branding are not the asset direction for this project.

## Implementation and evidence

- Share the music core between firmware and native audio rendering. Share actual UI drawing and animation code between firmware and the SDL preview.
- Export documentation screenshots from that preview at 240 × 135 and integer nearest-neighbor scales. Label them as desktop renders; hardware audio, timing and installation need separate physical evidence.
- Keep composition randomness separate from visual randomness. Scene changes must not change the generated score.
- Keep allocations, file operations and display calls out of the audio rendering path. Give queued PCM buffers explicit lifetime/ownership rules.
- Pin platform and library versions. Recheck assumptions when those pins change.
- Adapt reference code selectively and preserve required attribution. Reference READMEs are not proof that the corresponding safeguards are implemented.

## M5Apps and releases

- Ordinary releases contain an ESP32-S3 application BIN, checked against the declared compatibility profile, plus explicitly selected optional resources.
- Prefer M5Apps Installer → SD for installation. Do not reuse another project's serial flash offset or assume an upstream launcher partition is the user's application slot.
- Before any direct USB application update, inspect the actual partition layout and verify the intended installed application identity, target bounds and neighboring regions. Do not automate a write when identity is ambiguous.
- Do not erase or rewrite the launcher, partition table, apps_nvs or other applications as part of an ordinary update. Full recovery is a separately scoped operation.
- Do not initialize or erase a generic NVS partition blindly. Confirm compatibility with the installed M5Apps layout before choosing flash persistence.
- Keep device dumps, backups and private SD state out of public packages and the site. Generate publication output from an explicit allowlist.

## Scope

Planning does not authorize flashing hardware, creating remote repositories, publishing a site or issuing a release. These become concrete implementation/delivery tasks when requested. There is no need to add approval steps to local, reversible implementation work that the user subsequently authorizes.
