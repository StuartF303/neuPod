# Wheel input widgets

One library for every place the UI needs the user to enter something with a click wheel: text (passwords, names, search), numbers,
levels, choices and dates. Code: `app/ui/input/`. The **model** of each widget (`app/include/wheel_input.h`, `wheel_input.c`) is pure C with no
LVGL and is unit tested on the PC (`tests/test_wheel_input.c`); the **view** (`app/ui/input/ui_input.c`) draws it with LVGL and feeds it the input.

## Key semantics (the same in every widget)

| Input | Meaning |
| --- | --- |
| scroll | change what is under the cursor: the glyph in the ring, the value, the highlighted choice, the field. Steps arrive already accelerated by the UI frame |
| Select | choose or type: types the highlighted key, ticks a multi-choice item, picks the choice, goes to the next date field |
| Menu | back: backspace when there is text, otherwise cancel (a number, slider or choice cancels at once; a slider restores its old value); **held 0.6 s it clears the whole text** |
| Play/Pause | confirm and close |
| Left / Right | the mode chip of a text entry, or the date/time field |

A modal swallows every button (Menu never pops the screen underneath by accident).

## Widgets

| Widget | Request kind | Result in `wi_value_t` | Notes |
| --- | --- | --- | --- |
| Text entry | `WI_K_TEXT` | `text` | the **letter ring** (design review 3.7, IN-1): a field box with a caret at the top, a ring of nine glyphs (the current one Big in the accent, its neighbours smaller and greyer) turned by the wheel, and **mode chips** `abc` / `ABC` / `123` / `#!?` / `space` / `del` under it, switched with Left / Right (a charset of digits or hex offers only `123` and `del`; `space` only for free text); Select types the glyph in the ring (on the `space` chip a space, on `del` deletes one), Menu deletes (hold clears), Play is done. A `masked` password shows the character just typed for 800 ms, then a bullet. The text always grows at its end (no cursor). `max_chars` (code points), `max_bytes`, `charset` ANY / DIGITS / HEX; UTF-8 safe buffer; `initial` text |
| Search | `WI_K_TEXT` with `search = true` | `text` | like text, and `change` fires after every edit so the caller can filter a list live. A suggestions row is not built; the callback is where it would hook in |
| Number | `WI_K_NUMBER` | `number` | the value Big and centred with a bar under it and `min` / `max` in Small at its ends (IN-2); `min`, `max`, `step`, `decimals` (fixed point, value in units of 10^-decimals), `unit` ("dB", "min", "%"); scroll steps are multiplied by the wheel acceleration; clamps |
| Slider | `WI_K_SLIDER` | `number` | a number with a bar and a live `change` callback; cancel sends the original value through `change` so the preview is reverted. Settings > Display (backlight) uses it |
| Choice | `WI_K_PICKER` | `pick` | a plain list; single choice from `items[count]`; `multi = true` makes Select tick items and the result is `mask` |
| Date and time | `WI_K_DATETIME` | `dt` | the active field in the accent colour (underlined) with the name of every field under it (Day, Month, Year, Hour, Min); `has_date` / `has_time`; fields day, month, year (2000 to 2099), hour, minute; the day follows the month and leap years; Select walks the fields and confirms on the last one |
| Letter jump | `ui_letter_overlay_*` + `wi_letterjump_*` | n/a | not a modal: an overlay on a sorted list. While the wheel is turned fast (6 steps within 400 ms) a box shows the initial of the item under the cursor; it hides 700 ms later. `wi_letter_first_index()` finds the first item for a letter (binary search). Files shows it today; the library will use it |

## API

```c
#include "ui_input.h"

static void name_done(void *ctx, bool ok, const wi_value_t *v) { if (ok) use(v->text); }

ui_input_open(&(wi_request_t){
    .kind = WI_K_TEXT, .title = "Network name", .initial = "", .max_chars = 32, .done = name_done });
```

`ui_input_open()` pushes the modal screen on the UI stack (one at a time: it returns false while another is open) and calls `done(ctx, ok, &value)` exactly once,
before the modal closes. Two rules follow from that:

* **Do not open the next modal from inside `done`.** The modal is popped after the callback returns, which would pop the new one. Record the result and open the next
  step from the owning screen's tick (the Wi-Fi screen does this for SSID, then security, then password).
* Strings in the request only have to live until the call returns; the result is copied into `wi_value_t`.

Live filtering for a future library search: `search = true` plus `change = filter_list` where the callback sets the list's filter text and refreshes the list model.

## Memory

Models are small structs (the text model is 90 bytes). LVGL objects exist only while a modal is open. A covered screen can free its rebuildable objects
(`suspend` / `resume` on `ui_screen_t`; the list widget frees all its rows) and a modal calls `ui_frame_suspend_top()` before building itself, so a modal opened over a list
does not add its size on top of the list's. LVGL pool use on the host (64-bit objects, pool 72 KB, about 63 KB usable), read from the tour (`ctest -R input_tour`), with the
Extras > Input widgets list underneath:

| Widget | LVGL pool used while open | Without a covering list |
| --- | --- | --- |
| text, password, search | 46.8 KB | the same |
| number, slider | 44.9 to 45.4 KB | |
| picker, multi choice | 59.8 KB (a list in a list) | |
| date/time | 46.4 KB | |
| the list screen alone | 57.4 KB | |

The firmware uses 32-bit pointers, so the objects are smaller there; the figure to read on the board is the `lvgl_mem=` line of the monitor.

## Demo and tests

* Extras > **Input widgets** opens each widget and shows the last result.
* `neupod --tour-input --snapshots DIR` (ctest `input_tour`) walks through every widget and writes one snapshot each.
* `ctest -R wheel_input` runs the model tests: typing, cursor, page switching, maximum length, masking, charsets, UTF-8, cancel and confirm, number clamping and
  acceleration, picker wrap and clamp, multi choice, date validity including leap years, letter-jump index calculation.
* Not built: typing directly on the host keyboard as a shortcut (the host key map reserves most letters for the wheel keys); the testbed UART keys drive the widgets like the wheel.
