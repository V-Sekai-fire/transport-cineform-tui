// How a number is shown, in one place.
//
// ffmpeg's status line is the reference and it is copied deliberately: anybody who has
// encoded video knows what `frame=`, `fps=`, `size=`, `time=`, `bitrate=` and `speed=` mean,
// and a new vocabulary for the same six quantities would be a cost with no benefit.
//
// The formatting lives here rather than at each call site because CLAUDE.md's rule about
// repeated measurements applies: where a program prints the same kind of quantity in several
// places, it gets a helper instead of relying on whoever edits it next to recall the units.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
#ifndef CINEFORM_FORMAT_HPP
#define CINEFORM_FORMAT_HPP

#include <cstdint>
#include <cstdio>
#include <string>

namespace cineform {

// ffmpeg reports size in kibibytes and labels it `kB`. The label is theirs and it is
// inaccurate; the divisor is 1024. It is copied exactly rather than corrected, because a tool
// that prints a number 2.4 percent different from ffmpeg's for the same file invites somebody
// to go looking for the discrepancy in the encoder.
inline std::string size_kb(uint64_t bytes) {
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%lluKiB", (unsigned long long)(bytes / 1024ULL));
	return buf;
}

// HH:MM:SS.ss of movie produced, from the frame count and the rate. This is the duration of
// the OUTPUT, not the wall clock spent making it; the two differ by `speed` and confusing
// them is the most common misreading of ffmpeg's own line.
inline std::string timestamp(uint64_t frames, uint32_t fps) {
	if (fps == 0) {
		return "00:00:00.00";
	}
	const uint64_t hundredths = frames * 100ULL / fps;
	const uint64_t total_s = hundredths / 100ULL;
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu.%02llu",
			(unsigned long long)(total_s / 3600ULL), (unsigned long long)((total_s / 60ULL) % 60ULL),
			(unsigned long long)(total_s % 60ULL), (unsigned long long)(hundredths % 100ULL));
	return buf;
}

// kbit/s over the movie's own duration, which is what a bitrate means. Dividing by wall clock
// instead would make a fast machine look like it produced a smaller file.
inline std::string bitrate_kbits(uint64_t bytes, uint64_t frames, uint32_t fps) {
	if (frames == 0 || fps == 0) {
		return "N/A";
	}
	// bits per second = bytes * 8 * fps / frames. Multiply before dividing so the ratio keeps
	// its precision at low frame counts.
	const uint64_t bits_per_s = bytes * 8ULL * uint64_t(fps) / frames;
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%llu.%llukbits/s", (unsigned long long)(bits_per_s / 1000ULL),
			(unsigned long long)((bits_per_s % 1000ULL) / 100ULL));
	return buf;
}

// The wire carries integers scaled by 1000, so this is where the decimal point goes back in.
// No float ever crosses the bus; see wire.hpp.
inline std::string milli(uint32_t scaled, const char *suffix) {
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%u.%02u%s", scaled / 1000u, (scaled % 1000u) / 10u, suffix);
	return buf;
}

} // namespace cineform

#endif
