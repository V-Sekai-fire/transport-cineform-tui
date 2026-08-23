// The caller's half of the command bus.
//
// `weft/loop.hpp` is the server shape: one `ask` answering many commands. A caller sending
// one command and waiting for its own reply is a different shape, and `contract-bus`'s own
// `proof/command_publisher.cpp` says why it writes the raw ABI rather than wrapping it -- a
// client-side wrapper for a two-line difference is an abstraction that grows a copy in every
// caller and then drifts. This is a caller, so it is written here.
//
// CORRELATION IS BY REQUEST ID, AND A REPLY THAT DOES NOT MATCH IS DROPPED. The bus is
// asynchronous and the reply service is a broadcast: every caller sees every reply. A reply
// to somebody else's job, or to one this process already gave up on, would be a WRONG answer
// rather than a missing one, which is worse. RFD 207d settles this for the RunPod transport
// and the reasoning does not change for a terminal.
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
#ifndef CINEFORM_COMMAND_CLIENT_HPP
#define CINEFORM_COMMAND_CLIENT_HPP

#include "iox2_api.h"
#include "weft/bus.hpp"
#include "weft/command.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace cineform {

class CommandClient {
public:
	bool open() {
		if (iox2_node_builder_create(iox2_node_builder_new(nullptr), nullptr,
					iox2_service_type_e_IPC, &node_) != IOX2_OK) {
			std::fprintf(stderr, "cineform-tui: no node\n");
			return false;
		}
		cmd_service_ = open_service(weft::COMMAND_SERVICE_NAME);
		reply_service_ = open_service(weft::REPLY_SERVICE_NAME);
		if (!cmd_service_ || !reply_service_) {
			std::fprintf(stderr, "cineform-tui: could not open the command or reply service\n");
			return false;
		}

		auto pub_builder = iox2_port_factory_pub_sub_publisher_builder(&cmd_service_, nullptr);
		iox2_port_factory_publisher_builder_set_initial_max_slice_len(&pub_builder,
				weft::MESSAGE_BYTES);
		if (iox2_port_factory_publisher_builder_create(pub_builder, nullptr, &pub_) != IOX2_OK) {
			std::fprintf(stderr, "cineform-tui: no command publisher\n");
			return false;
		}

		// The reply subscriber is created BEFORE the command is sent. Opening it afterwards
		// races the interactor: a job that fails immediately can be answered before this end
		// is listening, and the reply is then never seen at all.
		if (iox2_port_factory_subscriber_builder_create(
					iox2_port_factory_pub_sub_subscriber_builder(&reply_service_, nullptr),
					nullptr, &sub_) != IOX2_OK) {
			std::fprintf(stderr, "cineform-tui: no reply subscriber\n");
			return false;
		}
		return true;
	}

	// Publishes `body` under `request_id`. Returns false if the bus refused the loan, which
	// is a full buffer rather than a lost interactor.
	bool send(uint64_t request_id, const unsigned char *body, size_t len) {
		const size_t total = weft::HEADER_BYTES + len;
		iox2_sample_mut_h sample = nullptr;
		if (iox2_publisher_loan_slice_uninit(&pub_, nullptr, &sample, total) != IOX2_OK) {
			return false;
		}
		void *payload = nullptr;
		size_t n = 0;
		iox2_sample_mut_payload_mut(&sample, &payload, &n);
		weft::command_write_header((unsigned char *)payload, request_id);
		std::memcpy((unsigned char *)payload + weft::HEADER_BYTES, body, len);
		return iox2_sample_mut_send(sample, nullptr) == IOX2_OK;
	}

	// Non-blocking. Returns true only when a reply carrying THIS request id has arrived.
	// Replies for other ids are consumed and discarded rather than left to block the queue.
	bool poll_reply(uint64_t request_id, std::vector<unsigned char> *out) {
		for (;;) {
			iox2_sample_h sample = nullptr;
			if (iox2_subscriber_receive(&sub_, nullptr, &sample) != IOX2_OK) {
				return false;
			}
			if (!sample) {
				return false;
			}
			const void *payload = nullptr;
			size_t n = 0;
			iox2_sample_payload(&sample, &payload, &n);
			if (n < weft::HEADER_BYTES) {
				iox2_sample_drop(sample);
				continue; // shorter than the envelope; malformed, not ours
			}
			const uint64_t id = weft::command_read_header((const unsigned char *)payload);
			if (id == request_id) {
				out->assign((const unsigned char *)payload + weft::HEADER_BYTES,
						(const unsigned char *)payload + n);
				iox2_sample_drop(sample);
				return true;
			}
			iox2_sample_drop(sample);
		}
	}

	void wait(uint32_t ns) { (void)iox2_node_wait(&node_, 0, ns); }

	void close() {
		if (sub_) { iox2_subscriber_drop(sub_); sub_ = nullptr; }
		if (pub_) { iox2_publisher_drop(pub_); pub_ = nullptr; }
		if (reply_service_) { iox2_port_factory_pub_sub_drop(reply_service_); reply_service_ = nullptr; }
		if (cmd_service_) { iox2_port_factory_pub_sub_drop(cmd_service_); cmd_service_ = nullptr; }
		if (node_) { iox2_node_drop(node_); node_ = nullptr; }
	}

	~CommandClient() { close(); }

private:
	iox2_port_factory_pub_sub_h open_service(const char *name) {
		iox2_service_name_h svc_name = nullptr;
		if (iox2_service_name_new(nullptr, name, std::strlen(name), &svc_name) != IOX2_OK) {
			return nullptr;
		}
		auto builder = iox2_service_builder_pub_sub(
				iox2_node_service_builder(&node_, nullptr, iox2_cast_service_name_ptr(svc_name)));
		if (iox2_service_builder_pub_sub_set_payload_type_details(&builder,
					iox2_type_variant_e_DYNAMIC, weft::PAYLOAD_TYPE, std::strlen(weft::PAYLOAD_TYPE),
					1, 1) != IOX2_OK) {
			iox2_service_name_drop(svc_name);
			return nullptr;
		}
		iox2_port_factory_pub_sub_h service = nullptr;
		const int rc = iox2_service_builder_pub_sub_open_or_create(builder, nullptr, &service);
		iox2_service_name_drop(svc_name);
		return rc == IOX2_OK ? service : nullptr;
	}

	iox2_node_h node_ = nullptr;
	iox2_port_factory_pub_sub_h cmd_service_ = nullptr;
	iox2_port_factory_pub_sub_h reply_service_ = nullptr;
	iox2_publisher_h pub_ = nullptr;
	iox2_subscriber_h sub_ = nullptr;
};

} // namespace cineform

#endif
