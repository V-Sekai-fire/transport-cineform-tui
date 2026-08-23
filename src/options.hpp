// The command line, which is ffmpeg's where the two tools mean the same thing.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
#ifndef CINEFORM_OPTIONS_HPP
#define CINEFORM_OPTIONS_HPP

#include "cineform/job.hpp"

#include <string>

namespace cineform {

struct Options {
	Job job;
	// Off when stdout is not a terminal, or when -nostats is given. A full-screen redraw into
	// a pipe writes escape sequences to a log file nobody can read.
	bool tui = true;
	bool show_help = false;
	std::string parse_error;
};

// Parses argv into a job. `-s 1920x1080` sets both dimensions, as in ffmpeg.
Options parse_options(int argc, char **argv);

// The usage text, as one string, so the same words go to a terminal and to an error stream.
const char *usage_text();

} // namespace cineform

#endif
