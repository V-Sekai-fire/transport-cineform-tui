// The command line as a transport layer, with a display on it.
//
// `transport-bus-cli` terminates a terminal and prints one reply. This terminates a terminal
// too, and the difference is the third service: an encode runs for minutes, so there is
// something to watch rather than only something to wait for. Everything below the input is
// the same -- same command service, same 8-byte request-id envelope, same interactor -- so a
// job that works here works from any other transport layer, and one that does not is the
// interactor's fault rather than this program's.
//
// The status line is ffmpeg's, deliberately. See format.hpp.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "cineform/job.hpp"
#include "cineform/progress_bus.hpp"
#include "cineform/wire.hpp"

#include "weft/cbor.hpp"
#include "weft/command.hpp"

#include "command_client.hpp"
#include "format.hpp"
#include "options.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace {

using namespace ftxui;

// This caller's request id. The reply subscriber drops anything not carrying this exact
// value, so two callers choosing the same number see each other's replies refused rather
// than accepted.
constexpr uint64_t REQUEST_ID = 1;

std::string state_word(uint32_t state) {
	switch (state) {
		case cineform::STATE_IDLE: return "idle";
		case cineform::STATE_RUNNING: return "running";
		case cineform::STATE_DONE: return "done";
		case cineform::STATE_FAILED: return "failed";
		default: return "unknown";
	}
}

// The one line ffmpeg prints, from a Progress sample.
std::string status_line(const cineform::Progress &p, uint32_t fps) {
	return "frame=" + std::to_string(p.frame) +
			" fps=" + cineform::milli(p.fps_milli, "") +
			" q=" + std::to_string(p.quality) +
			" size=" + cineform::size_kb(p.bytes_out) +
			" time=" + cineform::timestamp(p.frame, fps) +
			" bitrate=" + cineform::bitrate_kbits(p.bytes_out, p.frame, fps) +
			" speed=" + cineform::milli(p.speed_milli, "x");
}

Element render(const cineform::Options &opt, const cineform::Progress &p, bool have_progress) {
	const std::string source = opt.job.source == cineform::SOURCE_TEST_PATTERN
			? std::string("testsrc")
			: opt.job.input;

	Elements rows;
	rows.push_back(hbox({
			text(" cineform ") | bold | inverted,
			text(" " + source + "  ->  " + opt.job.output),
	}));
	rows.push_back(separator());
	rows.push_back(hbox({
			text(" " + std::to_string(opt.job.width) + "x" + std::to_string(opt.job.height)),
			text("  " + std::to_string(opt.job.fps) + " fps"),
			text("  quality " + std::to_string(opt.job.quality)),
			text(opt.job.keep_alpha ? "  RGBA_4444" : "  RGB_444"),
			text("  " + state_word(have_progress ? p.state : cineform::STATE_IDLE)),
	}));

	// A gauge only when the source said how long it is. An unbounded stdin stream has no
	// percentage, and a bar that invents one -- creeping toward a total nobody knows -- is a
	// worse answer than no bar.
	if (p.total_frames > 0) {
		float ratio = float(double(p.frame) / double(p.total_frames));
		if (ratio > 1.0f) {
			ratio = 1.0f;
		}
		rows.push_back(hbox({
				text(" "),
				gauge(ratio) | flex,
				text(" " + std::to_string(p.frame) + "/" + std::to_string(p.total_frames) + " "),
		}));
	} else {
		rows.push_back(text(" frames " + std::to_string(p.frame) + " of an unbounded source"));
	}

	rows.push_back(separator());
	rows.push_back(text(" " + status_line(p, opt.job.fps)));
	return window(text(" interactor-cineform "), vbox(std::move(rows)));
}

