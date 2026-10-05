# transport-cineform-tui

A terminal front end that sends one encode job to interactor-cineform over the command bus and draws its progress.

## What it is for

An encode runs for minutes, so this transport gives it a live status line in the vocabulary of a common video encoder's log. It decides nothing itself: the interactor's reply is the verdict, its elapsed time is the one printed, and its result sets the exit code. RFD 1137 owns the topic.

## Build and run

```sh
repo init -u https://github.com/V-Sekai-fire/transport-cineform-tui -m default.xml
repo sync
cmake -B build
cmake --build build
```

Inside the workspace, build against the composed checkout instead, because `repo init` here re-points the workspace's goal manifest. `cineform-tui -help` lists the options.

## Licence

Apache-2.0 OR MIT; see `LICENSE-APACHE` and `LICENSE-MIT`.
