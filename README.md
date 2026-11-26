# SFU Burnaby Campus Tour

An interactive audio guide for the BeagleY-AI, controlled with a single rotary encoder (turn + push button).

| Program | What it does |
|---|---|
| `tour_player` | The main program. Choose a tour stop with the encoder, press to play, then control volume / pause / skip. Typed commands over SSH can stand in for the encoder (for debugging). |
| `audio_selftest` | Plays a file and checks play / pause / resume / volume / stop. Use it to confirm the sound path. |

Everything is written in C and built with CMake. Hardware access is isolated in `hal/`, the application logic is in `app/`.

## 1. One-time setup

Your board needs to be reachable by SSH, with the rotary encoder wired (see section 4) and the USB audio adapter plugged in.

### 1a. Build tools on the host

```
sudo apt install cmake gcc-aarch64-linux-gnu
```

The encoder uses **libgpiod v2**, so the host also needs the ARM64 version of it to cross-compile:

```
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install libgpiod-dev:arm64
```

Your host's libgpiod must be version 2.x (Debian 13 / Ubuntu 24.10 or newer). If you can't get it, build without the encoder: set `CMAKE_EXTRA_ARGS="-DENABLE_ENCODER=OFF"` in `config.sh`. Everything else still works with typed commands.

### 1b. Personal settings file

```
cp scripts/config.sh.example scripts/config.sh
nano scripts/config.sh
```

`config.sh` is git-ignored (see `.gitignore`), so everyone has their own. Set these values:

- `BOARD_USER`: your username on the board (what you type in `ssh <name>@192.168.7.2`).
- `BOARD_HOST`: the board's IP address. `192.168.7.2` if the board is connected to your computer by USB-C.
- `BOARD_DIR`: folder on the board that receives the programs. Leave as `campus_tour` (created automatically in your home folder).
- `CMAKE_EXTRA_ARGS`: optional build options, e.g. `-DENABLE_ENCODER=OFF`. Usually empty.

Check that you can log in before going on: `ssh <BOARD_USER>@<BOARD_HOST>` (type `exit` to come back). `ssh-copy-id <BOARD_USER>@<BOARD_HOST>` stops it asking for the password every time.

### 1c. Set up the board

```
scripts/setup_board.sh
```

This installs `mpg123` and `alsa-utils` on the board (the board needs internet for this), then checks that the libgpiod v2 library is present and that the USB audio adapter is detected. Fix anything it reports as `PROBLEM` before continuing.

If `apt` complains about the BeagleBoard package server ("Mirror sync in progress"), wait a few minutes and run the script again.

## 2. Build and copy to the board

```
scripts/deploy.sh
```

This builds (cross-compiles) and copies the two programs and the `audio/` folder to `BOARD_DIR` on the board. Options: `--no-build` (just copy) and `--no-audio` (skip the audio files, which are the big part).

## 3. Run it

All programs are started from your host with `scripts/run_on_board.sh`. It logs in over SSH and runs them with `sudo`, because GPIO access for the encoder needs root. It may ask for the board's password.

### Step 1: audio

```
scripts/run_on_board.sh audio_selftest
```

You should hear about five seconds of a tone with a short silence in the middle, and every line should say `PASS`.

### Step 2: the player

```
scripts/run_on_board.sh tour_player
```

#### Encoder controls

| Mode | Turn | Press | Hold (~1 s) |
|---|---|---|---|
| **Choosing a stop** (nothing playing) | Move through the list (stops at the first / last stop) | Play the highlighted stop | Nothing |
| **Playing** | Volume ±5% per click | Pause | Stop, back to the list |
| **Paused** | Skip ±5 s per click (never past 1 s before the end) | Resume | Stop, back to the list |

When a recording ends by itself, the **next** stop is highlighted ("Next stop: … press to play"). Nothing starts on its own. After the last stop, the player says the tour is complete.

#### Typed commands (debugging without the encoder)

Typed commands pretend to be the encoder, so you can test everything over SSH. Type the command and press Enter:

| Type | Same as |
|---|---|
| `+` or `.` | one click clockwise (`+++` = 3 clicks, `+5` = 5 clicks) |
| `-` or `,` | one click counter-clockwise (`---` = 3 clicks, `-5` = 5 clicks) |
| `p` | press |
| `x` or `hold` | hold (long press) |
| `i` | show the mode, track, time, volume and what turning does right now |
| `l` | list the stops (`>` highlighted, `*` playing) |
| `h` | help |
| `q` (or Ctrl-C) | quit |

If the encoder is not found (not wired, wrong pins, built with `ENABLE_ENCODER=OFF`), the player prints `Encoder not available - use typed commands instead` and keeps running.

Options:
- `--audio-dir DIR`: folder of MP3s.
- `--device ALSA_DEVICE`: e.g. `plughw:1,0`.
- `--log FILE`: also append every message, with a timestamp, to FILE (useful for debugging the GUI).

Example: `scripts/run_on_board.sh tour_player --log tour.log`.

## 4. Encoder wiring

| Encoder pin | BeagleY-AI line |
|---|---|
| A (CLK) | `GPIO16` |
| B (DT) | `GPIO17` |
| Push button (SW) | `GPIO18`: the other side of the switch goes to GND (internal pull-up, active low) |
| GND | GND |

