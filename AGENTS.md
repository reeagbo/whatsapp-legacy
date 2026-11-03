# Project workflow

- Do not compile, build an APK, create a release, change the app version, or run
  compilation-based checks until the user explicitly requests a build/version.
- Reading, editing and saving source changes is allowed between releases.
- Keep GitHub Actions manual-only (`workflow_dispatch`); pushing code must not
  trigger a compilation. This is an explicit standing user instruction.
- Keep protocol and Signal logic in the portable C99 core, platform adapters
  separate, and Android UI in Kotlin.
