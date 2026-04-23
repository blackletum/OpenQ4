# OpenQ4 SDL3 Input Key Matrix Audit (2026-04-23)

This checklist audits SDL3 input parity against the legacy Win32 path for:

- console
- GUI edit fields
- chat
- binds
- numpad
- modifiers

## Method

- Reviewed event generation in `src/sys/win32/win_sdl3.cpp`.
- Compared behavior to legacy `WM_KEY*` + `WM_CHAR`/DirectInput paths in:
  - `src/sys/win32/win_wndproc.cpp`
  - `src/sys/win32/win_input.cpp`
- Reviewed consumer paths:
  - `src/framework/Console.cpp`
  - `src/framework/EditField.cpp`
  - `src/ui/EditWindow.cpp`
  - `src/ui/Window.cpp`
  - `src/framework/Session.cpp`

## Checklist

| Area | Coverage | Result | Notes |
|---|---|---|---|
| Console input | Toggle key, enter/submit, tab-complete, history, paging, backspace, ctrl shortcuts | Pass | `SE_KEY` + `SE_CHAR` paths align for core behavior, and `Sys_GetConsoleKey` now follows the active SDL3 layout for the grave/console scancode. |
| GUI edit fields | Backspace/delete, cursor movement, enter handling, ctrl-h/ctrl-a/ctrl-e style chars | Pass | Backspace regression fixed via control-char synthesis on keydown. |
| Chat input | Text entry/editing in GUI-driven chat fields | Pass (UTF-8/control) | SDL text input is decoded to Unicode codepoints before queuing `SE_CHAR`, and control chars remain restored on keydown. |
| Binds | Key-down bind execution in session loop (including function keys and mouse/wheel) | Pass | Printable `SE_KEY` values now come from SDL3's current-layout keycode translation while special keys keep the explicit engine mappings. |
| Numpad | KP enter/arrows/home/end/ins/del, KP operators, KP equals | Pass (core) | SDL3 mapping includes KP key families used by id key enums. |
| Modifiers | Ctrl/Shift/Alt/RightAlt behavior | Pass (core) | RightAlt locale behavior now matches legacy intent for english vs selected non-english languages. |

## Remaining Parity Gaps

1. Legacy keynums still cap bindable printable keys to the engine's 8-bit key range.
   - SDL3 layout-aware translation now restores the intended non-English bind semantics for printable Latin-1 keycodes and console-toggle lookup.
   - Layouts whose base printable keys fall outside the legacy `< 256` keynum space still need a broader input-system expansion if they are to become bindable without fallback behavior.

## Recommended Next Steps

1. Validate the SDL3 layout-aware path against real AZERTY/QWERTZ/Spanish/Italian keyboards during runtime bring-up and record any layout-specific regressions in this matrix.
2. Decide whether future non-Latin-1 keyboard support should extend the legacy keynum/bind system beyond the current 8-bit printable-key range.
