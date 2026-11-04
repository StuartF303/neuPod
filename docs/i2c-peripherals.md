# I2C peripherals: fuel gauge and haptics

Phase 1 breadboard: one I2C bus, **I2C0 on the Pico 2 W, SDA = GP20, SCL = GP21**, 400 kHz (fall back to
100 kHz if NACKs appear). Both breakouts carry their own pull-ups, so the firmware keeps the RP2350
internal pull-ups **off** (the pad reset state is pull-down enabled, so they are switched off
explicitly). See [Phase 1 wiring](wiring-phase1.md).

| Device | Board | Address | HAL | Driver |
|---|---|---|---|---|
| MAX17048 fuel gauge | Adafruit MAX17048 | 0x36 | `hal_battery.h` | `app/drivers/max17048.c` |
| DRV2605L haptic driver | Adafruit DRV2605L | 0x5A | `hal_haptic.h` | `app/drivers/drv2605l.c` |

`hal_power.h` is kept for sleep and power-state control only; its old battery snapshot was never
implemented and is replaced by `hal_battery.h`.

## Bus layer (`hal_i2c.h`)

* Every call is bounded: timeout = 2 x the byte time at the actual baud, never below 2 ms.
* A NACK is a normal answer and never causes recovery. Only a timeout, or a line that is still low
  100 us after a failed transfer, starts recovery: reset the controller, then (if SDA/SCL is low) switch
  the pins to GPIO, clock up to 9 SCL pulses (drive low / release, never drive high), generate a STOP
  and restore the I2C function. `hal_i2c_get_stats()` counts errors and recoveries.
* Probing uses a 1-byte read (a zero-length write is unreliable on the RP2040/RP2350 controller) and
  skips the reserved ranges 0x00-0x07 and 0x78-0x7F.
* Arbitration loss cannot be detected on the RP2350: the SDK clears the abort-source register
  before the HAL can read it, so `ARB_LOST` is never reported there. A lost arbitration that leaves a
  line low shows up as a stuck bus.
* Reserved addresses are refused (`BAD_PARAM`) by every call, not just probes.
* Dropping from 400 to 100 kHz is never automatic: `hal_i2c_set_baud()` is called by the test (or
  application) deliberately.

## MAX17048 (0x36)

Registers are 16-bit big-endian. Used here:

| Reg | Name | Scaling / use |
|---|---|---|
| 0x02 | VCELL | 78.125 uV per LSB (mV = raw x 5 / 64, rounded) |
| 0x04 | SOC | 1/256 % per LSB (high byte = whole percent) |
| 0x06 | MODE | bit 14 = quick-start |
| 0x08 | VERSION | reads 0x001X |
| 0x0A | HIBRT | high byte hibernate threshold 0.208 %/h per LSB, low byte active threshold 1.25 mV per LSB |
| 0x0C | CONFIG | high byte RCOMP; low byte SLEEP (b7), ALSC (b6), ALRT (b5), ATHD (b4:0): alert at 32 - ATHD % |
| 0x16 | CRATE | signed, 0.208 %/h per LSB |
| 0x1A | STATUS | RI b8, VH b9, VL b10, VR b11, HD b12, SC b13, ENVR b14 |
| 0xFE | CMD | 0x5400 = power-on reset (the chip may NACK the write as it resets) |

`hal_battery_init()` also clears STATUS.RI (power-up/reset indicator) so a later RI means a fresh
reset; `hal_battery_reset()` waits 250 ms after the reset command and leaves RI set. Quick-start is
only valid with no load on the cell. HIBRT as read from the bench chip: **0x8030** (observed, not
asserted by the tests).

The driver uses integers only: millivolts, SOC in 1/256 %, rate in milli-percent per hour. A cell
below 2.5 V counts as "no battery".

!!! warning "Source check"
    The MAX17048 scaling above comes from the Maxim/Analog Devices register descriptions as recalled
    when writing the driver; the datasheet PDF could not be downloaded in the build environment, so
    it is **not yet cross-checked line by line**. Check VCELL against a multimeter on the cell during
    bench test 2.

**Bench note:** the gauge is powered from the battery side. With no LiPo, or with a flat one, 0x36
disappears from the bus (a flat LiPo made 0x36 vanish on the bench). That is expected, not a wiring
fault.

## DRV2605L (0x5A)

Checked against TI datasheet SLOS854D. Used here:

