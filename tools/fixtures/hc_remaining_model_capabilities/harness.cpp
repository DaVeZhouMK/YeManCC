// Test driver appended after extracted production definitions.
// It executes only pure identity/profile/catalog/matrix/power predicates and the
// exact oem.keys.get lambda against in-memory stubs. It never opens hardware.
namespace test {
static unsigned long long checks = 0;
static std::string section = "startup";
static json counts = json::object();
static json catalogQueries = json::array();
static json captureObservations = json::array();
static json steamDeckLegacyComparisons = json::array();
struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };
static void group(const char* name) { section = name; counts[section] = 0; }
static void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw Failure(section + ": " + message);
    counts[section] = counts[section].get<unsigned long long>() + 1;
}
static current::YmccFamily familyFor(const std::string& name) {
    if (name == "Unknown") return current::YmccFamily::Unknown;
    if (name == "Ayanero") return current::YmccFamily::Ayanero;
    if (name == "OneXPlayer") return current::YmccFamily::OneXPlayer;
    if (name == "ValveSteamMachine") return current::YmccFamily::ValveSteamMachine;
    throw Failure("unreviewed expected family " + name);
}
static std::string field(const json& row, const char* name) { return row.value(name, std::string{}); }
static current::MachineIdentity currentIdentity(const json& row) {
    current::MachineIdentity id;
    id.manufacturer = current::ymccDmiNormalize(row.value("manufacturer", std::string{}));
    id.product = current::ymccDmiNormalize(row.value("product", std::string{}));
    id.systemProduct = current::ymccDmiNormalize(row.value("systemProduct", std::string{}));
    id.processor = current::ymccDmiNormalize(row.value("processor", std::string{}));
    id.systemVersion = current::ymccDmiNormalize(row.value("systemVersion", std::string{}));
    current::ymccResolveFamily(id);
    return id;
}
static before::MachineIdentity beforeIdentity(const json& row) {
    before::MachineIdentity id;
    id.manufacturer = before::ymccDmiNormalize(row.value("manufacturer", std::string{}));
    id.product = before::ymccDmiNormalize(row.value("product", std::string{}));
    id.systemProduct = before::ymccDmiNormalize(row.value("systemProduct", std::string{}));
    id.processor = before::ymccDmiNormalize(row.value("processor", std::string{}));
    id.systemVersion = before::ymccDmiNormalize(row.value("systemVersion", std::string{}));
    before::ymccResolveFamily(id);
    return id;
}
static uint64_t bitFor(const std::string& name) {
    uint64_t bit = 0;
    check(current::nativeKeyboardShortcutMaskBit(name, bit), "canonical key resolves: " + name);
    return bit;
}
static uint32_t vkMask(const json& key) {
    uint32_t mask = 0;
    for (const auto& value : key.at("pressVks")) mask |= current::oemBackHookVkBit(value.get<DWORD>());
    return mask;
}
static uint64_t keyBit(const json& key) { return std::stoull(key.at("canonicalBit").get<std::string>()); }
static uint64_t expectedMask(const json& keys, unsigned subset) {
    uint64_t result = 0;
    for (unsigned i = 0; i < keys.size(); ++i) if (subset & (1u << i)) result |= keyBit(keys[i]);
    return result;
}
static uint32_t expectedKeyboard(const json& keys, unsigned subset) {
    uint32_t result = 0;
    for (unsigned i = 0; i < keys.size(); ++i) if (subset & (1u << i)) result |= keys[i].at("kbMask").get<uint32_t>();
    return result;
}
static void evaluate(int profile, uint32_t kb, bool mouse, bool ready, uint64_t& mask, int& state, int& physical, uint32_t& base, uint32_t& prefix) {
    current::oemChordEvalRules(profile, kb, mouse, ready, &mask, &state, &physical, &base, &prefix);
}
static json queryCurrent(const json& row) {
    current::g_machineIdentity = currentIdentity(row);
    current::testLogs.clear();
    return current::extractedOemKeysGet(json::object());
}
static json queryBefore(const json& row) {
    before::g_machineIdentity = beforeIdentity(row);
    before::testLogs.clear();
    return before::extractedOemKeysGet(json::object());
}
static const json& modelFor(const json& expected, const std::string& name) {
    for (const auto& model : expected.at("models")) if (model.at("name").get<std::string>() == name) return model;
    throw Failure("missing model expectation " + name);
}
static void testIdentityProfilesCatalog(const json& expected) {
    group("identity-selector-catalog-profile");
    unsigned aliasCount = 0;
    for (const auto& model : expected.at("models")) {
        const std::string familyName = model.at("family");
        const auto family = familyFor(familyName);
        const int expectedProfile = expectedProfileId(model.at("profile"));
        const auto& catalogExpectation = model.at("catalog");
        for (const auto& alias : expected.at("identities")) {
            if (alias.at("deviceClass").get<std::string>() != model.at("name").get<std::string>()) continue;
            ++aliasCount;
            const auto id = currentIdentity(alias);
            check(id.resolved, model.at("name").get<std::string>() + " alias resolves");
            check(id.family == family, model.at("name").get<std::string>() + " exact HC family");
            check(id.deviceClass == model.at("fullName").get<std::string>(), model.at("name").get<std::string>() + " exact device class");
            check(current::oemChordProfileIdFor(id.family, id.deviceClass) == expectedProfile,
                  model.at("name").get<std::string>() + " selector profile");
            const auto catalog = current::oemCatalogForFamily(id.family, id.deviceClass);
            check(catalog.count == static_cast<int>(catalogExpectation.size()), model.at("name").get<std::string>() + " catalog count");
            for (int i = 0; i < catalog.count; ++i) {
                const auto& want = catalogExpectation.at(i); const auto& got = catalog.keys[i];
                check(got.keyId == want.at("id").get<std::string>(), "catalog key ID order");
                check(got.label == want.at("label").get<std::string>(), "catalog source label");
                check(got.backIndex == want.at("backIndex").get<int>(), "catalog rear ordinal");
                check((got.form == current::OemKeyForm::KeyboardVk ? "KeyboardVk" : got.form == current::OemKeyForm::HidButton ? "HidButton" : "other") == want.at("form").get<std::string>(), "catalog form");
                check(got.clickOnly == want.at("clickOnly").get<bool>(), "catalog press/release capability");
            }
            const auto& profile = current::oemChordProfileFor(expectedProfile);
            check(profile.count == static_cast<int>(model.at("keys").size()), model.at("name").get<std::string>() + " profile rule count");
            for (const auto& key : model.at("keys")) {
                uint64_t bit = 0; check(current::nativeKeyboardShortcutMaskBit(key.at("id"), bit), "canonical ID exists");
                check(bit == keyBit(key), "canonical bit independent expectation");
            }
        }
    }
    check(aliasCount == 63 && aliasCount == expected.at("identities").size(), "all 63 fixed aliases execute, not merely the first alias for each model");
    const auto next2 = currentIdentity(json{{"manufacturer", "AYANEO"}, {"product", "NEXT 2"}});
    const auto lower = currentIdentity(json{{"manufacturer", "ayadevice"}, {"product", "ayaneo next ii"}});
    const auto nul = currentIdentity(json{{"manufacturer", std::string("AYA\0", 4)}, {"product", std::string("AB07\0", 5)}});
    check(next2.deviceClass == "HandheldCompanion.Devices.AYANEONEXT2", "NEXT2 remains exact legacy identity");
    check(lower.deviceClass == "HandheldCompanion.Devices.AYANEONEXT2", "DMI case normalization remains");
    check(nul.deviceClass == "HandheldCompanion.Devices.AYANEO3", "DMI NUL normalization remains");    const auto wrongAyaneo = currentIdentity(json{{"manufacturer", "AYANEO"}, {"product", "AYANEO 30"}});
    const auto wrongAir = currentIdentity(json{{"manufacturer", "AYANEO"}, {"product", "AIR 20"}});
    const auto wrongFlip = currentIdentity(json{{"manufacturer", "AYANEO"}, {"product", "FLIP 110"}});
    const auto wrongOnex = currentIdentity(json{{"manufacturer", "ONE-NETBOOK"}, {"product", "ONEXPLAYER X20"}});
    check(wrongAyaneo.deviceClass != "HandheldCompanion.Devices.AYANEO3", "AYANEO identity uses exact product token");
    check(wrongAir.deviceClass != "HandheldCompanion.Devices.AYANEOAIR2", "AIR identity uses exact product token");
    check(wrongFlip.deviceClass != "HandheldCompanion.Devices.AYANEOFlip11", "Flip identity uses exact product token");
    check(wrongOnex.deviceClass != "HandheldCompanion.Devices.OneXPlayerX2", "OneX identity uses exact product token");
    const auto machine = currentIdentity(json{{"manufacturer", "VALVE"}, {"product", "Fremont"}});
    check(machine.family == current::YmccFamily::ValveSteamMachine, "VALVE/Fremont exact SteamMachine family");
    check(machine.deviceClass == "HandheldCompanion.Devices.SteamMachine", "SteamMachine class");
    check(current::oemChordProfileIdFor(machine.family, machine.deviceClass) == expectedProfileId("None"), "SteamMachine has no handheld profile");
    check(current::oemCatalogForFamily(machine.family, machine.deviceClass).count == 0, "SteamMachine has no OEM catalog");
    check(machine.family != current::YmccFamily::ValveSteamDeck, "SteamMachine family is not SteamDeck");
    check(current::oemChordProfileIdFor(current::YmccFamily::ValveSteamMachine, "HandheldCompanion.Devices.SteamDeck") == expectedProfileId("None"), "SteamMachine family cannot borrow a Deck profile");
}
static void testProfilesAndEvents(const json& expected) {
    group("profile-evaluator-press-release-combinations");
    for (const auto& model : expected.at("models")) {
        const std::string name = model.at("name");
        const int profileId = expectedProfileId(model.at("profile"));
        const auto& keys = model.at("keys");
        const auto& profile = current::oemChordProfileFor(profileId);
        check(profile.count == static_cast<int>(keys.size()), name + " rule count is source-derived");
        for (const auto& key : keys) {
            const uint32_t kb = vkMask(key);
            check(kb == key.at("kbMask").get<uint32_t>(), name + "/" + key.at("id").get<std::string>() + " independent VK mask");
            uint64_t mask; int state, physical; uint32_t base, prefix;
            evaluate(profileId, kb, false, true, mask, state, physical, base, prefix);
            check(mask == keyBit(key), name + " press emits canonical bit");
            const int backIndex = key.at("backIndex").get<int>();
            const int expectedState = backIndex == 1 ? 1 : backIndex == 2 ? 2 : 0;
            check(state == expectedState, name + " left/right output semantics");
            check(physical == expectedState, name + " physical rear observation");
            check((backIndex == 0 && base == 0 && prefix == 0) || (backIndex != 0 && base == kb), name + " diagnostic rear base");
            for (const auto& vk : key.at("pressVks")) {
                uint32_t released = kb & ~current::oemBackHookVkBit(vk.get<DWORD>());
                evaluate(profileId, released, false, true, mask, state, physical, base, prefix);
                check(mask == 0, name + " release clears chord");
                check(state == 0, name + " release clears rear state");
            }
        }
        if (keys.size() <= 10) {
            const unsigned combinations = 1u << keys.size();
            for (unsigned subset = 0; subset < combinations; ++subset) {
                const uint32_t kb = expectedKeyboard(keys, subset);
                uint64_t mask; int state, physical; uint32_t base, prefix;
                evaluate(profileId, kb, false, true, mask, state, physical, base, prefix);
                check(mask == expectedMask(keys, subset), name + " all chord combinations");
                int expectedState = 0;
                for (unsigned i = 0; i < keys.size(); ++i) if (subset & (1u << i)) {
                    const int back = keys[i].at("backIndex").get<int>(); if (back == 1) expectedState |= 1; if (back == 2) expectedState |= 2;
                }
                check(state == expectedState, name + " all combination left/right semantics");
            }
        }
        if (name == "AYANEO3") {
            uint64_t mask; int state, physical; uint32_t base, prefix;
            evaluate(profileId, current::oemBackHookVkBit(0x7B), false, true, mask, state, physical, base, prefix);
            check(mask == 0, "AYANEO3 does not reuse NEXT2 Win+D Small");
            evaluate(profileId, current::oemBackHookVkBit(0x87), false, true, mask, state, physical, base, prefix);
            check(mask == keyBit(keys.at(1)), "AYANEO3 Small is F24");
        }
    }
    const int mini = expectedProfileId("OneXX2MiniPro");
    uint64_t mask; int state, physical; uint32_t base, prefix;
    evaluate(mini, current::oemBackHookVkBit(0x7E), false, true, mask, state, physical, base, prefix);
    check(mask == keyBit(modelFor(expected, "OneXPlayerX2MiniPro").at("keys").at(2)), "Mini F15 maps M1/L4");
    check(state == 1 && physical == 1, "Mini M1 is normative left bit0");
    evaluate(mini, current::oemBackHookVkBit(0x7F), false, true, mask, state, physical, base, prefix);
    check(mask == keyBit(modelFor(expected, "OneXPlayerX2MiniPro").at("keys").at(3)), "Mini F16 maps M2/R4");
    check(state == 2 && physical == 2, "Mini M2 is normative right bit1");
}
static void testMatrices(const json& expected) {
    group("matrix-remap-independent-helper");
    for (const auto& model : expected.at("models")) {
        if (model.at("matrices").empty()) continue;
        json alias;
        for (const auto& candidate : expected.at("identities")) {
            if (candidate.at("deviceClass").get<std::string>() == model.at("name").get<std::string>()) { alias = candidate; break; }
        }
        const auto id = currentIdentity(alias);
        const std::string name = model.at("name").get<std::string>();
        check(id.resolved, name + " matrix identity resolves");
        for (const auto& matrix : model.at("matrices")) {
            const std::string sensor = matrix.at("sensor").get<std::string>();
            const std::string matrixId = sensor == "gyro" ? id.gyroMatrixId : id.accelMatrixId;
            check(matrixId.find(":" + sensor + ":" + matrix.at("recipe").get<std::string>()) != std::string::npos, name + " HC recipe selector");
            for (const auto& sample : matrix.at("samples")) {
                const auto input = sample.at("input"); const auto want = sample.at("output");
                double ox = 0, oy = 0, oz = 0;
                check(current::ymccApplyNamedImuMatrix(matrixId, input.at(0).get<double>(), input.at(1).get<double>(), input.at(2).get<double>(), ox, oy, oz), name + " matrix helper accepts selector");
                check(ox == want.at(0).get<double>() && oy == want.at(1).get<double>() && oz == want.at(2).get<double>(), name + " matrix output");
            }
        }
    }
}
static void testSteamDeckCaptureGate(const json& expected) {
    group("steamdeck-unimplemented-capture-gate");
    const auto& policy = expected.at("steamDeckCapturePolicy");
    const std::string cls = policy.at("fullName");
    const auto family = current::YmccFamily::ValveSteamDeck;
    for (const auto& key : policy.at("catalogKeys"))
        check(!current::oemKeyCaptureImplemented(family, cls, key.get<std::string>()), "SteamDeck/" + key.get<std::string>() + " declared Embedded key has no physical capture implementation");
    const std::vector<std::string> unknownKeys = {"__not_catalog_key__", "", "Special", "special ", " special2", "special2x", "home", "l4", std::string("special\0", 8), std::string(1, '\0')};
    for (const auto& key : unknownKeys)
        check(!current::oemKeyCaptureImplemented(family, cls, key), "SteamDeck noncatalog key also has no physical capture implementation");
    for (unsigned byte = 0; byte != 256; ++byte)
        check(!current::oemKeyCaptureImplemented(family, cls, std::string(1, static_cast<char>(byte))), "SteamDeck all one-byte noncatalog keys are unavailable");
    for (const auto& pair : legacyFamilies) {
        const auto other = static_cast<current::YmccFamily>(pair.newId);
        if (other == family) continue;
        for (const auto& key : {"special", "special2", "__not_catalog_key__"})
            check(current::oemKeyCaptureImplemented(other, cls, key), std::string(pair.name) + "/SteamDeck same class with wrong family retains compatibility true");
    }
    for (const auto other : {current::YmccFamily::ValveSteamMachine, static_cast<current::YmccFamily>(-1)})
        check(current::oemKeyCaptureImplemented(other, cls, "__not_catalog_key__"), "new/unknown family is outside SteamDeck correction");
    for (const auto& other : {std::string{}, std::string("SteamDeck"), cls + "x", "x" + cls, cls + " ", cls + std::string(1, '\0'), std::string("HandheldCompanion.Devices.SteamMachine")})
        for (const auto& key : {"special", "special2", "__not_catalog_key__"})
            check(current::oemKeyCaptureImplemented(family, other, key), "SteamDeck family with different/nonexact class retains compatibility true");
}
static void normalizeSteamDeckCaptureFields(json& keys, const json& policy, const std::string& context) {
    check(keys.size() == policy.at("catalogKeys").size(), context + " retains exactly two declared catalog keys");
    for (size_t i = 0; i < keys.size(); ++i) {
        auto& key = keys.at(i);
        check(key.at("keyId") == policy.at("catalogKeys").at(i), context + " retains exact catalog key order");
        check(key.at("physicalForm") == "embedded", context + " retains Embedded catalog form");
        for (const auto& field : policy.at("captureFields").items()) {
            check(key.contains(field.key()) && key.at(field.key()).type() == field.value().type() && key.at(field.key()) == field.value(),
                  context + "/" + key.at("keyId").get<std::string>() + " exact " + field.key());
            key.erase(field.key());
        }
    }
}
static void testCaptureAndCatalogQuery(const json& expected) {
    catalogQueries = json::array();
    captureObservations = json::array();
    group("oem.keys.get-lambda-return-and-log");
    const auto& policy = expected.at("oneXApplicationPolicy");
    const std::set<std::string> reportClasses = {"OneXPlayerX2", "OneXPlayer3", "OneXPlayerX2MiniPro"};
    for (const auto& model : expected.at("models")) {
        const std::string name = model.at("name");
        json row;
        if (name == "SteamMachine") row = json{{"manufacturer", "VALVE"}, {"product", "Fremont"}};
        else for (const auto& candidate : expected.at("identities")) {
            if (candidate.at("deviceClass").get<std::string>() == name) { row = candidate; break; }
        }
        check(!row.is_null() && !row.empty(), name + " query identity fixture");
        current::testLogging = true;
        const auto result = queryCurrent(row);
        check(result.at("supported").get<bool>() == !model.at("catalog").empty(), name + " query support");
        check(result.at("keys").size() == model.at("catalog").size(), name + " query count");
        check(current::testLogs.size() == 1, name + " query emits exactly one diagnostic log");
        const auto log = current::testLogs.back();
        check(log.at("event") == "oem-key-catalog-query", name + " query log event");
        check(log.at("count").get<int>() == static_cast<int>(result.at("keys").size()), name + " log count matches return");
        check(log.at("keys") == result.at("keys"), name + " log keys match return including capture route");
        check(log.at("schema") == 2, name + " query log schema");
        check(log.at("deviceClass") == model.at("fullName"), name + " query log exact device class");
        check(log.at("family") == result.at("familyId"), name + " query log/return family match");
        check(log.at("supported") == result.at("supported"), name + " query log/return support match");
        check(log.at("inputStateObserved") == false, name + " software implementation does not prove input observed");
        check(log.at("physicalDirectionVerified") == false, name + " software implementation does not prove physical direction");
        check(log.at("verificationScope").get<std::string>().find("not-physical-input-or-output-proof") != std::string::npos,
              name + " query log retains evidence boundary");
        json expectedKeys = json::array();
        for (size_t i = 0; i < result.at("keys").size(); ++i) {
            const auto& got = result.at("keys").at(i); const auto& want = model.at("catalog").at(i);
            check(got.at("keyId") == want.at("id"), name + " query key ID");
            check(got.at("label") == want.at("label"), name + " query label");
            check(got.at("backIndex") == want.at("backIndex"), name + " query rear ordinal");
            const auto form = want.at("form").get<std::string>() == "KeyboardVk" ? "keyboard-vk" : "hid-button";
            const auto trigger = want.at("clickOnly").get<bool>() ? "click-only" : "full";
            check(got.at("physicalForm") == form, name + " query form");
            check(got.at("triggerCapability") == trigger, name + " query trigger capability");
            const bool reported = got.contains("captureImplemented");
            const bool routeReported = got.contains("captureRoute");
            captureObservations.push_back({{"deviceClass", model.at("fullName")}, {"keyId", got.at("keyId")},
                {"captureImplementationReported", reported}, {"captureImplemented", reported ? got.at("captureImplemented") : json(nullptr)},
                {"captureRouteReported", routeReported}, {"captureRoute", routeReported ? got.at("captureRoute") : json(nullptr)},
                {"runtimeReadyProven", false}});
            const bool newOneX = reportClasses.contains(name);
            check(reported == newOneX, name + " captureImplementationReported exact new-class boundary");
            check(routeReported == newOneX, name + " captureRouteReported exact new-class boundary");
            json expectedKey = {{"keyId", want.at("id")}, {"label", want.at("label")},
                {"backIndex", want.at("backIndex")}, {"triggerCapability", trigger}, {"physicalForm", form}};
            if (newOneX) {
                check(got.at("captureImplemented").is_boolean() && got.at("captureImplemented") == true,
                      name + " all five catalog keys have implemented software capture");
                check(want.at("capture") == true, name + " independent software policy expects implemented capture");
                const auto& route = policy.at("routes").at(name).at(want.at("id").get<std::string>());
                check(want.at("captureRoute") == route, name + " independent policy/catalog route consistency");
                check(got.at("captureRoute").is_string() && got.at("captureRoute") == route,
                      name + "/" + got.at("keyId").get<std::string>() + " exact capture source ownership");
                check(!got.contains("captureReason"), name + " implemented key has no stale unavailable reason");
                expectedKey["captureImplemented"] = true;
                expectedKey["captureRoute"] = route;
            } else {
                check(!got.contains("captureReason"), name + " other classes retain old key JSON shape");
            }
            check(got == expectedKey, name + " complete returned key JSON matches reviewed policy");
            expectedKeys.push_back(expectedKey);
        }
        check(result == json{{"familyId", result.at("familyId")}, {"supported", !model.at("catalog").empty()}, {"keys", expectedKeys}},
              name + " complete query response matches catalog-only contract");
        catalogQueries.push_back({{"deviceClass", model.at("fullName")}, {"response", result}, {"log", log}});
        current::testLogging = false;
        check(queryCurrent(row) == result, name + " logging disabled leaves query return unchanged");
        check(current::testLogs.empty(), name + " logging disabled emits no catalog event");
        current::testLogging = true;
    }
    // Compare complete actual preimage/current JSON for every legacy alias. Only
    // exact SteamDeck keys may add the three validated unavailable-capture fields.
    const auto& steamDeckPolicy = expected.at("steamDeckCapturePolicy");
    std::set<std::string> legacyClasses;
    size_t steamDeckAliases = 0;
    for (const auto& row : expected.at("legacyIdentities")) {
        const std::string cls = row.at("deviceClass"); legacyClasses.insert(cls);
        const auto oldResult = queryBefore(row); const auto oldLogs = before::testLogs;
        const auto newResult = queryCurrent(row); const auto newLogs = current::testLogs;
        auto normalizedResult = newResult; auto normalizedLogs = newLogs;
        const auto& id = current::g_machineIdentity;
        const bool steamDeck = id.family == current::YmccFamily::ValveSteamDeck && id.deviceClass == steamDeckPolicy.at("fullName").get<std::string>();
        if (steamDeck) {
            ++steamDeckAliases;
            check(before::g_machineIdentity.family == before::YmccFamily::ValveSteamDeck && before::g_machineIdentity.deviceClass == id.deviceClass,
                  "SteamDeck exception requires matching exact frozen/current identity");
            check(newResult.at("supported").is_boolean() && newResult.at("supported") == true, "SteamDeck catalog remains supported");
            check(newLogs.size() == 1 && newLogs.at(0).at("keys") == newResult.at("keys"), "SteamDeck complete return/log keys have identical unavailable status");
            normalizeSteamDeckCaptureFields(normalizedResult.at("keys"), steamDeckPolicy, "SteamDeck return");
            normalizeSteamDeckCaptureFields(normalizedLogs.at(0).at("keys"), steamDeckPolicy, "SteamDeck log");
            current::testLogging = false;
            check(queryCurrent(row) == newResult, "SteamDeck logging disabled retains complete actual response");
            check(current::testLogs.empty(), "SteamDeck logging disabled emits no event");
            current::testLogging = true;
            steamDeckLegacyComparisons.push_back({{"identity", row}, {"beforeResponse", oldResult}, {"response", newResult},
                {"beforeLogs", oldLogs}, {"logs", newLogs}, {"normalizedResponse", normalizedResult}, {"normalizedLogs", normalizedLogs}});
        }
        check(normalizedResult == oldResult, cls + " complete legacy oem.keys.get response unchanged after exact SteamDeck field normalization");
        check(normalizedLogs == oldLogs, cls + " complete legacy oem-key-catalog-query log unchanged after exact SteamDeck field normalization");
        for (const auto& key : normalizedResult.at("keys")) {
            check(!key.contains("captureImplemented") && !key.contains("captureRoute") && !key.contains("captureReason"),
                  cls + " normalized legacy key JSON has no new capture fields");
        }
    }
    check(expected.at("legacyIdentities").size() == 181 && steamDeckAliases == 2, "all 181 aliases compared; only two exact SteamDeck aliases normalized");
    // Synthetic family/class combinations exercise the exact query boundary too.
    const std::string steamDeckClass = steamDeckPolicy.at("fullName");
    for (const auto& pair : legacyFamilies) {
        const auto family = static_cast<current::YmccFamily>(pair.newId);
        for (const auto& cls : {steamDeckClass, std::string{}, steamDeckClass + "x"}) {
            if (family == current::YmccFamily::ValveSteamDeck && cls == steamDeckClass) continue;
            before::g_machineIdentity = before::MachineIdentity{};
            current::g_machineIdentity = current::MachineIdentity{};
            before::g_machineIdentity.family = static_cast<before::YmccFamily>(pair.oldId);
            current::g_machineIdentity.family = family;
            before::g_machineIdentity.deviceClass = cls; current::g_machineIdentity.deviceClass = cls;
            before::testLogs.clear(); current::testLogs.clear();
            const auto oldQuery = before::extractedOemKeysGet(json::object());
            check(current::extractedOemKeysGet(json::object()) == oldQuery, std::string(pair.name) + " nonmatching identity complete query unchanged");
            check(current::testLogs == before::testLogs, std::string(pair.name) + " nonmatching identity complete logs unchanged");
        }
    }
    check(legacyClasses.size() == 80, "all 80 frozen legacy classes have complete response/log regression");
    const json old = json{{"manufacturer", "AYANEO"}, {"product", "NEXT 2"}};
    const auto oldResult = queryBefore(old); const auto oldLogs = before::testLogs;
    check(queryCurrent(old) == oldResult, "legacy NEXT2 complete oem.keys.get response unchanged");
    check(current::testLogs == oldLogs, "legacy NEXT2 complete query log unchanged");
    check(!queryCurrent(old).at("keys").at(0).contains("captureImplemented"), "legacy NEXT2 query has no new capture field");
}
static void testCaptureGateAndOldOneXDescriptor(const json& expected) {
    group("capture-gate-exact-five-keys");
    const auto& policy = expected.at("oneXApplicationPolicy");
    const std::vector<std::string> invalid = {"__not_catalog_key__", "", "Home", "HOME", "special", "m1", "m2", "l5", "r5", "home ", " home", "l4x", "xturbo", "Keyboard", "native-held", "*"};
    for (const auto& entry : policy.at("routes").items()) {
        const std::string cls = "HandheldCompanion.Devices." + entry.key();
        for (const auto& key : policy.at("legalKeys")) {
            const std::string id = key;
            check(current::oemKeyCaptureImplemented(current::YmccFamily::OneXPlayer, cls, id), cls + "/" + id + " software gate admits exact catalog key");
            for (const auto& bad : {id + "x", "x" + id, id + " ", " " + id, id + std::string(1, '\0'), std::string(1, '\0') + id}) {
                check(!current::oemKeyCaptureImplemented(current::YmccFamily::OneXPlayer, cls, bad), cls + " software gate rejects key padding/prefix/suffix/NUL");
            }
        }
        for (const auto& id : invalid) check(!current::oemKeyCaptureImplemented(current::YmccFamily::OneXPlayer, cls, id), cls + " software gate rejects noncatalog key");
        for (unsigned byte = 0; byte != 256; ++byte) {
            check(!current::oemKeyCaptureImplemented(current::YmccFamily::OneXPlayer, cls, std::string(1, static_cast<char>(byte))), cls + " software gate rejects all one-byte noncatalog inputs");
        }
        // Other families retain the pre-existing compatibility gate behavior.
        check(current::oemKeyCaptureImplemented(current::YmccFamily::Ayanero, cls, "__not_catalog_key__"), "non-OneX family retains legacy gate behavior");
    }
    for (const auto& row : expected.at("legacyIdentities")) {
        const auto id = currentIdentity(row);
        const auto catalog = current::oemCatalogForFamily(id.family, id.deviceClass);
        const bool steamDeck = id.family == current::YmccFamily::ValveSteamDeck && id.deviceClass == expected.at("steamDeckCapturePolicy").at("fullName").get<std::string>();
        for (int i = 0; i < catalog.count; ++i)
            check(current::oemKeyCaptureImplemented(id.family, id.deviceClass, catalog.keys[i].keyId) == !steamDeck, "legacy catalog capture gate unchanged except exact unavailable SteamDeck");
        check(current::oemKeyCaptureImplemented(id.family, id.deviceClass, "__not_catalog_key__") == !steamDeck, "legacy unknown-key compatibility true except exact unavailable SteamDeck");
    }
    group("onex-new-models-never-x1");
    const std::vector<std::string> oldClasses = {"OneXPlayerX1", "OneXPlayerX1AMD", "OneXPlayerX1Intel", "OneXPlayerX1Mini", "OneXPlayerX1Pro", "OneXPlayerApex"};
    for (const auto& simple : oldClasses) {
        const std::string cls = "HandheldCompanion.Devices." + simple;
        const auto a = current::onexCapDescriptorFor(cls); const auto b = before::onexCapDescriptorFor(cls);
        check(a.valid && a.independentRearProtocol, cls + " old descriptor remains admitted");
        check(a.valid == b.valid && a.independentRearProtocol == b.independentRearProtocol &&
              static_cast<int>(a.initProfile) == static_cast<int>(b.initProfile) && a.role21Orange == b.role21Orange,
              cls + " complete legacy descriptor unchanged");
        for (const auto& bad : {simple, cls + "x", "x" + cls, cls + std::string(1, '\0')}) {
            const auto d = current::onexCapDescriptorFor(bad);
            check(!d.valid && !d.independentRearProtocol, "old X1 descriptor admission still exact");
        }
    }
    for (const auto& entry : policy.at("routes").items()) {
        const std::string cls = "HandheldCompanion.Devices." + entry.key();
        for (const auto& candidate : {entry.key(), cls, cls + "x", "x" + cls, cls + std::string(1, '\0')}) {
            const auto d = current::onexCapDescriptorFor(candidate);
            check(!d.valid && !d.independentRearProtocol && d.initProfile == current::OnexInitProfile::None && !d.role21Orange,
                  candidate + " new model must never borrow old X1 descriptor");
        }
    }
}
static void testLegacyAndStableNumbers(const json& expected) {
    group("legacy-80-identities-profile-numbers-and-canonical");
    for (const auto& pair : legacyProfiles) check(pair.oldId == pair.newId, std::string("legacy profile number ") + pair.name);
    for (const auto& pair : legacyFamilies) check(pair.oldId == pair.newId, std::string("legacy family number ") + pair.name);
    check(static_cast<int>(current::YmccFamily::ValveSteamMachine) > static_cast<int>(current::YmccFamily::SuiPlay), "SteamMachine family appended after SuiPlay");
    check(static_cast<int>(current::OemChordProfileId::Ayaneo3) < static_cast<int>(current::OemChordProfileId::None), "new profile before None sentinel");
    for (const auto& name : expected.at("canonicalNames")) {
        uint64_t oldBit = 0, newBit = 0; const std::string code = name;
        check(before::nativeKeyboardShortcutMaskBit(code, oldBit), "legacy canonical exists");
        check(current::nativeKeyboardShortcutMaskBit(code, newBit) && oldBit == newBit, "legacy canonical bit unchanged");
    }
    for (const auto& row : expected.at("legacyIdentities")) {
        const before::MachineIdentity oldId = beforeIdentity(row);
        const current::MachineIdentity newId = currentIdentity(row);
        check(oldId.family == static_cast<before::YmccFamily>(static_cast<int>(newId.family)), "legacy family result unchanged");
        check(oldId.deviceClass == newId.deviceClass, "legacy device class unchanged");
        check(oldId.resolved == newId.resolved, "legacy resolved state unchanged");
        check(oldId.gyroMatrixId == newId.gyroMatrixId && oldId.accelMatrixId == newId.accelMatrixId, "legacy matrix selectors unchanged");
    }
}
static void testPowerGate() {
    group("power-lifecycle-and-aoac");
    auto setIdentity = [](current::YmccFamily family, const char* cls) { current::g_machineIdentity = current::MachineIdentity{}; current::g_machineIdentity.family = family; current::g_machineIdentity.deviceClass = cls; };
    const current::PowerLifecycle phases[] = {current::PowerLifecycle::Ready, current::PowerLifecycle::Resuming, current::PowerLifecycle::Suspending, current::PowerLifecycle::Suspended};
    for (bool aoac : {false, true}) {
        current::testAoAc = aoac;
        for (const auto phase : phases) {
            current::g_powerLifecycle.store(phase, std::memory_order_release);
            setIdentity(current::YmccFamily::ValveSteamMachine, "HandheldCompanion.Devices.SteamMachine");
            check(current::sgShouldKeepInputDuringStandby() == aoac, "SteamMachine keeps input only under AoAc");
            check(current::sgVirtualWriterAllowedNow() == (phase == current::PowerLifecycle::Ready || (phase == current::PowerLifecycle::Resuming && aoac)), "SteamMachine writer phase/AoAc");
            setIdentity(current::YmccFamily::Ayanero, "HandheldCompanion.Devices.AYANEO3");
            check(current::sgShouldKeepInputDuringStandby(), "AYANEO keeps input independent of AoAc");
            setIdentity(current::YmccFamily::OneXPlayer, "HandheldCompanion.Devices.OneXPlayerX2");
            check(current::sgShouldKeepInputDuringStandby(), "OneX keeps input independent of AoAc");
            setIdentity(current::YmccFamily::Unknown, "");
            check(current::sgShouldKeepInputDuringStandby() == aoac, "unknown family follows AoAc");
        }
    }
}
static json run(const json& expected) {
    try {
        testIdentityProfilesCatalog(expected);
        testProfilesAndEvents(expected);
        testMatrices(expected);
        testSteamDeckCaptureGate(expected);
        testCaptureAndCatalogQuery(expected);
        testCaptureGateAndOldOneXDescriptor(expected);
        testLegacyAndStableNumbers(expected);
        testPowerGate();
        return { {"status", "pass"}, {"checks", checks}, {"counts", counts}, {"catalogQueries", catalogQueries}, {"captureObservations", captureObservations}, {"steamDeckLegacyComparisons", steamDeckLegacyComparisons}, {"logSamples", current::testLogs} };
    } catch (const Failure& failure) {
        return { {"status", "fail"}, {"section", section}, {"checks", checks}, {"counts", counts}, {"error", failure.what()} };
    } catch (const std::exception& error) {
        return { {"status", "fail"}, {"section", section}, {"checks", checks}, {"counts", counts}, {"error", error.what()} };
    }
}
}
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: hc_remaining_models.exe expectations.json\n"; return 2; }
    try {
        std::ifstream input(argv[1]); if (!input) throw std::runtime_error("expectations file unavailable");
        const json expected = json::parse(input);
        const json report = test::run(expected);
        std::cout << report.dump() << '\n';
        return report.at("status") == "pass" ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 2;
    }
}