# Copilot Instructions for NoteSoFast

## Goal
Build a lightweight, native Windows note-taking app (NoteSoFast) using Win32 API and SQLite, focusing on clarity, maintainability, and easy future extension. Cross-platform support is not required.

## Coding Style
- Prefer clear, readable C++17 (or later) with minimal abstractions; avoid over-templating.
- Use RAII and smart pointers (`unique_ptr`, `shared_ptr` where ownership sharing is required); avoid raw `new`/`delete`.
- Keep functions short and single-purpose; limit files to cohesive responsibilities.
- Use `constexpr`/`enum class` for constants and strong typing; avoid macros except for Win32/SQLITE definitions.
- Favor early returns over deep nesting; guard clauses for error handling.
- Apply const-correctness throughout; pass by reference-to-const where appropriate.
- Prefer `std::optional`/`std::string_view` where it simplifies intent without adding complexity.

## Windows & Win32 Guidelines
- Target Windows 10/11 x64 only; no cross-platform shims.
- Use native Win32 controls: `ListView` for note lists, `RICHEDIT50W` for editing, `Toolbar`, `StatusBar`, `ComboBox`, dialogs for settings/about/color picker.
- Keep the message loop straightforward; centralize message handling in a main window procedure with helper functions per feature area.
- Avoid MFC/ATL; keep dependencies minimal (only SQLite + system-provided controls).
- Use Unicode (wide-char) Win32 APIs; set `UNICODE` and `_UNICODE`.
- Wrap Win32 handles (HWND, HFONT, HICON, HBRUSH) in small RAII helpers to ensure proper cleanup.

## Database (SQLite)
- Use prepared statements for all queries; bind parameters, step, reset, and finalize reliably (RAII wrapper for statements).
- Enable WAL mode and create indexes on `title` and `modified_at` after schema creation.
- Use transactions for batch changes (e.g., checklist reorder, bulk export).
- Keep schema aligned with PLAN.md: `notes`, `colors`, `checklist_items` tables; keep migrations simple and additive.

## Architecture & Organization
- Suggested layout (adapt as needed):
  - `src/main.cpp` (WinMain, app init), `window.cpp/h` (main window + message routing), `database.cpp/h` (SQLite wrapper), `note.cpp/h` (data model), `ui_controls.cpp/h` (control helpers), `utils.cpp/h` (helpers), `resource.rc` (menus, dialogs, icons).
  - `include/sqlite3.h`, `lib/sqlite3.lib`, `resources/icons/`.
- Separate UI logic from data access; avoid mixing SQL in UI code.
- Keep feature flags/config in one place; prefer simple structs over global variables.
- Add lightweight logging for debug builds (OutputDebugString) with compile-time toggle.

## Feature Priorities (align with PLAN.md)
- MVP: create/edit/delete notes, list view with sorting, debounced auto-save, timestamps, search over title+content.
- Phase 2: color categories (default set), checklists, pin-to-top, archive toggle, sort options, auto-save polish.
- Phase 3: light rich text (basic styles, bullets), auto-link detection (URLs/email/paths), export/import (.txt), keyboard shortcuts.
- Exclude: calendar/reminders, sticky widgets, encryption, online sync, SMS/email sharing, web search, map integration.

## UX & Behavior
- Fast startup (<500ms) and low footprint (<50MB RAM, small exe without SQLite DLL).
- Keep UI responsive: avoid blocking the UI thread on I/O; use worker threads for DB-heavy tasks if needed.
- Debounce auto-save to reduce writes; surface last-saved time in status bar.
- Provide clear affordances: pinned indicator, color chips, archive toggle, search clear button.

## Testing & Quality
- Add unit tests where feasible (non-UI logic: database wrapper, data model transforms).
- Manual test checklist: create/edit/delete, search, color filter, pin/unpin, archive/unarchive, checklist operations, rich text basics, auto-link clicks, export/import, keyboard shortcuts.
- Run basic memory checks (e.g., Visual Studio diagnostics) and watch for handle leaks.

## Documentation & Comments
- Prefer self-explanatory code; add brief comments only for non-obvious Win32 interactions or tricky logic.
- Keep README/PLAN in sync with feature scope and schema changes.
- Document any deviations from the plan in comments or commit messages.

## Build & Tooling
- Support MSVC (primary) and optionally MinGW-w64; provide simple build scripts/project files.
- Treat warnings as errors in CI; enable /W4 (or /Wall where practical) and static analysis where available.
- Avoid introducing additional third-party deps beyond SQLite unless justified.

## Extensibility
- Design for incremental features: new note fields, additional filters, or formatting options should slot in via small, localized changes.
- Keep serialization/migrations additive and backward compatible; avoid breaking schema changes.
- Use enums/config tables for color categories to simplify future additions.

## Security & Privacy (minimal scope)
- No encryption or online sync; avoid storing secrets.
- Validate and sanitize file paths and URLs before opening; fail safely.

## Git & Reviews
- Keep commits small and focused; include concise messages referencing the feature/area touched.
- Favor code clarity over cleverness; prioritize maintainability.
