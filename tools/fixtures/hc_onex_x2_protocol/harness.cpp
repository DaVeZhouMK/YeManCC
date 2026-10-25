#include "onex_x2_protocol.h"
#include "oracle.generated.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace p = ymcc::onex_x2;
static std::uint64_t checks = 0;
static const char* section = "header-contract";
static void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
static bool expectedMatch(const oracle::Model& m, unsigned vid, unsigned pid, unsigned page, unsigned usage) {
    return vid == m.vid && std::find(m.pids.begin(), m.pids.end(), pid) != m.pids.end() &&
           page == m.page && usage == m.usage;
}
static const oracle::Model* expectedModel(p::Model model) {
    for (const auto& m : oracle::models) if (m.tag == static_cast<unsigned>(model)) return &m;
    return nullptr;
}
static void selectors() {
    section = "selectors";
    for (const auto& m : oracle::models) {
        const auto model = static_cast<p::Model>(m.tag);
        for (const auto* alias : {m.cls, m.canonicalClass}) {
            const std::string name = alias;
            require(p::modelForClass(name) == model, "exact short/canonical class");
            for (std::size_t i = 0; i < name.size(); ++i) {
                const auto prefix = std::string_view(name.data(), i);
                auto expected = p::Model::Unsupported;
                for (const auto& other : oracle::models)
                    if (prefix == other.cls || prefix == other.canonicalClass) expected = static_cast<p::Model>(other.tag);
                require(p::modelForClass(prefix) == expected, "exact valid/invalid class prefix");
            }
            for (const auto& bad : {name + "X", " " + name, name + " ", name + "\n", "X" + name,
                                   std::string("HandheldCompanion.Devices.") + name,
                                   name + std::string("\0suffix", 7), std::string("\0",1) + name}) {
                // Adding the exact namespace to a short class is a valid alias;
                // every other extra prefix/suffix remains rejected.
                auto expected = p::Model::Unsupported;
                for (const auto& other : oracle::models) if (bad == other.canonicalClass) expected = static_cast<p::Model>(other.tag);
                require(p::modelForClass(bad) == expected, "precise prefix/suffix/namespace/null");
            }
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; });
            require(p::modelForClass(lower) == p::Model::Unsupported, "class lowercase");
        }
        const auto pid = static_cast<std::uint16_t>(m.pids.front());
        for (unsigned x = 0; x <= 65535; ++x) {
            const auto field = static_cast<std::uint16_t>(x);
            require(p::matchesCollection(model, field, pid, static_cast<std::uint16_t>(m.page), static_cast<std::uint16_t>(m.usage)) ==
                    expectedMatch(m, x, pid, m.page, m.usage), "exhaustive VID");
            require(p::matchesCollection(model, static_cast<std::uint16_t>(m.vid), field, static_cast<std::uint16_t>(m.page), static_cast<std::uint16_t>(m.usage)) ==
                    expectedMatch(m, m.vid, x, m.page, m.usage), "exhaustive PID");
            require(p::matchesCollection(model, static_cast<std::uint16_t>(m.vid), pid, field, static_cast<std::uint16_t>(m.usage)) ==
                    expectedMatch(m, m.vid, pid, x, m.usage), "exhaustive usage page");
            require(p::matchesCollection(model, static_cast<std::uint16_t>(m.vid), pid, static_cast<std::uint16_t>(m.page), field) ==
                    expectedMatch(m, m.vid, pid, m.page, x), "exhaustive usage");
        }
        for (auto validPid : m.pids) require(p::matchesCollection(model, static_cast<std::uint16_t>(m.vid),
            static_cast<std::uint16_t>(validPid), static_cast<std::uint16_t>(m.page), static_cast<std::uint16_t>(m.usage)), "every source PID");
    }
    for (auto bad : {"", "OneXPlayerX1", "OneXPlayerX1AMD", "OneXPlayerX1Mini", "OneXPlayerX1Pro", "OneXPlayerApex", "X2", "ONEXPLAYER X2", "HandheldCompanion.Devices.OneXPlayerX1", "HandheldCompanion.Devices.OneXPlayerX1Mini",
                     "HandheldCompanion.Device.OneXPlayerX2", "HandheldCompanion.Devices..OneXPlayerX2",
                     "HandheldCompanion.Devices.OneXPlayerX2.", "handheldCompanion.Devices.OneXPlayerX2",
                     "HandheldCompanion.Devices.OneXPlayerX2MiniPRO"})
        require(p::modelForClass(bad) == p::Model::Unsupported, "old/unresolved class");
    for (unsigned tag = 0; tag < 256; ++tag) {
        auto model = static_cast<p::Model>(tag);
        const bool known = expectedModel(model) != nullptr;
        require(p::supported(model) == known, "invalid model tag");
        if (!known) {
            require(!p::matchesCollection(model, 0x1a86, 0xfe00, 0xff00, 1), "invalid model selector");
            require(!p::initStep(model, 0), "invalid init model");
            std::array<std::uint8_t, 64> data{};
            data[0] = static_cast<std::uint8_t>(oracle::buttonCommand);
            data[oracle::markerIndex] = static_cast<std::uint8_t>(oracle::marker);
            data[data.size()-oracle::markerFromEnd] = static_cast<std::uint8_t>(oracle::marker);
            require(!p::decodeLogicalFrame(model, data.data(), data.size()), "invalid decoder model");
            for (unsigned id = 0; id < 256; ++id)
                require(p::mapButton(model, static_cast<std::uint8_t>(id)) == p::Button::None, "invalid model button");
        }
    }
}
static void roles() {
    section = "button-roles";
    for (const auto& m : oracle::models) {
        for (unsigned id = 0; id < 256; ++id)
            require(static_cast<unsigned>(p::mapButton(static_cast<p::Model>(m.tag), static_cast<std::uint8_t>(id))) == m.roles[id], "all 256 button roles");
    }
    for (unsigned tag = 0; tag < 256; ++tag) {
        const bool expected = std::find(oracle::clickRoles.begin(), oracle::clickRoles.end(), tag) != oracle::clickRoles.end();
        require(p::clickOnly(static_cast<p::Button>(tag)) == expected, "click/held/invalid semantics");
    }
    require(p::clickPulseMs == oracle::clickMs, "source click duration");
}
static void initialization() {
    section = "initialization";
    for (const auto& m : oracle::models) {
        for (std::size_t index = 0; index < 256; ++index) {
            const auto step = p::initStep(static_cast<p::Model>(m.tag), index);
            require(bool(step) == (index < m.init.size()), "initialization step range");
            if (!step) continue;
            const auto& want = m.init[index];
            require(step->command == want.command, "initialization command");
            require(step->payloadSize == want.payload.size(), "initialization payload length");
            require(step->delayAfterMs == want.delay, "initialization delay");
            for (std::size_t i = 0; i < step->payload.size(); ++i)
                require(step->payload[i] == (i < want.payload.size() ? want.payload[i] : 0), "exact inherited remap/intercept byte");
        }
        require(!p::initStep(static_cast<p::Model>(m.tag), std::numeric_limits<std::size_t>::max()), "huge initialization index");
    }
}
static std::array<std::uint8_t,65> referenceEncoding(std::uint8_t command, const std::uint8_t* payload, std::size_t size, std::size_t length) {
    // Independent source-derived construction; no production encoder/helpers.
    std::vector<std::uint8_t> frame(oracle::frameLength, 0);
    frame[0] = command;
    frame[oracle::markerIndex] = static_cast<std::uint8_t>(oracle::marker);
    frame[oracle::indicatorIndex] = static_cast<std::uint8_t>(oracle::indicator);
    if (size) std::copy_n(payload, size, frame.begin() + oracle::payloadIndex);
    frame[frame.size()-oracle::markerTail] = static_cast<std::uint8_t>(oracle::marker);
    frame[frame.size()-oracle::commandTail] = command;
    if (length != frame.size()) frame.insert(frame.begin(), static_cast<std::uint8_t>(oracle::reportId));
    std::array<std::uint8_t,65> result{};
    std::copy(frame.begin(), frame.end(), result.begin());
    return result;
}
static void encoding() {
    section = "encoding";
    std::array<std::uint8_t,66> payload{};
    for (std::size_t i=0; i<payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(i*37+11);
    for (std::size_t length=0; length<=256; ++length) {
        for (std::size_t size=0; size<=66; ++size) {
            const bool valid = (length==oracle::frameLength || length==oracle::frameLength+1) && size<=oracle::frameLength-oracle::payloadOverhead;
            auto report = p::encodeReport(0xb4, payload.data(), size, length);
            require(bool(report)==valid, "report length/payload boundary");
            if (report) {
                require(report->size==length, "report size");
                require(report->bytes==referenceEncoding(0xb4,payload.data(),size,length), "all frame/padding bytes");
            }
        }
    }
    for (std::size_t length : {oracle::frameLength,oracle::frameLength+1}) {
        for (unsigned command=0; command<256; ++command) {
            for (std::size_t size=0; size<=oracle::frameLength-oracle::payloadOverhead; ++size) {
                auto report=p::encodeReport(static_cast<std::uint8_t>(command),payload.data(),size,length);
                require(bool(report), "all command/payload combinations");
                require(report->size==length && report->bytes==referenceEncoding(static_cast<std::uint8_t>(command),payload.data(),size,length), "independent 64/65 frame oracle");
            }
        }
        require(bool(p::encodeReport(0xb4,nullptr,0,length)), "null empty payload");
        require(!p::encodeReport(0xb4,nullptr,1,length), "null nonempty payload");
        require(!p::encodeReport(0xb4,payload.data(),std::numeric_limits<std::size_t>::max(),length), "huge payload size");
        p::InitStep invalid;
        invalid.payloadSize=60;
        require(!p::encodeReport(invalid,length), "oversized InitStep");
        invalid.payloadSize=std::numeric_limits<std::size_t>::max();
        require(!p::encodeReport(invalid,length), "huge InitStep");
        for (const auto& m : oracle::models) for (std::size_t i=0;i<m.init.size();++i) {
            const auto& want=m.init[i];
            const auto step=p::initStep(static_cast<p::Model>(m.tag),i);
            const auto report=p::encodeReport(*step,length);
            require(report && report->bytes==referenceEncoding(static_cast<std::uint8_t>(want.command),want.payload.data(),want.payload.size(),length), "every init encoded 64/65");
        }
    }
    require(!p::encodeReport(0xb4,payload.data(),0,std::numeric_limits<std::size_t>::max()), "huge report length");
}
static void compareDecode(const oracle::Model& m, const std::vector<std::uint8_t>& data) {
    // Re-express the pinned C# read loop directly using source-extracted fields.
    bool accepted = data.size() >= oracle::minimumLogicalLength;
    if (accepted) accepted = data[oracle::markerIndex] == oracle::marker && data[data.size()-oracle::markerFromEnd] == oracle::marker;
    if (accepted) accepted = data[oracle::commandIndex] == oracle::buttonCommand || data[oracle::commandIndex] == oracle::statusCommand;
    const auto actual = p::decodeLogicalFrame(static_cast<p::Model>(m.tag),data.data(),data.size());
    require(bool(actual)==accepted,"logical frame acceptance");
    if (!accepted) return;
    const bool button=data[oracle::commandIndex]==oracle::buttonCommand;
    require(actual->kind==(button ? p::FrameKind::Button : p::FrameKind::Status),"logical command kind");
    if (button) {
        const auto id=data[oracle::buttonIndex];
        require(actual->buttonId==id,"logical raw button ID (no report-ID offset guess)");
        require(static_cast<unsigned>(actual->button)==m.roles[id],"logical mapped button");
        require(actual->pressed==(data[oracle::pressedIndex]==oracle::pressedValue),"logical press byte");
        require(actual->status==0,"button defaults");
    } else {
        require(actual->status==data[oracle::statusIndex],"logical status");
        require(actual->buttonId==0 && actual->button==p::Button::None && !actual->pressed,"status no fabricated input");
    }
}
static std::vector<std::uint8_t> logical(std::size_t size,unsigned command,unsigned id,unsigned value) {
    std::vector<std::uint8_t> data(size,0);
    if (size>=oracle::minimumLogicalLength) {
        data[oracle::commandIndex]=static_cast<std::uint8_t>(command);
        data[oracle::buttonIndex]=static_cast<std::uint8_t>(id);
        data[oracle::pressedIndex]=static_cast<std::uint8_t>(value);
        data[oracle::markerIndex]=static_cast<std::uint8_t>(oracle::marker);
        data[size-oracle::markerFromEnd]=static_cast<std::uint8_t>(oracle::marker);
    }
    return data;
}
static void decoding() {
    section="logical-decoding";
    std::uint32_t seed=0x5eed1234;
    auto random=[&]() { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; };
    for (const auto& m : oracle::models) {
        const auto model=static_cast<p::Model>(m.tag);
        for (std::size_t size : {std::size_t(0),std::size_t(14),std::size_t(64),std::numeric_limits<std::size_t>::max()})
            require(!p::decodeLogicalFrame(model,nullptr,size),"null data guard");
        for (std::size_t size=0;size<=128;++size) {
            for (auto command : {oracle::buttonCommand,oracle::statusCommand,std::size_t(0),std::size_t(0xff)}) {
                auto data=logical(size,static_cast<unsigned>(command),0x22,1);
                compareDecode(m,data);
            }
        }
        for (unsigned id=0;id<256;++id) for (unsigned value : {0u,1u,2u,255u})
            compareDecode(m,logical(64,static_cast<unsigned>(oracle::buttonCommand),id,value));
        for (unsigned value=0;value<256;++value) {
            compareDecode(m,logical(64,static_cast<unsigned>(oracle::buttonCommand),0x21,value));
            auto status=logical(64,static_cast<unsigned>(oracle::statusCommand),0,0);
            status[oracle::statusIndex]=static_cast<std::uint8_t>(value);
            compareDecode(m,status);
            compareDecode(m,logical(64,value,0x24,1));
            auto head=logical(64,static_cast<unsigned>(oracle::buttonCommand),0x22,1);
            head[oracle::markerIndex]=static_cast<std::uint8_t>(value);
            compareDecode(m,head);
            auto tail=logical(64,static_cast<unsigned>(oracle::buttonCommand),0x22,1);
            tail[tail.size()-oracle::markerFromEnd]=static_cast<std::uint8_t>(value);
            compareDecode(m,tail);
        }
        auto raw65=logical(64,static_cast<unsigned>(oracle::buttonCommand),0x22,1);
        raw65.insert(raw65.begin(),0);
        compareDecode(m,raw65);
        require(!p::decodeLogicalFrame(model,raw65.data(),raw65.size()),"raw zero-report-ID is never auto-stripped");
        // The final byte and frame indicator are not input checks in pinned HC.
        for (unsigned last=0;last<256;++last) {
            auto data=logical(64,static_cast<unsigned>(oracle::buttonCommand),0x23,1);
            data.back()=static_cast<std::uint8_t>(last);
            data[2]=static_cast<std::uint8_t>(last);
            compareDecode(m,data);
        }
        for (unsigned round=0;round<20000;++round) {
            std::vector<std::uint8_t> data(random()%130);
            for (auto& byte : data) byte=static_cast<std::uint8_t>(random());
            if (round%2==0 && data.size()>=oracle::minimumLogicalLength) {
                data[oracle::commandIndex]=static_cast<std::uint8_t>(round%4==0 ? oracle::buttonCommand : oracle::statusCommand);
                data[oracle::markerIndex]=static_cast<std::uint8_t>(oracle::marker);
                data[data.size()-oracle::markerFromEnd]=static_cast<std::uint8_t>(oracle::marker);
            }
            compareDecode(m,data);
        }
    }
}
int main() {
    static_assert(p::modelForClass("OneXPlayerX2")==p::Model::X2,"constexpr class selector");
    static_assert(p::modelForClass("HandheldCompanion.Devices.OneXPlayerX2")==p::Model::X2,"constexpr canonical class selector");
    static_assert(p::initStep(p::Model::X2MiniPro,3)->payloadSize==3,"constexpr init metadata");
    try {
        selectors();roles();initialization();encoding();decoding();
        std::cout << "{\"status\":\"pass\",\"checks\":" << checks
                  << ",\"models\":3,\"buttonIdsPerModel\":256,\"selectorFieldsExhaustive16bit\":4,\"fuzzFrames\":60000}\n";
        return 0;
    } catch(const std::exception& e) {
        std::cout << "{\"status\":\"fail\",\"checks\":" << checks << ",\"section\":\"" << section
                  << "\",\"error\":\"" << e.what() << "\"}\n";
        return 1;
    }
}