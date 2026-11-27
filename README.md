# SFU Burnaby Campus Tour

An interactive audio guide for the BeagleY-AI with a 7" screen, a rotary encoder and a joystick. It starts by itself at power-on and picks up where it left off.

| Program | What it does |
|---|---|
| `tour_player` | The tour. Choose stops with the encoder, play/pause, control volume and skip with the joystick, read directions between stops. |
| `display_test` | Draws a test pattern on the screen. Use it to check the screen wiring. |
| `joystick_test` | Live joystick readings (X, Y, direction, button). Use it to check the joystick wiring. |
| `audio_selftest` | Plays a file and checks play / pause / resume / volume / stop. Use it to check the sound path. |

Everything is written in C and built with CMake. Hardware access is in `hal/`, the application logic is in `app/`.

## 1. Wiring

All header pins are physical pin numbers on the BeagleY-AI's 40-pin header (Raspberry Pi layout: pin 1 = 3.3 V corner, odd pins in one row, even pins in the other). **All grounds are connected together.**

### Header summary

| Pin | Signal | Goes to |
|---|---|---|
| 1 | 3.3 V | ADC VDD + VREF, joystick Vcc |
| 2 | 5 V | RA8875 VIN |
| 6, 9, 14, 20 | GND | RA8875, ADC + joystick, encoder common, encoder button |
| 11 | GPIO17 | Encoder A |
| 13 | GPIO27 | Encoder B |
| 15 | GPIO22 | Encoder button |
| 16 | GPIO23 | RA8875 RST |
| 18 | GPIO24 | RA8875 WAIT |
| 19 | SPI0 MOSI | ADC DIN **and** RA8875 MOSI |
| 21 | SPI0 MISO | ADC DOUT only (see RA8875 below) |
| 23 | SPI0 SCK | ADC CLK **and** RA8875 SCK |
| 24 | SPI0 CE0 | ADC CS → `/dev/spidev0.0` |
| 26 | SPI0 CE1 | RA8875 CS → `/dev/spidev0.1` |
| 29 | GPIO5 | Joystick SEL (button) |

### MCP3208 ADC (for the joystick)

| ADC pin | Connect to |
|---|---|
| 1 (CH0) | Joystick Xout |
| 2 (CH1) | Joystick Yout |
| 9 (DGND), 14 (AGND) | GND |
| 10 (CS) | Pin 24 |
| 11 (DIN) | Pin 19 |
| 12 (DOUT) | Pin 21 |
| 13 (CLK) | Pin 23 |
| 15 (VREF), 16 (VDD) | 3.3 V (pin 1) |

Put a 0.1 µF capacitor from VDD (pin 16) to GND close to the chip, so the screen's noise doesn't make the joystick readings jumpy.

### Joystick

