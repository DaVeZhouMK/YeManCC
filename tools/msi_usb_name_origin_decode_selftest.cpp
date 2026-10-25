#include "msi_usb_name_origin_decode.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

using ymcc::msi::usbName::json;
namespace decoder = ymcc::msi::usbName;
using Bytes = std::vector<std::uint8_t>;
using Decode = std::function<json(const Bytes&)>;
using Check = std::function<bool(const json&)>;

static_assert(std::is_same_v<decltype(&decoder::deviceDescriptor), json (*)(const Bytes&)>);
static_assert(std::is_same_v<decltype(&decoder::configurationDescriptor), json (*)(const Bytes&)>);
static_assert(std::is_same_v<decltype(&decoder::stringDescriptor),
                            json (*)(const Bytes&, std::uint8_t)>);

struct Tests {
    json report = {{"testKind", "standalone-pure-fake-input"},
                   {"platformApisUsed", false}, {"liveHardwareAccess", false},
                   {"cases", json::array()}};
    std::size_t failures = 0;

    void run(const std::string& name, const Bytes& input, const Decode& decode,
             bool expectedValid, const std::string& expectedError = "",
             const Check& check = {}) {
        const Bytes before = input;
        json entry = {{"case", name}, {"actualBytes", input.size()},
                      {"expectedValid", expectedValid}, {"expectedError", expectedError}};
        bool passed = false;
        try {
            const auto result = decode(input);
            const bool evidence = result.at("rawBytes").is_array() &&
                result.at("rawBytes") == json(input) &&
                result.at("actualBytes") == input.size() && input == before &&
                result.contains("reportedLength") == !input.empty() &&
                result.contains("reportedType") == (input.size() >= 2) &&
                (input.empty() || result.at("reportedLength") == input[0]) &&
                (input.size() < 2 || result.at("reportedType") == input[1]);
            const bool status = result.at("valid").is_boolean() &&
                result.at("valid") == expectedValid && result.at("error").is_string() &&
                result.at("error") == expectedError;
            const bool fields = !check || check(result);
            passed = evidence && status && fields;
            entry["evidencePassed"] = evidence;
            entry["statusPassed"] = status;
            entry["fieldChecksPassed"] = fields;
            entry["valid"] = result.at("valid");
            entry["error"] = result.at("error");
            if (!passed) entry["receipt"] = result;
        } catch (const std::exception& error) {
            entry["exception"] = error.what();
        }
        entry["passed"] = passed;
        if (!passed) {
            ++failures;
            std::cerr << "FAIL " << name << '\n';
        }
        report["cases"].push_back(std::move(entry));
    }
};

Bytes device() {
    return {18, 1, 0, 2, 0, 0, 0, 64, 0x34, 0x12, 0x78, 0x56,
            0, 1, 2, 3, 4, 5};
}

Bytes configuration(const std::vector<Bytes>& descriptors = {}) {
    Bytes bytes = {9, 2, 9, 0, 2, 7, 6, 0x80, 50};
    for (const auto& descriptor : descriptors)
        bytes.insert(bytes.end(), descriptor.begin(), descriptor.end());
    const auto total = bytes.size();
    bytes[2] = static_cast<std::uint8_t>(total & 0xff);
    bytes[3] = static_cast<std::uint8_t>((total >> 8) & 0xff);
    return bytes;
}

Bytes stringBytes(const std::vector<std::uint16_t>& units) {
    if (units.size() > 126) throw std::runtime_error("test string exceeds bLength");
    Bytes bytes = {static_cast<std::uint8_t>(2 + 2 * units.size()), 3};
    for (const auto unit : units) {
        bytes.push_back(static_cast<std::uint8_t>(unit & 0xff));
        bytes.push_back(static_cast<std::uint8_t>(unit >> 8));
    }
    return bytes;
}

bool noRenderedName(const json& result) {
    return !result.contains("product") && !result.contains("productName") &&
        !result.contains("text") && !result.contains("utf8");
}

bool languageOnly(const json& result) {
    return noRenderedName(result) && !result.contains("utf16") &&
        !result.contains("utf16WellFormed");
}