| Reg | Name | Use |
|---|---|---|
| 0x00 | STATUS | b7:5 DEVICE_ID (7 = DRV2605L), b3 DIAG_RESULT (1 = calibration failed; clears on read) |
| 0x01 | MODE | b7 reset, b6 standby, b2:0: 0 internal trigger, 5 RTP, 7 auto-calibration |
| 0x02 | RTP_INPUT | real-time amplitude |
| 0x03 | LIBRARY | 1-5 and 7 ERM libraries A-F, 6 LRA |
| 0x04-0x0B | WAVEFORM_SEQ | effect id 1-123, or bit 7 set = wait (n x 10 ms); 0 ends the sequence |
| 0x0C | GO | start (clears itself at the end of playback / calibration) |
| 0x16 / 0x17 | RATED_VOLTAGE / OD_CLAMP | see below |
| 0x18 / 0x19 | A_CAL_COMP / A_CAL_BEMF | calibration results |
| 0x1A | FEEDBACK_CONTROL | b7 ERM(0)/LRA(1), b6:4 brake factor, b3:2 loop gain, b1:0 BEMF gain |
| 0x1B-0x1E | CONTROL1-4 | drive time, sample/blanking/idiss, ERM open-loop and RTP format, auto-cal time |

### Motor

Adafruit 1201 "Vibrating Mini Motor Disc": ERM, 10 mm coin, brushed, rated 3.0 V (operates 2-5 V),
about 11,000 rpm, about 75-100 mA running. Default drive mode is **ERM open loop** with ROM library 2
(TS2200 library B); the voltage ceiling is 3.6 V. Build-time defaults: `NEUPOD_HAPTIC_ACTUATOR=HAL_HAPTIC_ERM`
and `NEUPOD_HAPTIC_ERM_LOOP=HAL_HAPTIC_LOOP_OPEN` (or `HAL_HAPTIC_LOOP_CLOSED`). Test menu keys: `o` toggles
open/closed loop, `e` / `l` select ERM / LRA.

**Why open loop:** on the bench the closed-loop auto-calibration did not converge on this motor (3.0 V
rated, 3.3 V supply; DIAG_RESULT failed after 1844 ms) and the motor buzzed instead of clicking. By user
decision the ERM now runs open loop, which is also how Adafruit's own library drives this motor. Closed
loop stays available (`o` key or build flag) for later experiments. The LRA path (library 6, LRA bit) is
selectable at run time (Settings > Haptics > Actuator) and described below.

### LRA: LEADER LD0832AA

Target actuator for the final board: 8 mm coin LRA, rated 2.0 V rms, resonance about 235 Hz (build defaults in
`HAL_HAPTIC_LRA_LD0832AA`, `hal_haptic_lra_params_t`; override at run time with `hal_haptic_set_lra_params()`).
**Not confirmed from the LCSC datasheet (C2682305):** the datasheet could not be fetched from the build host, so
2.0 V rms and 235 Hz are the figures from the brief. Everything below is computed from those two numbers, so a
different actuator only needs different parameters. The peak ceiling (2.8 V, 2 V x sqrt(2) floored) is a design
choice, not a datasheet figure.

| Value | Formula (datasheet SLOS854D) | Result |
|---|---|---|
| LIBRARY | LRA library | 6 |
| FEEDBACK | N_ERM_LRA = 1, brake factor 2, loop gain 2, BEMF gain 2 (calibration overwrites the gain) | 0xAA |
| RATED_VOLTAGE | Eq. 5: V rms = 20.58 mV x RV / sqrt(1 - (4 x tSAMPLE + 300 us) x f), tSAMPLE = 300 us, so RV = floor(2000 x sqrt(1 - 1500 us x 235) / 20.58) | 78 (0x4E) |
| OD_CLAMP | Eq. 9: V peak = 21.22 mV x OD, so OD = floor(2800 / 21.22) | 131 (0x83) = 2.78 V |
| CONTROL1 | STARTUP_BOOST + DRIVE_TIME = (half period - 0.5 ms) / 0.1 ms; half period 2.128 ms | 0x90 (2.1 ms) |
| CONTROL2 | SAMPLE_TIME 3, BLANKING 1, IDISS 1 (datasheet defaults) | 0xF5 |
| CONTROL3 | ERM_OPEN_LOOP and LRA_OPEN_LOOP cleared (closed loop, auto-resonance), RTP signed | 0x80 |
| CONTROL4 | AUTO_CAL_TIME = 3 | 0x30 |

Click effect on an LRA: library 6 effect 4, Sharp Click 100 % (ERM keeps effect 3).

### Calibration and persistence

`hal_haptic_calibrate()` follows datasheet 8.5.6: MODE = 7, write the inputs above, GO, poll GO every 10 ms
(through the delay hook, `drv2605l_set_delay_fn`; the firmware installs the scheduler delay so other tasks run),
read STATUS DIAG_RESULT (1 = failed), then A_CAL_COMP, A_CAL_BEMF and BEMF_GAIN. A failure or timeout rebuilds the
chip from reset with the previous good values.

