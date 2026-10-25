// JSON-line in-memory adapter for the unchanged production mailbox. No Windows,
// hardware, settings, process-control, or device API is linked into this helper.
#include <iostream>
#include <string>
#include "deps/json/json.hpp"
#include "native/shortcut_action_protocol.h"
using json = nlohmann::json;
int main() {
    ymcc::ShortcutActionMailbox mailbox("memory-input");
    uint64_t generation = 1, revision = 1, epoch = 1;
    bool ready = true;
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            const json in = json::parse(line);
            const std::string op = in.at("op");
            json out;
            if (op == "context") {
                generation = in.value("generation", generation);
                revision = in.value("revision", revision);
                epoch = in.value("epoch", epoch);
                ready = in.value("ready", ready);
                out = {{"ok", true}};
            } else if (op == "issue") {
                const auto capturedGeneration = generation, capturedRevision = revision, capturedEpoch = epoch;
                auto current = [&, capturedGeneration, capturedRevision, capturedEpoch] {
                    return ready && generation == capturedGeneration && revision == capturedRevision && epoch == capturedEpoch;
                };
                out = mailbox.issue(in.at("actionId"), in.at("params"), generation, revision,
                    in.at("tick"), in.at("unixMs"), current);
            } else if (op == "claim") {
                out = mailbox.claim(in.at("id"), in.at("tick"));
            } else if (op == "complete") {
                out = mailbox.complete(in.at("id"), in.at("outcome"), in.at("tick"), in.at("unixMs"));
            } else if (op == "size") {
                out = {{"size", mailbox.size()}};
            } else { out = {{"adapterError", "UNKNOWN_OPERATION"}}; }
            std::cout << out.dump() << std::endl;
        } catch (...) { std::cout << "{\"adapterError\":\"INVALID_ADAPTER_MESSAGE\"}" << std::endl; }
    }
}
