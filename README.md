SFU Burnaby Campus Tour: audio player and joystick HAL
An interactive audio guide for the BeagleY-AI.

joystick_test: Live display of the joystick (raw ADC values, voltage, a bar, and the detected direction). Use it to confirm the wiring. 
audio_selftest: Plays a file and checks play / pause / resume / volume / stop. Use it to confirm the sound path. |
tour_player: The main program. Type a track number to play it, then pause, change volume, skip, etc. (The terminal is a stand-in until the keypad arrives.) |

Everything is written in C and built with CMake. Hardware access is isolated in `hal/`, the application logic is in `app/`.

    One-time setup guide:

Your board needs to be reachable by SSH, has SPI enabled, and has the USB audio adapter plugged in.

Install the build tools on the host:

sudo apt install cmake gcc-aarch64-linux-gnu


Create your personal settings file

cp scripts/config.sh.example scripts/config.sh
nano scripts/config.sh

`config.sh` is git-ignored, so make your own. Set these values:

`BOARD_USER` is your username on the board. The name you chose when flashing the SD card. It is what you type in `ssh <name>@192.168.7.2`.

`BOARD_HOST` is the board's IP address. `192.168.7.2` if the board is connected to your computer by USB-C. Same as ip when connecting with ssh.

`BOARD_DIR` Folder on the board that receives the programs. Leave as `campus_tour` (inside your home folder on the board; created automatically). 

`CMAKE_EXTRA_ARGS` is optional build options. Leave empty.

Check that you can log in before going on: `ssh <BOARD_USER>@<BOARD_HOST>` (type `exit` to come back). Typing the password every time is normal; `ssh-copy-id <BOARD_USER>@<BOARD_HOST>` stops it asking.

    Set up the board
run:

scripts/setup_board.sh

This installs `mpg123` and `alsa-utils` on the board (the board needs internet for this; see your course's networking guide) and then checks that `/dev/spidev0.0` exists and that the USB audio adapter is detected. Fix anything it reports as `PROBLEM` before continuing.

If `apt` complains about the BeagleBoard package server ("Mirror sync in progress"), wait a few minutes and run the script again.


    2. Build and copy to the board
```
scripts/deploy.sh
```
This builds (cross-compiles) and copies the three programs and the `audio/` folder to `BOARD_DIR` on the board. Useful options: `--no-build` (just copy) and `--no-audio` (skip the audio files, which are the big part).

    3. Run it

All programs are started from your host with `scripts/run_on_board.sh`. It logs in over SSH and runs them with `sudo`, because the SPI device needs root. It may ask for the board's password.

    Step 1: joystick
```
scripts/run_on_board.sh joystick_test
```
Move the stick. You should see something like:
```
X 2051 1.652V [----------#|---------]  Y 2047 1.649V [----------#|---------]  -> none
```
- Centre reads about 2048 (1.65 V). Pushing to an extreme goes toward 0 or 4095.
- Push UP: the line should end in `UP`. Push LEFT: it should end in `LEFT`.
- `scripts/run_on_board.sh joystick_test --samples 50` prints 50 readings and a min/max summary, which is handy for a bug report.
- If it prints `cannot open /dev/spidev0.0`, SPI is not enabled (run `scripts/setup_board.sh` and read its report).

    Step 2: audio
```
scripts/run_on_board.sh audio_selftest
```
You should hear about five seconds of a tone with a short silence in the middle, and every line should say `PASS`.

    Step 3: the player
```
scripts/run_on_board.sh tour_player
```
| Type | Does |
|---|---|
| a number, e.g. `2` | plays that track |
| `p` | pause / resume |
| `s` | stop |
| `+` / `-` | volume up / down (10% steps) |
| `f` / `b` | skip forward / back 10 seconds |
| `l` | list the tracks |
| `i` | show what is playing, the time, and the volume |
| `h` | help |
| `q` (or Ctrl-C) | quit |

When a recording ends, the player says `Finished: <name>. Choose another stop.`

Options: `--audio-dir DIR` (folder of MP3s), `--device ALSA_DEVICE` (see section 6). Example: `scripts/run_on_board.sh tour_player --audio-dir /home/alex/my_tour`.

---

    Adding the real tour audio

Put the MP3 files in the `audio/` folder of the project and run `scripts/deploy.sh`. Name them with a number first so they play in order: `01_Welcome_to_SFU.mp3`, `02_Academic_Quadrangle.mp3`, ... The menu shows "Welcome to SFU", "Academic Quadrangle", and so on. Delete the `*_Test_Tone.mp3` placeholders when real recordings arrive.


    Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `Permission denied` when starting a script | Run once: `chmod +x scripts/*.sh` (some ways of copying files lose the executable flag). |
| `No scripts/config.sh yet` | Do step 1b. |
| `Cannot reach alex@192.168.7.2` | Board still booting (wait 60 s), wrong `BOARD_USER` / `BOARD_HOST`, or the USB network is not set up. Try `ssh` by hand. |
| `spi_adc: cannot open /dev/spidev0.0` | SPI is not enabled on that board, or you ran the program without `sudo` (the scripts already use it). |
| `could not start 'mpg123'` | Run `scripts/setup_board.sh` (installs it). |
| `Audio device: default (no USB audio adapter found...)` | Adapter not plugged in or not detected: `aplay -l` on the board. |
| Plays but no sound | Wrong output device (use `--device`, section 6), or the speakers/headphones are not plugged into the adapter. Check volume with `i`. |
| `Playback problem: ... Device or resource busy` | Another program is using the sound card. Make sure no old `mpg123` is running: `pkill mpg123`. |
| Joystick up/down or left/right reversed | Section 4. |
| Build error `aarch64-linux-gnu-gcc: not found` | Step 1a. |

---

    Project layout

```
hal/                       hardware access only
  include/hal/spi_adc.h    MCP3208 ADC over SPI (/dev/spidev0.0)
  include/hal/joystick.h   joystick: raw values, direction, auto-calibration
  include/hal/audio_device.h   finds the USB sound card
app/
  src/main.c               the tour player (menu loop)
  src/audio_player.c       plays MP3s via ALSA (controls the mpg123 player)
  src/track_list.c         finds and names the MP3 files
  src/terminal_input.c     typed commands -> input events (temporary input source)
  test/                    joystick_test, audio_selftest
tests/                     hardware-free unit tests (run on the host)
audio/                     the MP3 files
scripts/                   config.sh.example, build.sh, deploy.sh, run_on_board.sh, setup_board.sh
cmake/aarch64-toolchain.cmake
```

    How the pieces are meant to grow
- **Keypad:** `main.c` only deals with `input_event_t` values (select track, pause, volume ...). Add a `keypad_input.c` that returns the same events, and swap it in where `terminal_input_poll()` is called. The player code does not change.
- **Joystick as input:** use `joystick_get_direction()` to produce the same events (for example left/right = previous/next, up/down = volume).
- **Display:** `audio_player_get_status()` already provides the state, position, duration, and volume that the screen needs.
- **Save and resume:** save the current track number and `position_sec` to a file; on start, play that track and use `audio_player_seek_relative()` to return to the saved spot.
- **Audio back end:** `audio_player.h` hides the fact that `mpg123` does the decoding. If you ever want in-process decoding (libmpg123 + ALSA), only `audio_player.c` changes.


    Tests

Hardware-free tests (joystick direction logic against a fake ADC, and sound card discovery) run on the host:
```
cmake -S . -B build-host -DBUILD_HOST_TESTS=ON
cmake --build build-host
./build-host/tests/joystick_logic_test
./build-host/tests/audio_device_test
```

