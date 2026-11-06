# Project workflow

- During the current synchronization debugging phase, compile and deliver a new
  version after source changes without waiting for an explicit build request.
  This temporary authorization was given by the user on 2026-11-05.
- GitHub Actions may build pushes to main during this phase. Avoid intermediate
  builds by saving source/version changes before updating the workflow.
- Include the app version in the APK filename and provide direct GitHub links.
- Keep protocol and Signal logic in the portable C99 core, platform adapters
  separate, and Android UI in Kotlin.

- The current test phone reports `armeabi-v7a,armeabi` in its APP_OPEN log.
  Deliver only `armeabi-v7a` by default. Do not infer Android process ABI from
  the CPU model or assume ARM64 support. x86_64 is for CI emulator tests only.
