Heartbeat: 1
Current step: Restored the native no-forwards policy conversion and removed the disabled macOS icon command block.
Files edited: Telegram/SourceFiles/history/history_item_helpers.cpp; Telegram/CMakeLists.txt; phase-review-fix.progress.md; phase-review-fix.result.md.
Validation: source-level checks confirm the native MTP flag conversion, the separate Ayu metadata assignment, and the upstream policy consumer; the focused CMake icon section contains no comments or disabled actool block; git diff --check passes.
Build constraint: AGENTS.md prohibits configure and build commands; none were run.
Blocker: none.