// Reads the interactor's reply. The reply is the verdict; a display that decided for itself
// whether a job succeeded would be reporting its own guess.
int report_reply(const std::vector<unsigned char> &reply, const cineform::Options &opt) {
	weft::cbor::Reading r(reply.data(), reply.size());
	if (!r.ok()) {
		std::fprintf(stderr, "cineform-tui: the reply was not a well-formed CBOR map\n");
		return 1;
	}
	std::string status;
	if (!r.text("status", status)) {
		std::fprintf(stderr, "cineform-tui: the reply carried no status\n");
		return 1;
	}
	if (status != "ok") {
		std::string message = "(none given)";
		r.text("error", message);
		std::fprintf(stderr, "cineform-tui: %s\n", message.c_str());
		return 1;
	}

	uint64_t frames = 0;
	uint64_t bytes = 0;
	uint64_t elapsed_ms = 0;
	r.uint("frames", frames);
	r.uint("bytes", bytes);
	r.uint("elapsed_ms", elapsed_ms);

	// The interactor's milliseconds, never a clock this process kept. A duration measured
	// outside the program that did the work has been wrong here before, which is why
	// transport-bus-cli prints the far end's number and none of its own.
	std::printf("%s: %llu frames, %s, %llu ms in the encoder\n", opt.job.output.c_str(),
			(unsigned long long)frames, cineform::size_kb(bytes).c_str(),
			(unsigned long long)elapsed_ms);
	return 0;
}

void draw(const cineform::Options &opt, const cineform::Progress &p, bool have_progress,
		bool final_frame) {
	Element document = render(opt, p, have_progress);
	Screen screen = Screen::Create(Dimension::Full(), Dimension::Fit(document));
	Render(screen, document);
	std::printf("%s", screen.ToString().c_str());
	if (final_frame) {
		// Let the shell prompt land below the last drawn line rather than on top of it.
		std::printf("\n");
	} else {
		std::printf("\033[%dA", screen.dimy());
	}
	std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv) {
	cineform::Options opt = cineform::parse_options(argc, argv);
	if (opt.show_help) {
		std::printf("%s", cineform::usage_text());
		return 0;
	}
	if (!opt.parse_error.empty()) {
		std::fprintf(stderr, "cineform-tui: %s\n\n%s", opt.parse_error.c_str(),
				cineform::usage_text());
		return 2;
	}
	// A full-screen redraw into a pipe writes cursor escapes into whatever is reading it, so
	// the display turns itself off when stdout is not a terminal. -nostats forces it off too.
	if (!isatty(fileno(stdout))) {
		opt.tui = false;
	}

	if (!weft::load_bus()) {
		return 1;
	}

	// The progress subscriber opens BEFORE the command is sent. Opening it afterwards races
	// the encoder, and a short job could be finished before this end was listening.
	cineform::ProgressSubscriber progress;
	if (!progress.open()) {
		return 1;
	}
	cineform::CommandClient client;
	if (!client.open()) {
		return 1;
	}

	std::vector<unsigned char> body(weft::BODY_MAX);
	const size_t n = cineform::job_encode(opt.job, body.data(), body.size());
	if (n == 0) {
		std::fprintf(stderr, "cineform-tui: the job did not fit one bus message\n");
		return 1;
	}
	if (!client.send(REQUEST_ID, body.data(), n)) {
		std::fprintf(stderr,
				"cineform-tui: the bus refused the command. Is interactor-cineform running?\n");
		return 1;
	}

	cineform::Progress p{};
	p.total_frames = opt.job.total_frames;
	p.quality = opt.job.quality;
	bool have_progress = false;
	std::vector<unsigned char> reply;
	std::string last_line;

	for (;;) {
		if (progress.latest(&p)) {
			have_progress = true;
			if (opt.tui) {
				draw(opt, p, have_progress, false);
			} else {
				// One line per sample, the way ffmpeg writes when it is not on a terminal.
				// Identical consecutive lines are dropped so a stalled encode does not fill
				// a log with the same frame number over and over.
				const std::string line = status_line(p, opt.job.fps);
				if (line != last_line) {
					std::printf("%s\n", line.c_str());
					std::fflush(stdout);
					last_line = line;
				}
			}
		}

		if (client.poll_reply(REQUEST_ID, &reply)) {
			break;
		}
		// 10 ms, which is `proof/subscriber.cpp`'s poll interval. A display redrawing at
		// 100 Hz is already faster than a terminal is worth updating.
		progress.wait(10 * 1000 * 1000);
	}

	if (opt.tui) {
		draw(opt, p, have_progress, true);
	}
	return report_reply(reply, opt);
}