The haptic task (core 0) owns this. The results are stored in the settings store (`NPhc`, 9 bytes: version,
actuator, comp, bemf, gain, rated mV, peak mV, resonance Hz; version 2, 11 bytes) and re-applied on every boot, so the motor spins for the
calibration once per actuator. It runs automatically when an LRA is selected (or built in) and no matching record
exists, once the settings are known (the card's values have loaded, or there is no working card; a boot without a card cannot store it, so it repeats each boot). If the record cannot be stored (store full) the row shows Failed and a console line says so, and on demand
from Settings > Haptics > Calibrate. Changing the actuator type, or the rated voltage, peak voltage or resonance (OD_CLAMP is a calibration input), makes the stored
record not match, so it recalibrates (as does an old version 1 record, which had no peak); switching back and forth between types re-runs it for each. The task holds the
I2C bus (the gauge read waits) for about a second but sleeps between polls, never blocking the UI. Clicks during it:
`rt_haptic_click()` drops the request while the state is RUNNING (nothing is queued), and the task drains its queue when
a calibration starts and again when it ends, which also discards a click that raced past the check. If the automatic attempt fails it is not retried until the next boot or a manual Calibrate.

### Click setting and rate limit

Settings > Click: Piezo / Haptic / Both / Off (`NPck` 2 / 1 / 3 / 0). There is no piezo driver yet
(`NEUPOD_PIEZO_FITTED` is 0), so the menu cycles Haptic / Off only; a Piezo or Both value already stored is accepted but
silent (`rt_piezo_click()` is a stub). Haptic clicks go through a 2-deep queue and a
35 ms gate (`haptic_gate_pass`): a fast wheel spin drops requests instead of queueing them.

### Bring-up order (motor safety)

In open loop OD_CLAMP is the full-scale drive (21.59 mV x OD, datasheet Eq. 6), so the clamp value
must always match the loop mode in effect: 182 (the closed-loop value) would be about 3.93 V in open
loop. The chip powers up open-loop. The driver therefore leaves standby, sets the chosen loop bit in
CONTROL3 and reads it back, writes the feedback/timing registers, and writes RATED_VOLTAGE then
OD_CLAMP **last**. If any step fails the chip is put back into standby and play/click/RTP/calibrate
return an error until init succeeds. A failed or timed-out calibration resets the chip and re-applies
the previous good calibration. The test menu puts the driver in a safe state (RTP 0, GO 0, standby)
as soon as the bus comes up, and back in standby after every haptic test. The `l` (LRA) key is
disabled unless the test is built with `NEUPOD_I2C_TEST_ALLOW_LRA`.

### Register values: ERM open loop (default)

| Value | Formula (datasheet Eq. 6) | Result |
|---|---|---|
| CONTROL3 | ERM_OPEN_LOOP = 1 (power-on default), RTP signed | 0xA0 |
| OD_CLAMP | V = 21.59 mV x OD, so OD = floor(3600 / 21.59) | 166 (0xA6) = 3.584 V (167 would be 3.605 V) |
| RATED_VOLTAGE | ignored in open loop (datasheet 8.5.2.1); the closed-loop value is written as a safe one | 141 (0x8D) |

The datasheet constant is 21.59 mV per step for ERM open loop (Eq. 6), not 21.96 mV; the maximum open-loop
clamp is therefore 166, enforced by the tests. Open-loop RTP is signed: 0 = no drive, 127 = +OD_CLAMP,
negative values drive in reverse (braking). The ramp test only uses 0 to 127 to 0.

### Register values: ERM closed loop (optional, datasheet default timings)

Timings used: DRIVE_TIME = 0x13 (ERM: 1 ms + 19 x 0.2 ms = 4.8 ms), BLANKING_TIME = 1 (75 us),
IDISS_TIME = 1 (75 us); SAMPLE_TIME is LRA-only and left at 3. CONTROL1 = 0x93, CONTROL2 = 0xF5.

| Value | Formula (datasheet) | Result |
|---|---|---|
| RATED_VOLTAGE | Eq. 4: V = 21.18 mV x RV, so RV = floor(3000 / 21.18) | 141 (0x8D) = 2.986 V |
| OD_CLAMP | Eq. 8: V = 21.64 mV x OD x (4.8 ms - 300 us) / (4.8 ms + 75 us + 75 us), so OD = floor(3600 / 19.673 mV) | 182 (0xB6) = 3.580 V (183 would give 3.6001 V, over the limit) |
| FEEDBACK (pre-cal) | ERM, brake factor 2, loop gain 2, BEMF gain 2 (power-on default; calibration overwrites it) | 0x2A |
| CONTROL3 | ERM_OPEN_LOOP cleared (power-on default is open loop), RTP format signed | 0x80 |
| CONTROL4 | AUTO_CAL_TIME = 3 (1000-1200 ms) | 0x30 |

Brake factor 2, loop gain 2 and AUTO_CAL_TIME 3 are the "valid for most actuators" values in the
datasheet calibration procedure. The floors are deliberate: the register never exceeds the
requested voltage. The output cannot exceed the DRV2605L supply either (3.3 V on the breakout
unless VIN is higher), so the real ceiling on this bench is the supply, not 3.6 V.

### Real-time playback

Signed format (the power-on default). Closed loop (bidirectional): 0x00 = no drive, 0x7F (127) = full
rated voltage, 0x3F about half; negative values act as zero. Open loop: 0x7F = +OD_CLAMP, 0x81 =
-OD_CLAMP (reverse drive). The ramp test uses 0 to 127 to 0, which is safe in both.

### Click latency

`hal_haptic_click_prepare(effect)` loads the sequencer once; `hal_haptic_click()` is then a single
two-byte I2C write (GO). Test 7 times that call with the microsecond clock. It measures the
host-side I2C cost only; motor current and mechanical onset cannot be measured on this bench (no
current sense or scope hook).

## Test menu (`i2c_test`)

Flash with `scripts/flash.sh rp2350 i2c_test`; menu on UART0 at 115200. `q` stops a running test (any
key stops the effect sweep).

| Key | Test | Notes |
|---|---|---|
| 1 | Bus scan | idle-line check, scan 0x08-0x77, PASS when 0x36 and 0x5A answer; retries at 100 kHz if a device is missing or errors occur |
| 2 | Gauge | dumps version/config, then 10 s of voltage, SOC and rate; SKIP if no battery |
| 3 | Gauge under load | 5 s idle, 10 s with backlight 100 % and CPU busy; backlight is turned off at the end. Shows nothing if the Pico is USB-powered: the load current must flow from the battery through the gauge |
| 4 | Haptic ID | STATUS, device ID (must be 7), mode, library, control registers |
| 5 | Haptic calibrate | closed loop only (SKIP in open loop): auto-calibration (motor spins about 1 s); prints the measured values, then notes the restore, and a build-flag snippet |
| 6 | Haptic sweep | effects 1-123, 300 ms apart (1-10 only in run-all) |
| 7 | Click latency | 20 clicks, min/avg/max microseconds |
| 8 | Haptic RTP | amplitude ramp 0 to 127 to 0 over 2 s |
| 9 | Soak | 60 s: gauge read every 500 ms, click every 2 s; PASS only with zero I2C errors and recoveries ("clicks only" if the gauge is absent) |

## Bench results (2026-11-02)

| Item | Value |
|---|---|
| Working I2C speed | 399361 Hz measured actual at 400 kHz requested (both devices answered, no errors, 0 recoveries for 110 probe NACKs) |
| Gauge identity | VERSION 0x0012, CONFIG 0x971C, HIBRT read 0x8030 |
| Gauge VCELL check | chip 3919 mV vs multimeter 3917.5 mV (0.04 %) |
| Gauge under load (test 3) | no change seen: the Pico was USB-powered, so the load did not pass through the gauge; the test needs the load current to flow from the battery through the gauge |
| Haptic ID | STATUS 0xE0 (device ID 7), start-up safe state worked (MODE 0x40) |
| Closed-loop auto-cal | failed to converge (1844 ms, DIAG_RESULT failed); motor buzzed but did not click; switched to open loop |
| Click latency (call to GO write complete) | closed loop 86 / 87 / 91 us; open loop 84 / 84 / 86 us (min / avg / max); target < 15 ms |
| Open-loop click feel | effect 1 (Strong Click 100 %) at OD_CLAMP 166 (3.584 V): crisper than closed loop but still slightly buzzy; accepted for now |
| RTP ramp (test 8) | PASS, 2062 ms, open loop |
| Soak result (test 9) | PASS: 60 s, 120 gauge reads, 30 clicks, 510 transactions, 0 I2C errors, 0 recoveries |

### Chosen effect IDs

| Use | Effect ID |
|---|---|
| Wheel click | 1 (Strong Click 100 %), provisional default; not yet chosen (test 6 sweep not run) |
| Button click | _tbd_ |
| Alert | _tbd_ |

## When something fails

| Symptom | Meaning |
|---|---|
| Scan: nothing answers | No ACK at all: check SDA GP20 / SCL GP21 wiring, breakout power, and that the breakout pull-ups are present |
| Scan: lines low before start | A device is holding the bus; the test tries recovery. If it stays low, power-cycle the breakouts |
| 0x36 missing, 0x5A fine | Gauge is powered from the battery side: connect a charged LiPo (a flat one makes 0x36 vanish) |
| 0x5A missing | DRV2605L power (3.3 V), wiring or EN pin |
| Test 4: device ID not 7 | A different chip or a bad read |
| Test 5: calibration fails or never finishes | Wrong ERM/LRA setting, or the motor is not connected/seated |
| Test 9 reports errors | Look at the stats line: NACKs or timeouts point at wiring or pull-ups; try 100 kHz |
