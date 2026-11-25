# Touch input

The Waveshare RP2350 / ESP32-S3 Touch AMOLED 2.41 dev boards have a 600x450 touch panel and no click wheel. The UI keeps its click-wheel vocabulary
(scroll, Select, Menu, Play; see [UI](ui.md) and [Wheel input widgets](ui-input.md)); touch is a stand-in that turns finger gestures into those same events.
Nothing in a screen knows about touch.

**HAL.** `hal/include/hal_touch.h`: `hal_touch_present()`, `hal_touch_init()`, `hal_touch_read()` (single point, panel coordinates in the display's current orientation).
Boards without a panel (Pico 2 W, Y2) link `hal/common/hal_touch_none.c` (not present, init `HAL_ENOTSUP`). A board with one supplies its own driver instead of that file.

**Wiring.** The input task (`app/runtime/runtime.c`) starts the controller once `boot_start()` has brought the I2C bus up (`rt_i2c_ready()`), takes the I2C mutex around
`hal_touch_init()` and every `hal_touch_read()` (the read only try-locks: a busy bus skips one 2 ms poll), and polls at the click wheel's 2 ms cadence. It is the only caller of
`hal_touch_read()`. A controller that does not answer for 25 polls (50 ms) drops the touch in progress without sending anything: a read error never ends a gesture with a tap.
A touch panel whose init fails is logged once (`input: touch panel init failed`) and sends nothing.

**Gestures** (`app/input/touch_gesture.c`, unit-tested by `touch_gesture`). Distances scale with the panel, so the same movement works at 320x240 and 600x450:

| Gesture | Event | Numbers (600x450) |
| --- | --- | --- |
| Vertical drag | `INPUT_SCROLL`, 4 wheel positions per list step | 14 px per position, so one list row (56 px) of travel is one step; counted from where the finger came down (panel height / 32 per position) |
| Tap | Select press and release, at the lift | lifted within 600 ms, moved less than the slop (panel height / 20: 22 px) |
| Swipe right | Menu (back) press and release, once per touch, when the distance is reached | starts horizontal, 120 px (panel width / 5) |
| Long press | Play press and release, once per touch, when the time is up (or at the lift if the polls skipped past it) | still for 600 ms |

The first axis to leave the slop decides the touch (a vertical drag cannot turn into a swipe, nor the reverse). A swipe left does nothing. **Direction** is the phone's: the finger moves up, the content moves up,
the highlight goes down the list (positive steps, as clockwise on the wheel); dragging down goes back up, and returning to the start gives the steps back. The UI's scroll acceleration applies as it does to
the wheel, so a quick drag travels further than a slow one.

**Hold.** The events enter the same queue as the wheel's, so Hold, the screen-off stage and the Hold reminder treat touch exactly as they treat the wheel (a locked or sleeping screen swallows the tap and shows the reminder or wakes).

**Host simulator.** `NEUPOD_TOUCH=1` makes the mouse in the SDL window the touch panel (left button = finger down; the mouse wheel stays the click wheel), with `NEUPOD_DISPLAY_SIZE=600x450` for the right size.
The tours inject contacts with the `touch:down:X:Y`, `touch:move:X:Y` and `touch:up` script tokens (`neupod --tour-touch`, ctest `hires600_touch_tour`): a tap opens Music, a swipe right goes back, drags move the Home highlight, a long press leaves the screen as it is.