bool noActiveConfiguration(const json& result) {
    return !result.contains("activeConfiguration") && !result.contains("active") &&
        !result.contains("isActive");
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: msi_usb_name_origin_decode_selftest.exe report.json\n";
        return 2;
    }
    Tests tests;
    const Decode decodeDevice = decoder::deviceDescriptor;
    const Decode decodeConfig = decoder::configurationDescriptor;
    const Decode decodeString = [](const Bytes& b) { return decoder::stringDescriptor(b, 3); };
    const Decode decodeLanguages = [](const Bytes& b) { return decoder::stringDescriptor(b, 0); };

    const auto deviceBytes = device();
    tests.run("device-fields-little-endian", deviceBytes, decodeDevice, true, "",
        [](const json& r) {
            return r.at("vendorId") == 0x1234 && r.at("productId") == 0x5678 &&
                r.at("iManufacturer") == 2 && r.at("iProduct") == 3 &&
                r.at("iSerialNumber") == 4 && r.at("numConfigurations") == 5 &&
                r.at("trailingBytes") == json::array();
        });
    for (std::size_t size = 0; size < deviceBytes.size(); ++size) {
        const Bytes prefix(deviceBytes.begin(), deviceBytes.begin() + size);
        tests.run("device-truncated-" + std::to_string(size), prefix, decodeDevice, false,
                  size < 2 ? "device-header-truncated" : "device-truncated");
    }
    for (unsigned length = 0; length <= 255; ++length) {
        auto bytes = deviceBytes;
        bytes[0] = static_cast<std::uint8_t>(length);
        tests.run("device-bLength-" + std::to_string(length), bytes, decodeDevice,
                  length == 18, length == 18 ? "" : "device-length-invalid");
    }
    for (unsigned type = 0; type <= 255; ++type) {
        auto bytes = deviceBytes;
        bytes[1] = static_cast<std::uint8_t>(type);
        tests.run("device-type-" + std::to_string(type), bytes, decodeDevice,
                  type == 1, type == 1 ? "" : "device-type-invalid");
    }
    auto bytes = deviceBytes;
    bytes.insert(bytes.end(), {0, 0xff, 18, 1});
    tests.run("device-trailing-preserved", bytes, decodeDevice, true, "",
        [](const json& r) { return r.at("trailingBytes") == json(Bytes{0, 0xff, 18, 1}); });
    bytes = deviceBytes;
    std::fill(bytes.begin() + 8, bytes.end(), std::uint8_t{0});
    tests.run("device-zero-field-evidence-not-device-policy", bytes, decodeDevice, true, "",
        [](const json& r) { return r.at("vendorId") == 0 && r.at("iProduct") == 0; });

    const Bytes iad = {8, 11, 2, 3, 0xff, 0x42, 1, 9};
    const Bytes interface0 = {9, 4, 2, 0, 1, 0xff, 0, 0, 10};
    const Bytes interface1 = {9, 4, 2, 1, 1, 0xff, 0, 0, 11};
    const auto configBytes = configuration({iad, interface0, interface1,
                                          {3, 0xfe, 0xa5}, {7, 5, 0x81, 3, 64, 0, 1}});
    tests.run("configuration-IAD-interface-alternates-unknown-endpoint", configBytes,
        decodeConfig, true, "", [](const json& r) {
            return r.at("configurationValue") == 7 && r.at("totalLength") == 45 &&
                r.at("numInterfaces") == 2 && r.at("interfaces") == json::array({
                    {{"number", 2}, {"alternateSetting", 0}, {"iInterface", 10}},
                    {{"number", 2}, {"alternateSetting", 1}, {"iInterface", 11}}}) &&
                r.at("iads") == json::array({
                    {{"firstInterface", 2}, {"interfaceCount", 3}, {"iFunction", 9}}}) &&
                !r.contains("errorOffset") && noActiveConfiguration(r);
        });
    tests.run("configuration-header-only-does-not-invent-interfaces", configuration(),
        decodeConfig, true, "", [](const json& r) {
            return r.at("interfaces").empty() && r.at("iads").empty() &&
                r.at("numInterfaces") == 2 && noActiveConfiguration(r);
        });
    for (std::size_t size = 0; size < configBytes.size(); ++size) {
        const Bytes prefix(configBytes.begin(), configBytes.begin() + size);
        tests.run("configuration-transfer-truncated-" + std::to_string(size), prefix,
            decodeConfig, false, size < 9 ? "configuration-header-truncated" :
                                          "configuration-truncated");
    }
    for (unsigned length = 0; length <= 255; ++length) {
        bytes = configBytes;
        bytes[0] = static_cast<std::uint8_t>(length);
        tests.run("configuration-bLength-" + std::to_string(length), bytes, decodeConfig,
            length == 9, length == 9 ? "" : "configuration-length-invalid");
    }
    for (unsigned type = 0; type <= 255; ++type) {
        bytes = configBytes;
        bytes[1] = static_cast<std::uint8_t>(type);
        tests.run("configuration-type-" + std::to_string(type), bytes, decodeConfig,
            type == 2, type == 2 ? "" : "configuration-type-invalid");
    }
    for (unsigned total : {0u, 1u, 8u, 16385u, 65535u}) {
        bytes = configuration();
        bytes[2] = static_cast<std::uint8_t>(total & 0xff);
        bytes[3] = static_cast<std::uint8_t>(total >> 8);
        tests.run("configuration-total-boundary-" + std::to_string(total), bytes,
            decodeConfig, false, total < 9 ? "configuration-total-length-invalid" :
                                           "configuration-total-length-limit");
    }
    bytes = configBytes;
    bytes.insert(bytes.end(), {0, 4, 0xff, 1, 3});
    tests.run("configuration-trailing-malformed-descriptors-are-evidence", bytes,
        decodeConfig, true, "", [](const json& r) {
            return r.at("trailingBytes") == json(Bytes{0, 4, 0xff, 1, 3}) &&
                r.at("interfaces").size() == 2 && r.at("iads").size() == 1;
        });
    for (unsigned length : {0u, 1u}) {
        tests.run("configuration-zero-or-one-length-" + std::to_string(length),
            configuration({{static_cast<std::uint8_t>(length), 0xfe}}), decodeConfig,
            false, "descriptor-length-invalid",
            [](const json& r) { return r.at("errorOffset") == 9; });
    }
    tests.run("configuration-descriptor-one-byte-header", configuration({{2}}),
              decodeConfig, false, "descriptor-header-truncated");
    tests.run("configuration-descriptor-declared-truncated", configuration({{255, 0xfe}}),
              decodeConfig, false, "descriptor-truncated");
    tests.run("configuration-interface-descriptor-truncated", configuration({{9, 4, 1, 2, 3}}),
              decodeConfig, false, "descriptor-truncated");
    tests.run("configuration-IAD-descriptor-truncated", configuration({{8, 11, 1, 2, 3}}),
              decodeConfig, false, "descriptor-truncated");
    Bytes longestUnknown(255, 0xa5);
    longestUnknown[0] = 255;
    longestUnknown[1] = 0xfe;
    tests.run("configuration-unknown-max-bLength-255", configuration({longestUnknown}),
              decodeConfig, true);
    for (unsigned length = 2; length < 9; ++length) {
        Bytes descriptor(length, 0);
        descriptor[0] = static_cast<std::uint8_t>(length);
        descriptor[1] = 4;
        tests.run("configuration-short-interface-" + std::to_string(length),
            configuration({descriptor}), decodeConfig, false, "interface-length-invalid");
    }
    for (unsigned length = 2; length < 8; ++length) {
        Bytes descriptor(length, 0);
        descriptor[0] = static_cast<std::uint8_t>(length);
        descriptor[1] = 11;
        tests.run("configuration-short-IAD-" + std::to_string(length),
            configuration({descriptor}), decodeConfig, false, "iad-length-invalid");
    }
    tests.run("configuration-invalid-after-valid-interface", configuration({interface0, {0, 11}}),
        decodeConfig, false, "descriptor-length-invalid", [](const json& r) {
            return r.at("interfaces").size() == 1 && r.at("iads").empty() &&
                r.at("errorOffset") == 18;
        });
    bytes = configBytes;
    bytes[2] = 15; // The IAD cannot borrow bytes outside wTotalLength.
    tests.run("configuration-bLength-cannot-cross-totalLength", bytes, decodeConfig, false,
        "descriptor-truncated", [](const json& r) {
            return r.at("trailingBytes").size() == 30 && r.at("iads").empty();
        });
    auto extendedIad = iad;
    extendedIad[0] = 10;
    extendedIad.insert(extendedIad.end(), {0xa1, 0xa2});
    auto extendedInterface = interface0;
    extendedInterface[0] = 11;
    extendedInterface.insert(extendedInterface.end(), {0xb1, 0xb2});
    tests.run("configuration-known-descriptors-with-extra-bytes", configuration({extendedIad, extendedInterface}),
        decodeConfig, true, "", [](const json& r) {
            return r.at("iads").at(0).at("iFunction") == 9 &&
                r.at("interfaces").at(0).at("iInterface") == 10;
        });
    bytes.assign(decoder::maxConfigurationBytes, 0);
    const auto configHeader = configuration();
    std::copy(configHeader.begin(), configHeader.end(), bytes.begin());
    bytes[2] = 0;
    bytes[3] = 0x40;
    bytes[9] = 3;
    bytes[10] = 0xfe;
    bytes[11] = 0xaa;
    for (std::size_t offset = 12; offset < bytes.size(); offset += 2) {
        bytes[offset] = 2;
        bytes[offset + 1] = 0xfe;
    }
    tests.run("configuration-exact-16384-byte-bound-many-descriptors", bytes, decodeConfig,
        true, "", [](const json& r) { return r.at("totalLength") == 16384; });
    bytes.pop_back();
    tests.run("configuration-16384-one-byte-short", bytes, decodeConfig, false,
              "configuration-truncated");

    tests.run("indexed-string-empty-valid", {2, 3}, decodeString, true, "",
        [](const json& r) {
            return r.at("utf16").empty() && r.at("utf16WellFormed") == true &&
                !r.contains("languageIds") && noRenderedName(r);
        });
    tests.run("index-zero-no-languages-invalid", {2, 3}, decodeLanguages, false,
              "language-list-empty", languageOnly);
    tests.run("index-zero-no-languages-despite-trailing-langid", {2, 3, 9, 4}, decodeLanguages,
        false, "language-list-empty", [](const json& r) {
            return languageOnly(r) && r.at("trailingBytes") == json(Bytes{9, 4});
        });
    const auto languageBytes = stringBytes({0x0609, 0x0409});
    tests.run("index-zero-0609-0409-are-LANGIDs-only", languageBytes, decodeLanguages,
        true, "", [](const json& r) {
            return languageOnly(r) && r.at("languageIds") == json::array({0x0609, 0x0409});
        });
    tests.run("indexed-U0609-exact-code-unit", stringBytes({0x0609}), decodeString,
        true, "", [](const json& r) {
            return r.at("utf16") == json::array({0x0609}) &&
                r.at("utf16WellFormed") == true && !r.contains("languageIds") && noRenderedName(r);
        });
    tests.run("index-zero-0409-never-product", stringBytes({0x0409}), decodeLanguages,
        true, "", [](const json& r) {
            return languageOnly(r) && r.at("languageIds") == json::array({0x0409});
        });
    tests.run("index-zero-LANGID-not-surrogate-validation", stringBytes({0xd800}),
        decodeLanguages, true, "", [](const json& r) {
            return languageOnly(r) && r.at("languageIds") == json::array({0xd800});
        });
    for (const auto& units : std::vector<std::vector<std::uint16_t>>{{0}, {0x0409, 0}, {0, 0x0609}}) {
        tests.run("index-zero-reject-zero-LANGID-" + std::to_string(tests.report["cases"].size()),
            stringBytes(units), decodeLanguages, false, "language-id-zero", languageOnly);
    }
    const std::vector<std::vector<std::uint16_t>> validUnits = {
        {0}, {0x0041, 0x0609, 0x4e2d}, {0xd83d, 0xde00}, {0xd800, 0xdc00},
        {0xdbff, 0xdfff}, {0x0041, 0xd83d, 0xde00, 0x4e2d, 0xd800, 0xdc00},
        {0xd7ff, 0xe000, 0xffff}};
    for (std::size_t i = 0; i < validUnits.size(); ++i) {
        const auto units = validUnits[i];
        tests.run("indexed-valid-BMP-or-surrogate-units-" + std::to_string(i), stringBytes(units),
            decodeString, true, "", [units](const json& r) {
                return r.at("utf16") == json(units) && r.at("utf16WellFormed") == true &&
                    noRenderedName(r) && !r.contains("languageIds");
            });
    }
    const std::vector<std::vector<std::uint16_t>> invalidUnits = {
        {0xd800}, {0xdc00}, {0xdfff}, {0xd800, 0x0041}, {0xd800, 0xd800},
        {0xdc00, 0xd800}, {0x0041, 0xd83d}, {0xd83d, 0xde00, 0xdc00}};
    for (std::size_t i = 0; i < invalidUnits.size(); ++i) {
        const auto units = invalidUnits[i];
        tests.run("indexed-invalid-surrogates-preserve-units-" + std::to_string(i),
            stringBytes(units), decodeString, false, "utf16-ill-formed", [units](const json& r) {
                return r.at("utf16") == json(units) && r.at("utf16WellFormed") == false &&
                    noRenderedName(r) && !r.contains("languageIds");
            });
    }
    const auto indexedBytes = stringBytes({0x0041, 0xd83d, 0xde00, 0x0609});
    for (std::size_t size = 0; size < indexedBytes.size(); ++size) {
        const Bytes prefix(indexedBytes.begin(), indexedBytes.begin() + size);
        tests.run("indexed-string-truncated-" + std::to_string(size), prefix, decodeString,
            false, size < 2 ? "string-header-truncated" : "string-truncated");
    }
    for (std::size_t size = 0; size < languageBytes.size(); ++size) {
        const Bytes prefix(languageBytes.begin(), languageBytes.begin() + size);
        tests.run("language-list-truncated-" + std::to_string(size), prefix, decodeLanguages,
            false, size < 2 ? "string-header-truncated" : "string-truncated", languageOnly);
    }
    for (unsigned length = 0; length <= 255; ++length) {
        Bytes string(256, 0x41);
        string[0] = static_cast<std::uint8_t>(length);
        string[1] = 3;
        const bool valid = length >= 2 && length % 2 == 0;
        tests.run("indexed-string-bLength-" + std::to_string(length), string, decodeString,
            valid, valid ? "" : "string-length-invalid");
        const bool validLanguages = valid && length >= 4;
        tests.run("index-zero-bLength-" + std::to_string(length), string, decodeLanguages,
            validLanguages, !valid ? "string-length-invalid" :
                            length == 2 ? "language-list-empty" : "", languageOnly);
    }
    for (unsigned type = 0; type <= 255; ++type) {
        bytes = indexedBytes;
        bytes[1] = static_cast<std::uint8_t>(type);
        tests.run("string-type-" + std::to_string(type), bytes, decodeString,
            type == 3, type == 3 ? "" : "string-type-invalid");
        auto languages = languageBytes;
        languages[1] = static_cast<std::uint8_t>(type);
        tests.run("index-zero-type-" + std::to_string(type), languages, decodeLanguages,
            type == 3, type == 3 ? "" : "string-type-invalid", languageOnly);
    }
    bytes = indexedBytes;
    bytes.insert(bytes.end(), {0, 0xd8, 0xff});
    tests.run("indexed-string-trailing-surrogate-and-odd-byte-preserved", bytes, decodeString,
        true, "", [](const json& r) {
            return r.at("trailingBytes") == json(Bytes{0, 0xd8, 0xff}) &&
                r.at("utf16") == json::array({0x0041, 0xd83d, 0xde00, 0x0609}) &&
                r.at("utf16WellFormed") == true;
        });
    bytes = languageBytes;
    bytes.insert(bytes.end(), {0, 0, 0xff});
    tests.run("index-zero-trailing-zero-LANGID-and-odd-byte-preserved", bytes, decodeLanguages,
        true, "", [](const json& r) {
            return languageOnly(r) && r.at("trailingBytes") == json(Bytes{0, 0, 0xff}) &&
                r.at("languageIds") == json::array({0x0609, 0x0409});
        });
    tests.run("string-odd-length-malformed-language-list", {5, 3, 9, 4, 6}, decodeLanguages,
              false, "string-length-invalid", languageOnly);
    tests.run("string-malformed-surrogate-trailing-retained", {4, 3, 0, 0xd8, 9, 4},
        decodeString, false, "utf16-ill-formed", [](const json& r) {
            return r.at("utf16") == json::array({0xd800}) &&
                r.at("trailingBytes") == json(Bytes{9, 4}) && noRenderedName(r);
        });
    const auto maximumString = stringBytes(std::vector<std::uint16_t>(126, 0x0609));
    tests.run("indexed-string-max-even-bLength-254", maximumString, decodeString, true, "",
        [](const json& r) { return r.at("utf16").size() == 126 && r.at("reportedLength") == 254; });
    tests.run("index-zero-max-126-LANGIDs", maximumString, decodeLanguages, true, "",
        [](const json& r) { return languageOnly(r) && r.at("languageIds").size() == 126; });
    for (unsigned index = 1; index <= 255; ++index) {
        const auto expectedIndex = static_cast<std::uint8_t>(index);
        tests.run("every-nonzero-index-uses-UTF16-" + std::to_string(index), stringBytes({0x0609}),
            [expectedIndex](const Bytes& b) { return decoder::stringDescriptor(b, expectedIndex); },
            true, "", [expectedIndex](const json& r) {
                return r.at("index") == expectedIndex && r.at("utf16") == json::array({0x0609}) &&
                    !r.contains("languageIds") && noRenderedName(r);
            });
    }

    // Cancellation belongs to the caller. Captured bytes remain deterministically
    // decodable before/after that local flag changes; no cancellation/OS hook is
    // passed into these exact public interfaces. This is not an adapter I/O test.
    struct CapturedInput { Bytes bytes; bool callerCancelled = false; };
    const std::vector<std::pair<Bytes, Decode>> capturedCases = {
        {deviceBytes, decodeDevice}, {configBytes, decodeConfig},
        {languageBytes, decodeLanguages}, {indexedBytes, decodeString},
        {{0, 3}, decodeString}, {configuration({{0, 4}}), decodeConfig},
        {stringBytes({0xd800}), decodeString}};
    for (std::size_t i = 0; i < capturedCases.size(); ++i) {
        CapturedInput capture{capturedCases[i].first};
        const auto decode = capturedCases[i].second;
        const auto beforeCancellation = decode(capture.bytes);
        capture.callerCancelled = true;
        tests.run("captured-input-caller-cancellation-independent-" + std::to_string(i),
            capture.bytes, decode, beforeCancellation.at("valid").get<bool>(),
            beforeCancellation.at("error").get<std::string>(), [&](const json& r) {
                return capture.callerCancelled && r == beforeCancellation;
            });
        // Interleaved failures and other descriptor types cannot alter a receipt.
        (void)decoder::deviceDescriptor({0});
        (void)decoder::configurationDescriptor(configuration({{1, 4}}));
        (void)decoder::stringDescriptor(stringBytes({0xdfff}), 255);
        tests.run("captured-input-replay-after-interleaved-errors-" + std::to_string(i),
            capture.bytes, decode, beforeCancellation.at("valid").get<bool>(),
            beforeCancellation.at("error").get<std::string>(), [&](const json& r) {
                return r == beforeCancellation;
            });
    }

    tests.report["caseCount"] = tests.report["cases"].size();
    tests.report["failureCount"] = tests.failures;
    tests.report["passed"] = tests.failures == 0;
    tests.report["configurationParseLimitBytes"] = decoder::maxConfigurationBytes;
    tests.report["indexedStringMaxUtf16Units"] = 126;
    tests.report["allSuppliedRawAndTrailingBytesPreserved"] = tests.failures == 0;
    tests.report["cancellationScope"] = "local caller flag and captured-byte replay only; no adapter I/O";
    std::ofstream output(argv[1], std::ios::binary);
    if (!output) {
        std::cerr << "Cannot open report path\n";
        return 2;
    }
    output << tests.report.dump(2) << '\n';
    output.close();
    if (!output) {
        std::cerr << "Cannot write report\n";
        return 2;
    }
    std::cout << "MSI_USB_NAME_ORIGIN_DECODE_SELFTEST cases=" << tests.report["caseCount"]
              << " failures=" << tests.failures << " passed=" << (tests.failures == 0 ? "true" : "false")
              << '\n';
    return tests.failures == 0 ? 0 : 1;
}
