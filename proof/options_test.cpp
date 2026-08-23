// The command line, checked against input that must be refused as well as input that must be
// accepted.
//
// Option parsing is where a tool most easily lies to its user: a size that half-parses, a
// flag that silently wins over an earlier one, a missing value read off the end of argv. Each
// of those produces a run rather than an error, and the run encodes the wrong thing.
//
// The population is fixed -- every flag the parser knows -- so it is enumerated, and there is
// no detection floor to state.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "options.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string &what) {
	checks++;
	std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok) {
		failures++;
	}
}

cineform::Options parse(std::vector<const char *> args) {
	args.insert(args.begin(), "cineform-tui");
	// parse_options takes char**, matching main's signature. The strings are literals and
	// the parser never writes to them.
	return cineform::parse_options(int(args.size()), const_cast<char **>(args.data()));
}

} // namespace

int main() {
	std::printf("Accepted: a complete command line\n");
	{
		const cineform::Options o = parse({"-i", "in.raw", "-s", "1920x1080", "-r", "60", "-q",
				"4", "-frames", "600", "-threads", "12", "-alpha", "out.mkv"});
		check(o.parse_error.empty(), "parses without error");
		check(o.job.input == "in.raw", "input");
		check(o.job.output == "out.mkv", "output");
		check(o.job.width == 1920 && o.job.height == 1080, "size splits on x");
		check(o.job.fps == 60, "rate");
		check(o.job.quality == 4, "quality");
		check(o.job.total_frames == 600, "frames");
		check(o.job.threads == 12, "threads");
		check(o.job.keep_alpha, "alpha");
		check(o.job.source == cineform::SOURCE_RAW_RGBA, "source defaults to raw");
	}

	std::printf("\nAccepted: defaults when the line is short\n");
	{
		const cineform::Options o = parse({"-s", "64x64", "out.mkv"});
		check(o.parse_error.empty(), "parses without error");
		check(o.job.fps == 30, "rate defaults to 30");
		check(o.job.quality == 2, "quality defaults to 2");
		check(o.job.threads == 0, "threads defaults to 0");
		check(o.job.input == "-", "input defaults to stdin");
		check(!o.job.keep_alpha, "alpha defaults off");
	}

	std::printf("\nAccepted: testsrc with a length\n");
	{
		const cineform::Options o = parse({"-testsrc", "-s", "64x64", "-frames", "10", "o.mkv"});
		check(o.parse_error.empty(), "parses without error");
		check(o.job.source == cineform::SOURCE_TEST_PATTERN, "source is the pattern");
	}

	std::printf("\nNegative controls: command lines that MUST be refused\n");

	// A size that half-parses is the dangerous one. sscanf would take "1920x1080garbage" and
	// use the good prefix, so a typo becomes a successful encode at a size nobody asked for.
	check(!parse({"-s", "1920x1080garbage", "o.mkv"}).parse_error.empty(),
			"a size with a trailing tail is refused");
	check(!parse({"-s", "1920", "o.mkv"}).parse_error.empty(),
			"a size with no x is refused");
	check(!parse({"-s", "x1080", "o.mkv"}).parse_error.empty(),
			"a size with no width is refused");
	check(!parse({"-s", "1920x", "o.mkv"}).parse_error.empty(),
			"a size with no height is refused");

	// A flag needing a value, last on the line, would read past the end of argv.
	check(!parse({"-s", "64x64", "-q"}).parse_error.empty(),
			"a value flag at the end of the line is refused, not read past");

	// Two bare words: last-one-wins would silently encode to a file the user did not name.
	check(!parse({"-s", "64x64", "a.mkv", "b.mkv"}).parse_error.empty(),
			"two outputs are refused rather than last-one-wins");

	check(!parse({"-s", "64x64"}).parse_error.empty(), "no output is refused");
	check(!parse({"o.mkv"}).parse_error.empty(), "no size is refused");
	check(!parse({"-s", "0x0", "o.mkv"}).parse_error.empty(), "a zero size is refused");
	check(!parse({"-nosuchflag", "-s", "64x64", "o.mkv"}).parse_error.empty(),
			"an unknown flag is refused rather than ignored");

	// testsrc has no end of its own, so an unbounded one would encode until the disk filled.
	check(!parse({"-testsrc", "-s", "64x64", "o.mkv"}).parse_error.empty(),
			"testsrc without -frames is refused");

	// -help must win over a broken line, or a user who cannot get the syntax right cannot
	// read the syntax either.
	{
		const cineform::Options o = parse({"-nosuchflag", "-help"});
		check(o.show_help, "-help is honoured even after a bad flag");
	}

	std::printf("\n%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