The pins are set by name at the top of `hal/src/encoder.c`. If clockwise turns go the wrong way, set `INVERT_DIRECTION` to `true` there. The long-press time is `ENCODER_LONG_PRESS_MS` in `hal/include/hal/encoder.h`.

## Adding the real tour audio

Put the MP3 files in the `audio/` folder of the project and run `scripts/deploy.sh`. Name them with a number first so they play in order: `01_Welcome_to_SFU.mp3`, `02_Academic_Quadrangle.mp3`, … The list shows "Welcome to SFU", "Academic Quadrangle", and so on. Nothing else needs to change: the list, the highlight limits and the "next stop" all come from the files that are there. Delete the `*_Test_Tone.mp3` placeholders when real recordings arrive.

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `Permission denied` when starting a script | Run once: `chmod +x scripts/*.sh`. |
| `No scripts/config.sh yet` | Do step 1b. |
| `Cannot reach name@192.168.7.2` | Board still booting (wait 60 s), wrong `BOARD_USER` / `BOARD_HOST`, or the USB network is not set up. Try `ssh` by hand. |
| CMake: `libgpiod (v2) not found for this compiler` | Do the libgpiod part of step 1a, or build with `-DENABLE_ENCODER=OFF`. |
| `encoder: GPIO lines GPIO16 and GPIO17 not found` | Wrong line names for your board, or the pins are claimed by another function. Check with `gpioinfo` on the board. |
| `encoder: unable to request GPIO lines` | Not run with `sudo`, or another program is using the pins. |
| Turning works but pressing does nothing | Button not wired to `GPIO18` / GND. The player prints `button line GPIO18 not available` at start if the pin is missing. |
| `could not start 'mpg123'` | Run `scripts/setup_board.sh` (installs it). |
| `Audio device: default (no USB audio adapter found...)` | Adapter not plugged in or not detected: `aplay -l` on the board. |
| Plays but no sound | Wrong output device (use `--device`), or the speakers/headphones are not plugged into the adapter. Check the volume with `i`. |
| `Playback problem: ... Device or resource busy` | Another program is using the sound card. Make sure no old `mpg123` is running: `pkill mpg123`. |
| Build error `aarch64-linux-gnu-gcc: not found` | Step 1a. |

## Project layout

```
hal/                             hardware access only
  include/hal/encoder.h          rotary encoder + button (libgpiod v2), turn / short / long press
  include/hal/press_detector.h   button debounce and long-press timing (pure logic, unit tested)
  include/hal/audio_device.h     finds the USB sound card
  src/encoder_stub.c             used instead of encoder.c when ENABLE_ENCODER=OFF
app/
  src/main.c                     main loop: inputs -> controller, once per frame
  src/tour_controller.c          the encoder control scheme; tour_controller_view() for the GUI
  include/controls.h             the control events every input source produces
  src/input_queue.c              thread-safe queue: encoder thread -> main loop
  src/terminal_input.c           typed commands -> the same control events (debugging)
  src/app_log.c                  all messages: kept in memory for the GUI, echoed, optional log file
  src/playback_display.c         time text (m:ss / h:mm:ss) and progress fraction
  src/audio_player.c             plays MP3s via ALSA (controls the mpg123 player)
  src/track_list.c               finds and names the MP3 files
  test/                          audio_selftest
tests/                           hardware-free unit tests (run on the host)
audio/                           the MP3 files
scripts/                         config.sh.example, build.sh, deploy.sh, run_on_board.sh, setup_board.sh
cmake/aarch64-toolchain.cmake
```

## How the pieces fit (and where the GUI goes)

```
encoder thread --push--> input_queue --+
typed commands ------------------------+--> main loop --> tour_controller --> audio_player
                                                 |               |
                                                 |      tour_controller_view()
                                                 +--> (GUI) draws the view + app_log lines every frame
```

- **GUI:** once per frame (`FRAME_INTERVAL_MS`, 100 ms), draw from `tour_controller_view()`:
  - `mode`: choosing / playing / paused, for the play/pause indicator.
  - `focus`: which widget to highlight, i.e. what turning changes (track list, volume bar or progress bar).
  - `highlighted`, `current`, `track_count`: for the track list.
  - `audio.position_sec`, `audio.duration_sec`, `audio.volume_percent`: for the time, progress bar and volume bar. Use `playback_display_time()` and `playback_display_progress()` to format them.
  - The terminal panel shows the lines from `app_log_get_line()`. `app_log_version()` changes whenever a new line arrives.

  Because everything is redrawn from the player's status, pausing, skipping and tracks ending always show correctly.
- **Another input device:** make it push the same `control_event_t` values into `input_queue`. Nothing else changes.
- **Audio back end:** `audio_player.h` hides the fact that `mpg123` does the decoding.

## Tests

Hardware-free tests run on the host. They need no libgpiod, sound card or encoder:

```
cmake -S . -B build-host -DBUILD_HOST_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

| Test | Checks |
|---|---|
| `tour_controller_test` | Every rule in the encoder controls table, against a fake player: highlight limits, volume steps, skip limits, hold to stop, next stop after a track ends, errors. |
| `press_detector_test` | Button debounce, short press, long press fires once while held. |
| `terminal_input_test` | Typed commands → control events. |
| `input_queue_test` | Order, capacity, wake-up fd, 1000 events pushed from another thread. |
| `playback_display_test` | `m:ss` / `h:mm:ss` text and the progress fraction. |
| `audio_device_test` | USB sound card discovery. |
