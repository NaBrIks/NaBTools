// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleg Kozhevnikov
#include "stream.hpp"
#include "journal.hpp"
#include <vector>
#include <fstream>
#include <iostream>
using namespace NaBTools::runtime;
struct Capture {
	bool accept{true}; std::vector<std::vector<uint8_t> > frames;
	static bool send(void *context, Bytes bytes)
	{
		auto &c = *static_cast<Capture *>(context); if(!c.accept)
			return false;

		c.frames.emplace_back(bytes.begin(), bytes.end()); return true;
	}

};
int main(int argc, char **argv)
{
	Capture captured; research::EventStream stream; stream.begin(123, &captured, Capture::send);
	research::MessageJournal journal;
	journal.on_entry(&stream, [](void *context, const research::MessageJournal::Entry &entry){
		static_cast<research::EventStream *>(context)->emit("message", [&](CborWriter &w){
			research::MessageJournal::write_entry(w, entry);
		});
	});
	std::array<uint8_t, 2> payload{0x18, 0x42};
	if(journal.push({{2, 3}, 100, 5, "arbitrary.v1"}, payload) != Delivery::accepted)return 1;

	payload.fill(0); if(stream.sequence() != 1 || stream.dropped() || captured.frames.size() != 1)return 2;

	const auto owned = captured.frames[0];
	captured.accept = false; stream.emit("control", [](CborWriter &w){
		w.null();
	});
	if(stream.sequence() != 2 || stream.dropped() != 1)return 3;

	captured.accept = true; std::array<uint8_t, 5000> large{};
	stream.emit("oversize", [&](CborWriter &w){
		w.bytes(large);
	});
	if(stream.sequence() != 3 || stream.dropped() != 2)return 4;

	stream.emit("control", [](CborWriter &w){
		w.map(1); w.text("result"); w.text("ok");
	});
	if(stream.sequence() != 4 || captured.frames.size() != 2 || captured.frames[0] != owned)return 5;

	if(argc > 1)
	{
		std::ofstream file(argv[1], std::ios::binary);
		file.write(reinterpret_cast<const char *>(owned.data()), owned.size());
	}

	std::cout << "PASS owned event stream, journal callback, bounded frames and observable gaps\n";
}
