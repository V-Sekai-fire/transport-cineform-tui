// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "options.hpp"

#include <cstdlib>
#include <cstring>

namespace cineform {

const char *usage_text() {
	return
		"cineform-tui: send an encode to interactor-cineform and watch it run.\n"
		"\n"
		"usage: cineform-tui [options] OUTPUT.mkv\n"
		"\n"
		"  -i PATH        input, packed 8-bit RGBA, one frame after another. \"-\" is stdin.\n"
		"  -testsrc       encode a deterministic test pattern instead of reading input.\n"
		"                 Needs -frames, because a pattern has no end of its own.\n"
		"  -s WxH         frame size. Both must be even; CineForm works in 8x8 blocks.\n"
		"  -r FPS         frame rate written into the container. Default 30.\n"
		"  -q N           quality ladder index, 0 low .. 5 filmscan3. Default 2 (high).\n"
		"  -frames N      how many frames the source holds. 0 means read until it ends.\n"
		"  -threads N     encoder pool threads. 0 lets the pool choose. Default 0.\n"
		"  -alpha         encode RGBA_4444 instead of RGB_444.\n"
		"  -nostats       plain lines instead of a full-screen display.\n"
		"  -h, -help      this text.\n"
		"\n"
		"The encoder is a separate process. Start it first, or through service-cineform,\n"
		"and make sure both ends can find libiceoryx2_ffi_c (WEFT_ICEORYX2_PATH).\n";
}

Options parse_options(int argc, char **argv) {
	Options o;
	bool have_output = false;
	bool have_size = false;
	bool testsrc = false;

	// Help is looked for BEFORE anything is validated, and the reason is a measured one: with
	// the scan folded into the loop below, `-nosuchflag -help` reported the unknown flag and
	// never printed the usage text, so the one line a confused user types to find the right
	// spelling was the one line that refused to help them.
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "-help") == 0 ||
				std::strcmp(argv[i], "--help") == 0) {
			o.show_help = true;
			return o;
		}
	}

	// A flag needing a value that sits last on the line would read past the end of argv, so
	// every one of them is checked rather than trusted.
	auto value = [&](int &i) -> const char * {
		if (i + 1 >= argc) {
			o.parse_error = std::string(argv[i]) + " needs a value";
			return nullptr;
		}
		return argv[++i];
	};

	for (int i = 1; i < argc && o.parse_error.empty(); i++) {
		const char *a = argv[i];
		if (std::strcmp(a, "-i") == 0) {
			if (const char *v = value(i)) {
				o.job.input = v;
			}
		} else if (std::strcmp(a, "-testsrc") == 0) {
			testsrc = true;
		} else if (std::strcmp(a, "-s") == 0) {
			if (const char *v = value(i)) {
				// `1920x1080`, and nothing else. sscanf would accept "1920x1080garbage" and
				// silently use the good prefix, so the separator and the tail are checked.
				const char *x = std::strchr(v, 'x');
				if (x == nullptr || x == v || x[1] == '\0') {
					o.parse_error = std::string("-s wants WxH, got ") + v;
				} else {
					char *end = nullptr;
					have_size = true;
					o.job.width = uint32_t(std::strtoul(v, &end, 10));
					if (end != x) {
						o.parse_error = std::string("-s wants WxH, got ") + v;
					} else {
						o.job.height = uint32_t(std::strtoul(x + 1, &end, 10));
						if (*end != '\0') {
							o.parse_error = std::string("-s wants WxH, got ") + v;
						}
					}
				}
			}
		} else if (std::strcmp(a, "-r") == 0) {
			if (const char *v = value(i)) {
				o.job.fps = uint32_t(std::atoi(v));
			}
		} else if (std::strcmp(a, "-q") == 0) {
			if (const char *v = value(i)) {
				o.job.quality = uint32_t(std::atoi(v));
			}
		} else if (std::strcmp(a, "-frames") == 0) {
			if (const char *v = value(i)) {
				o.job.total_frames = uint64_t(std::strtoull(v, nullptr, 10));
			}
		} else if (std::strcmp(a, "-threads") == 0) {
			if (const char *v = value(i)) {
				o.job.threads = uint32_t(std::atoi(v));
			}
		} else if (std::strcmp(a, "-ar") == 0) {
			if (const char *v = value(i)) {
				o.job.mix_rate = uint32_t(std::atoi(v));
			}
		} else if (std::strcmp(a, "-ac") == 0) {
			if (const char *v = value(i)) {
				o.job.channels = uint32_t(std::atoi(v));
			}
		} else if (std::strcmp(a, "-abits") == 0) {
			if (const char *v = value(i)) {
				o.job.audio_bits = uint32_t(std::atoi(v));
			}
		} else if (std::strcmp(a, "-alpha") == 0) {
			o.job.keep_alpha = true;
		} else if (std::strcmp(a, "-nostats") == 0) {
			o.tui = false;
		} else if (a[0] == '-' && a[1] != '\0') {
			o.parse_error = std::string("unknown option ") + a;
		} else {
			// A second bare word would silently replace the first, so two outputs is an error
			// rather than a last-one-wins.
			if (have_output) {
				o.parse_error = std::string("more than one output: ") + o.job.output + " and " + a;
			}
			o.job.output = a;
			have_output = true;
		}
	}

	if (!o.parse_error.empty()) {
		return o;
	}
	if (!have_output) {
		o.parse_error = "no output file given";
		return o;
	}
	if (testsrc) {
		o.job.source = SOURCE_TEST_PATTERN;
		if (o.job.total_frames == 0) {
			o.parse_error = "-testsrc needs -frames; a generated pattern has no end of its own";
			return o;
		}
	}
	// ABSENCE IS CHECKED SEPARATELY FROM ZERO, because Job's defaults are 1920x1080 and a
	// zero test can never see a missing flag. Measured: with only the zero test, omitting -s
	// encoded a 1080p file and reported success, which is the worst kind of wrong answer --
	// the user gets a valid file at a size they never named.
	if (!have_size) {
		o.parse_error = "-s is required; there is no default frame size";
		return o;
	}
	if (o.job.width == 0 || o.job.height == 0) {
		o.parse_error = "-s needs both dimensions non-zero";
		return o;
	}
	// The codec works in 8x8 wavelet blocks and reports an odd size late and unhelpfully, so
	// it is caught here where the message can name what was asked for. The interactor checks
	// again; this one exists so the answer arrives before a bus round trip.
	if ((o.job.width & 1u) || (o.job.height & 1u)) {
		o.parse_error = "CineForm needs even dimensions, got " + std::to_string(o.job.width) +
				"x" + std::to_string(o.job.height);
		return o;
	}

	// Audio is off unless -ar names a rate, so the other two settings are meaningless on
	// their own. Accepting them silently would let `-ac 6` look like it had done something
	// to a file with no audio track in it at all.
	if (o.job.mix_rate == 0) {
		return o;
	}
	if (o.job.audio_bits != 16 && o.job.audio_bits != 32) {
		o.parse_error = "-abits must be 16 or 32, got " + std::to_string(o.job.audio_bits);
		return o;
	}
	if (o.job.channels == 0 || o.job.channels > 8) {
		o.parse_error = "-ac must be 1 to 8, got " + std::to_string(o.job.channels);
		return o;
	}
	// Godot computes one frame's audio as mix_rate / fps with integer division, and this
	// interleaved layout is built on that figure. A rate below the frame rate gives zero
	// samples per frame, which is a silent track rather than an error unless it is caught.
	if (o.job.mix_rate < o.job.fps) {
		o.parse_error = "-ar " + std::to_string(o.job.mix_rate) + " is below the frame rate " +
				std::to_string(o.job.fps) + ", which is zero samples per frame";
	}
	return o;
}

} // namespace cineform
