# transport-cineform-tui

The command line as a transport layer, with a display on it. It sends one encode job to
`interactor-cineform` over the weft command bus and draws the progress coming back.

```
┌ interactor-cineform ─────────────────────────────────────────────────┐
│ cineform  /corpus/take01.rgba  ->  /out/take01.mkv                   │
│──────────────────────────────────────────────────────────────────────│
│ 1920x1080  60 fps  quality 2  RGB_444  running                       │
│ ▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆▆                    412/600        │
│──────────────────────────────────────────────────────────────────────│
│ frame=412 fps=118.34 q=2 size=296745KiB time=00:00:06.86 bitrate=... │
└──────────────────────────────────────────────────────────────────────┘
```

## Why it exists

`transport-bus-cli` terminates a terminal and prints one reply. This terminates a terminal
too, and the difference is the third service: an encode runs for minutes, so there is
something to *watch* rather than only something to wait for.

Everything below the input is the same — the same command service, the same 8-byte
request-id envelope, the same interactor — so a job that works here works from any other
transport layer, and one that does not is the interactor's fault rather than this
program's.

## The status line is ffmpeg's

Deliberately. Anybody who has encoded video knows what `frame=`, `fps=`, `q=`, `size=`,
`time=`, `bitrate=` and `speed=` mean, and a new vocabulary for the same six quantities
would be a cost with no benefit. `src/format.hpp` holds the formatting so the units are in
one place.

Two of its conventions are copied exactly rather than corrected:

- **`size=` is in kibibytes and labelled `kB`.** The label is inaccurate and the divisor is
  1024. A tool printing a number 2.4 percent different from ffmpeg's for the same file
  invites somebody to go looking for the discrepancy in the encoder.
- **`time=` is the duration of the OUTPUT**, not the wall clock spent making it. The two
  differ by `speed`, and confusing them is the most common misreading of ffmpeg's own line.

**No float ever crosses the bus.** `fps` and `speed` are carried as integers scaled by
1000 and the decimal point goes back in here. A float's bit pattern is a promise about two
compilers agreeing that nothing checks.

## Usage

```
cineform-tui [options] OUTPUT.mkv

  -i PATH        input, packed 8-bit RGBA, one frame after another. "-" is stdin.
  -testsrc       a deterministic test pattern instead. Needs -frames.
  -s WxH         frame size. Both must be even.
  -r FPS         frame rate written into the container. Default 30.
  -q N           quality ladder index, 0 low .. 5 filmscan3. Default 2.
  -frames N      how many frames the source holds. 0 means read until it ends.
  -threads N     encoder pool threads. 0 lets the pool choose.
  -alpha         encode RGBA_4444 instead of RGB_444.
  -nostats       plain lines instead of a full-screen display.
```

The encoder is a separate process. Start it through `service-cineform`, which also settles
the runtime directory and the library path both ends need.

**The display turns itself off when stdout is not a terminal**, and `-nostats` forces it
off. A full-screen redraw into a pipe writes cursor escapes into whatever is reading it.
In that mode it prints one line per sample, the way ffmpeg does, and drops repeats so a
stalled encode does not fill a log with the same frame number.

## What it does not decide

**The reply is the verdict.** A display that decided for itself whether a job had succeeded
would be reporting its own guess. The exit code is the interactor's.

**The elapsed time printed is the interactor's**, never a clock this process kept. A
duration measured outside the program that did the work has been wrong here before, which
is why `transport-bus-cli` prints the far end's number and none of its own.

**A reply that does not carry this caller's request id is dropped.** The bus is
asynchronous and the reply service is a broadcast, so a reply to somebody else's job would
be a wrong answer rather than a missing one. RFD 207d settles this for the RunPod transport
and the reasoning does not change for a terminal.

## The gauge, and when there isn't one

A bar appears only when the source said how long it is. An unbounded stdin stream has no
percentage, and a bar creeping toward a total nobody knows is a worse answer than no bar —
so that case prints a frame count and says the source is unbounded.

## Build

    repo init -u https://github.com/weftspun/transport-cineform-tui -m default.xml
    repo sync
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel

`interactor-cineform` is in the manifest **for its headers only**. `wire.hpp` and
`progress_bus.hpp` define the four things iceoryx2 compares at connect time, and two copies
of those would drift. Nothing here links the encoder and this build never compiles a line
of it — `src/job.cpp` is the one shared source file, compiled twice from one copy.

## What was measured

Built on Windows 11, clang 22.1.8, against FTXUI at `182ef70cd4dc` and contract-bus at
`f9f1ddcd9341`. Ran against a live `interactor-cineform` over iceoryx2 v0.9.3.

`proof/options_test.cpp` is 30 checks, twelve of them negative controls. Two real defects
came out of writing them, and both would have produced a successful run rather than an
error:

- **`-s` was not actually required.** The job struct defaults to 1920×1080, so the
  zero-check could never see a missing flag: omitting `-s` encoded a 1080p file and
  reported success, at a size the user never named. Absence is now checked separately from
  zero.
- **`-help` lost to any earlier bad flag.** The scan was folded into the validating loop,
  which exits on the first error, so `cineform-tui -nosuchflag -help` reported the unknown
  flag and never printed the usage text — the one line a confused user types was the one
  line that refused to help them. Help is now looked for before anything is validated.

An odd frame size is refused here as well as in the interactor, so the answer arrives
before a bus round trip rather than after one.

## Licence

`Apache-2.0 OR MIT`.