Vcc → 3.3 V (**not 5 V**: the outputs must stay within the ADC's 3.3 V reference). GND → GND. Xout → ADC CH0, Yout → ADC CH1, SEL → pin 29. The internal pull-up is turned on in software, so no resistor is needed.

### Rotary encoder

A → pin 11, B → pin 13, common (middle pin) → GND (pin 14). Push switch: one leg → pin 15, the other → GND (pin 20).

### RA8875 screen driver board

| RA8875 pin | Connect to |
|---|---|
| VIN | 5 V (pin 2) |
| GND | GND (pin 6) |
| SCK | Pin 23 |
| MOSI | Pin 19 |
| CS | Pin 26 |
| RST | Pin 16 |
| WAIT | Pin 18 |
| **MISO** | **Not connected** |
| 3Vo, LITE, INT, Y+, Y-, X-, X+ | Not connected |

- **MISO:** boards made before "Rev E" (Aug 2023) don't release MISO when they're not selected, which would corrupt the joystick readings. The driver never reads from the screen, so MISO isn't needed on any revision.
- **3Vo** is the board's own 3.3 V *output*. Never connect it to the BeagleY-AI's 3.3 V.
- **The 7" panel** connects to the RA8875 board with the 40-pin ribbon cable.
- **Power:** use a good 5 V / 3 A supply for the BeagleY-AI. The backlight draws far more than everything else.

## 2. One-time setup

### 2a. Build tools on the host

```
sudo apt install cmake gcc-aarch64-linux-gnu
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install libgpiod-dev:arm64
```

GPIO (encoder, joystick button, screen reset) uses **libgpiod v2**, which needs Debian 13 / Ubuntu 24.10 or newer on the host. Without it, set `CMAKE_EXTRA_ARGS="-DENABLE_GPIO=OFF"` in `config.sh`: the screen can't be reset and the encoder and joystick button won't work, but everything else does.

### 2b. Personal settings file

```
cp scripts/config.sh.example scripts/config.sh
nano scripts/config.sh
```

`config.sh` is git-ignored, so everyone has their own. Set these values:
- `BOARD_USER`: your username on the board.
- `BOARD_HOST`: the board's IP address (`192.168.7.2` over USB-C).
- `BOARD_DIR`: leave as `campus_tour`.
- `CMAKE_EXTRA_ARGS`: usually empty.

Check that you can log in first: `ssh <BOARD_USER>@<BOARD_HOST>`. `ssh-copy-id` stops the password prompts.

### 2c. Set up the board

SPI must be enabled first (course SPI guide, section 1: the `k3-am67a-beagley-ai-spidev0.dtbo` overlay). Then:

```
scripts/setup_board.sh
```

This installs `mpg123` and `alsa-utils`, then checks for:
- libgpiod v2,
- both SPI devices (`spidev0.0` for the ADC, `spidev0.1` for the screen),
- the USB audio adapter.

Fix anything it reports as `PROBLEM`.

## 3. Build, copy, check the hardware

```
scripts/deploy.sh                          # build, then copy the programs + audio to the board
scripts/run_on_board.sh display_test       # colour bars, a triangle and 4 text sizes on the screen
scripts/run_on_board.sh display_test --stress   # repeats the tour's kind of drawing to show up glitches
scripts/run_on_board.sh joystick_test      # move the stick / press it
scripts/run_on_board.sh audio_selftest     # every line should say PASS
scripts/run_on_board.sh tour_player        # the tour, with typed commands available
```

`run_on_board.sh` runs programs with `sudo`, because GPIO and SPI need root. If the tour service (section 5) is running, it's stopped first, so the two don't fight over the hardware.

## 4. Using the tour

| Control | What it does |
|---|---|
| **Knob: turn** | Move the highlight in the stop list. Never changes what is playing. |
| **Knob: press** | Pause / resume. If nothing is playing, play the highlighted stop. |
| **Knob: hold 1 s** | End the current track (nothing else). |
| **Stick: up / down** | Volume ±5%, repeating every 0.5 s while held. The stick must be pushed most of the way (70%) before anything happens. |
| **Stick: right / left** | Skip forward / back. While held, the bar and time preview the new spot (amber), at 10 s per second held. Letting go jumps there. Never before 0:00 or past 1 s before the end. |
| **Stick: press** | Show the directions to the highlighted stop, or close the popup. |

- **To switch tracks while one is playing:** turn to highlight the new stop, hold the knob to end the current track, then press to play.
- **When a track ends by itself:** the next stop is highlighted and the directions to it pop up. Press the stick to close them, then press the knob to play. After the last stop, a "Tour complete" popup appears.
- **Choosing a stop out of order** never shows directions automatically.
- **While a popup is open,** only the stick press (close) and the knob press (pause / resume a playing track) work.

**Saving.** The stop, position, volume and highlight are saved every 5 s while playing, and at once when they change, to `tour_state.txt` next to the program. Each save writes a new file and renames it over the old one, so pulling the power is safe. After a restart, the tour comes back **paused** at the saved spot: press the knob to continue. Delete `tour_state.txt` to start fresh.

**Typed commands (debugging).** When started with `run_on_board.sh`, typed commands imitate the devices. `h` lists them:

| Type | Same as |
|---|---|
| `+` / `-` (`+++`, `+5`) | Knob turn |
| `p` / `x` | Knob press / hold |
| `u` / `d` (`u3`) | Stick up / down |
| `f` / `b` (`f20` = 20 s) | Stick right / left, then let go |
| `g` | Stick press |
| `i`, `l`, `h`, `q` | Status, list of stops, help, quit |

**Options:**
- `--audio-dir DIR`
- `--device ALSA_DEVICE`
- `--log FILE`: timestamped messages; the file restarts past 1 MB.
- `--state FILE`
- `--no-display`
- `--spi-hz HZ`: screen SPI clock, default 500 kHz.

## 5. Start at power-on

```
scripts/service.sh install     # starts now and at every boot; restarts it if it crashes
scripts/service.sh log         # watch its messages live
scripts/service.sh stop        # until the next boot (start / restart / status also work)
scripts/service.sh uninstall   # stop starting at boot
```

The service runs `tour_player --log tour.log` from `BOARD_DIR`, with no keyboard input. `scripts/deploy.sh` stops it while copying and starts it again afterwards.

## Background repair

Breadboard wiring occasionally corrupts a drawing command, which shows up as a streak, a wrong colour or misplaced text. To repair this, the tour quietly repaints one small piece of the screen every 0.2 s (18 pieces for the main screen; 4–9 for a popup), so the whole screen is repainted about every 3.6 seconds. Pieces never overlap, and text is drawn with its own background, so repainting identical content is invisible: nothing is erased first, and there's no flicker. Each step takes at most about 0.1 s, so the controls stay responsive. The step interval is `GUI_REFRESH_STEP_MS` in `app/include/gui.h`.

## Adding recordings and directions

Put the MP3s in `audio/`, named with a number first so they sort in order: `01_Transportation_Centre.mp3`, `02_WAC_Bennett.mp3`, … The list shows "Transportation Centre", "WAC Bennett", and so on.

**Directions** to stop N (from stop N-1) go in a text file with the same name: `03_Convo_and_DFA.txt` next to `03_Convo_and_DFA.mp3`.
- If the file is missing, the popup shows "temp directions to location N".
- Stop 1 has no directions.
- Blank lines start new paragraphs.
- Text pasted from Word or Google Docs is fine: curly quotes and dashes are converted for the screen's font.
- About 250 characters fit in large text; longer text switches to the smaller font automatically.

Then run `scripts/deploy.sh`. No code changes are needed for new recordings or directions.

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| Screen stays dark | Check VIN is on 5 V, GND, and that the ribbon cable is fully in and latched. Check RST (pin 16) and CS (pin 26). Run `display_test`. |
| Screen lights up but shows noise or wrong colours | Signal problem on the SPI wires: keep them short, then try `display_test --spi-hz 250000`. If that works, run the tour with the same `--spi-hz`. |
| Occasional streaks, wrong colours or misplaced text in the tour | Bit errors on the SPI wires. The tour repairs these by itself within about 4 seconds (see "Background repair"), but fewer errors is still better. Run `display_test --stress` (and try `--spi-hz 250000`): anything outside the bar, the rows and the counter is a corrupted command. Fixes, in order: shorter SPI wires; a GND wire running right next to SCK; a 100 Ω resistor in series with SCK at the BeagleY-AI end (stops "ringing" that the screen reads as extra clock pulses); a lower `--spi-hz`. |
| `ra8875: cannot open /dev/spidev0.1` | SPI not enabled (section 2c), or not run with `sudo`. |
| Joystick does nothing, or directions are reversed | Run `joystick_test`. For reversed axes, build with `-DJOYSTICK_Y_UP_IS_HIGH=0` / `-DJOYSTICK_X_RIGHT_IS_HIGH=0` in `CMAKE_EXTRA_ARGS`. |
| Joystick acts on its own (e.g. volume keeps rising) | The stick was held or bumped while the program started: its resting position is measured at start-up. Restart without touching it. The dead zone (stick must be pushed 70% of the way) is `JOYSTICK_PRESS_FRACTION` in `hal/include/hal/joystick.h`. |
| Joystick readings jump around | Add the 0.1 µF capacitor at the ADC, and check that Vcc is 3.3 V. |
| `gpio: line GPIO17 not found` | Line names differ on your image: check with `gpioinfo` on the board and edit the names at the top of `hal/src/encoder.c` / `hal/include/hal/joystick.h` / `hal/include/hal/ra8875.h`. |
| `gpio: cannot request line` | Not run with `sudo`, or the service is still running (`scripts/service.sh stop`). |
| Turning the knob goes the wrong way | Swap the A and B wires, or set `INVERT_DIRECTION` to `true` in `hal/src/encoder.c`. |
| `Text file busy` while deploying | A hand-started `tour_player` is still running on the board. Quit it (`q`), then deploy again. |
| `could not start 'mpg123'` | Run `scripts/setup_board.sh`. |
| `Audio device: default (no USB audio adapter found...)` | Adapter not detected: check with `aplay -l` on the board. |
| `Playback problem: ... Device or resource busy` | Another program has the sound card: `sudo pkill mpg123`, or stop the service. |
| CMake: `libgpiod (v2) not found` | Section 2a, or build with `-DENABLE_GPIO=OFF`. |

## Project layout

```
hal/                             hardware access only
  include/hal/gpio.h             one GPIO line by name, on whichever gpiochip has it (libgpiod v2)
  include/hal/encoder.h          rotary encoder + button: turn / short / long press
  include/hal/joystick.h         joystick direction (auto-calibrating) + SEL button
  include/hal/spi_adc.h          MCP3208 ADC on /dev/spidev0.0
  include/hal/ra8875.h           RA8875 screen on /dev/spidev0.1: rectangles, triangles, text
  include/hal/press_detector.h   button debounce and long-press timing (pure logic)
  include/hal/audio_device.h     finds the USB sound card
app/
  src/main.c                     main loop: inputs -> controller -> screen -> save, every 100 ms
  src/tour_controller.c          the control rules; tour_controller_view() for the screen
  src/gui.c                      the screen layout: flicker-free pieces, redraws only what changed,
                                 plus the background repair
  src/joystick_input.c           joystick -> control events (repeat rates, skip release)
  src/guide_text.c               directions from the .txt files
  src/saved_state.c              save / resume file (atomic writes)
  src/input_queue.c              thread-safe queue: device threads -> main loop
  src/terminal_input.c           typed commands -> the same control events
  src/app_log.c                  all messages (terminal / service log / --log file)
  src/audio_player.c             plays MP3s via ALSA (controls mpg123)
  src/playback_display.c         m:ss / h:mm:ss text, progress fraction
  src/track_list.c               finds and names the MP3s
  include/canvas.h               drawing interface between gui.c and the screen
  test/                          display_test, joystick_test, audio_selftest
tests/                           hardware-free unit tests (run on the host)
tools/render_preview.py          turns gui_test's drawing data into PNG previews of the screen
audio/                           the MP3s and their .txt directions
scripts/                         build, deploy, run, setup, service
```

## Tests

Hardware-free tests run on the host. They need no libgpiod, sound card, screen, encoder or joystick:

```
cmake -S . -B build-host -DBUILD_HOST_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

| Test | Checks |
|---|---|
| `tour_controller_test` | Every control rule, against a fake player: highlight vs playing, pause, hold to end, volume, skip preview and single jump, end limit, popups and what works behind them, next-stop directions, tour complete, out-of-order stops, errors, resume. |
| `joystick_input_test` | Immediate first step, 0.5 s / 0.1 s repeats, one release per skip, one guide event per press. |
| `gui_test` | Every screen stays inside 800 × 480 with no text overflow; word wrap; only changed parts are redrawn; one repair cycle repaints every pixel without ever erasing visible text (no flicker). `--dump DIR` writes preview data. |
| `saved_state_test` | Save/load round trip, nonsense values, no leftover temporary file, when to save. |
| `guide_text_test` | `.txt` lookup, placeholder, stop 1, Word-style characters converted. |
| `press_detector_test` | Debounce, short press, long press fires once while held. |
| `joystick_logic_test` | Joystick direction from ADC values (fake ADC). |
| `terminal_input_test`, `input_queue_test`, `playback_display_test`, `audio_device_test` | Typed commands, the event queue, time formatting, sound card discovery. |

**Screen previews on a laptop:**
```
./build-host/tests/gui_test --dump previews
python3 tools/render_preview.py previews
```
This writes `previews/*.png`. The font is a stand-in for the RA8875's built-in one; sizes and positions are exact.
