// Appended after hash-bound production definitions in before:: and current::.
// This fixture contains expectations/driver code, never an alternate OEM evaluator.
namespace test {
static unsigned long long checks = 0;
static std::string section;
static json counts = json::object();
static void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(section + ": " + message);
}
static void group(const char* name) { section = name; counts[section] = 0; }
static void finishGroup(unsigned long long start) { counts[section] = checks - start; }
struct Frame {
    uint64_t mask = UINT64_MAX;
    int state = 7, physical = 7;
    uint32_t base = UINT32_MAX, prefix = UINT32_MAX;
    bool operator==(const Frame&) const = default;
};
static Frame evaluate(int profile, uint32_t kb, bool mouse = false, bool ready = false) {
    Frame result;
    current::oemChordEvalRules(profile, kb, mouse, ready, &result.mask, &result.state,
        &result.physical, &result.base, &result.prefix);
    return result;
}
static void next2Frame(const Frame& frame, uint64_t mask, const std::string& context) {
    check(frame.mask == mask, context + " canonical mask");
    check(frame.state == 0 && frame.physical == 0 && frame.base == 0 && frame.prefix == 0,
        context + " has no inferred/default physical rear slots");
}
static uint64_t expectedBit(const json& key) { return std::stoull(key.at("canonicalBit").get<std::string>()); }
static uint64_t expectedMask(const json& keys, unsigned subset) {
    uint64_t mask = 0;
    for (unsigned i = 0; i < keys.size(); ++i) if (subset & (1u << i)) mask |= expectedBit(keys[i]);
    return mask;
}
static uint32_t expectedKeyboard(const json& keys, unsigned subset) {
    uint32_t mask = 0;
    for (unsigned i = 0; i < keys.size(); ++i) if (subset & (1u << i)) mask |= keys[i].at("kbMask").get<uint32_t>();
    return mask;
}
static uint32_t keyboardFromActualVk(const json& keys, unsigned subset) {
    uint32_t mask = 0;
    for (unsigned i = 0; i < keys.size(); ++i) if (subset & (1u << i))
        for (const auto& vk : keys[i].at("vks")) mask |= current::oemBackHookVkBit(vk.get<DWORD>());
    return mask;
}
static current::MachineIdentity identity(std::string manufacturer, std::string model) {
    current::MachineIdentity result;
    result.manufacturer = current::ymccDmiNormalize(std::move(manufacturer));
    result.product = current::ymccDmiNormalize(std::move(model));
    current::ymccResolveFamily(result);
    return result;
}
static void expectPublished(uint64_t mask, const std::string& context) {
    check(current::g_oemKeyMask.load() == mask, context + " published mask");
    check(current::g_oemBackStateKb.load() == 0, context + " rear publication");
}
static void run(const json& expected) {
    const auto& keys = expected.at("keys");
    const auto cls = expected.at("deviceClass").get<std::string>();
    const int next2 = static_cast<int>(current::OemChordProfileId::AyaneoNext2);
    const int none = static_cast<int>(current::OemChordProfileId::None);
    const auto bitFor = [&](const std::string& code) {
        uint64_t bit = 0; check(current::nativeKeyboardShortcutMaskBit(code, bit), "canonical resolves " + code); return bit;
    };
    unsigned long long start;
    group("identity-aliases-and-negative-boundaries"); start = checks;
    unsigned aliases = 0, negativeIdentities = 0;
    for (const auto& manufacturer : expected.at("manufacturers")) for (const auto& model : expected.at("models")) {
        auto id = identity(manufacturer.get<std::string>(), model.get<std::string>());
        ++aliases;
        check(id.resolved && id.family == current::YmccFamily::Ayanero && id.deviceClass == cls, "exact HC alias");
        check(id.inputHidKey == expected.at("inputHidKey").get<std::string>(), "source denotes keyboard OEM only");
        check(current::oemChordProfileIdFor(id.family, id.deviceClass) == next2, "dedicated profile alias");
        check(current::oemCatalogForFamily(id.family, id.deviceClass).count == 9, "nine-key catalog alias");
        for (const auto& matrix : expected.at("matrices"))
            check((matrix.at("sensor").get<std::string>() == "gyro" ? id.gyroMatrixId : id.accelMatrixId) == matrix.at("id").get<std::string>(), "HC recipe selector");
        auto lower = identity(current::ascii_lower(manufacturer.get<std::string>()), current::ascii_lower(model.get<std::string>()));
        check(lower.deviceClass == cls, "production DMI uppercase normalization");
        auto withNul = identity(manufacturer.get<std::string>() + std::string(1, '\0'), model.get<std::string>() + std::string(1, '\0'));
        check(withNul.deviceClass == cls, "production DMI NUL normalization");
    }
    check(aliases == 15, "three manufacturers times five models");
    const auto rejectNext2 = [&](const std::string& manufacturer, const std::string& model) {
        // Reuse a previously resolved identity: unknown input must revoke old NEXT2 recipes/profile.
        auto id = identity("AYANEO", "NEXT II");
        id.manufacturer = current::ymccDmiNormalize(manufacturer);
        id.product = current::ymccDmiNormalize(model);
        current::ymccResolveFamily(id);
        check(id.deviceClass != cls, "near identity must not resolve NEXT2: " + manufacturer + "/" + model);
        check(current::oemChordProfileIdFor(id.family, id.deviceClass) != next2, "near identity must not enable dedicated profile");
        check(id.gyroMatrixId.find("ayanero-next2") == std::string::npos && id.accelMatrixId.find("ayanero-next2") == std::string::npos,
            "previous NEXT2 recipe revoked");
        check(id.inputHidKey != expected.at("inputHidKey").get<std::string>(), "previous NEXT2 keyboard route revoked");
        ++negativeIdentities;
    };
    for (const char* manufacturer : {"UNKNOWN", "", "NOT AYANEO", "AYANEOX", "AYADEVICE INC", "AYA INC", "ASUSTEK COMPUTER INC.", "MICRO-STAR INTERNATIONAL CO., LTD."})
        for (const auto& model : expected.at("models")) rejectNext2(manufacturer, model.get<std::string>());
    for (const auto& manufacturer : expected.at("manufacturers"))
        for (const char* model : {"NEXT", "NEXT I", "NEXT III", "NEXT 20", "NEXT 2 PRO", "NEXT2", "AB090", "XAB09", "NEXT II ", " NEXT II", "AYANEO NEXT IIX", "AYANEO NEXT 20"})
            rejectNext2(manufacturer.get<std::string>(), model);
    for (const char* fakeClass : {"HandheldCompanion.Devices.AYANEONEXT20", "HandheldCompanion.Devices.AYANEONEXT2Pro", "AYANEONEXT2", "XHandheldCompanion.Devices.AYANEONEXT2"}) {
        check(current::oemChordProfileIdFor(current::YmccFamily::Ayanero, fakeClass) != next2, "selector requires exact NEXT2 class");
        check(current::oemCatalogForFamily(current::YmccFamily::Ayanero, fakeClass).count != 9, "catalog requires exact NEXT2 class");
    }
    for (auto family : {current::YmccFamily::Unknown, current::YmccFamily::Gpd, current::YmccFamily::MsiClaw})
        check(current::oemChordProfileIdFor(family, cls) != next2, "class alone cannot cross family gate");
    finishGroup(start);

    group("independent-HC-nine-key-contract-and-canonical-IDs"); start = checks;
    const auto catalog = current::oemCatalogForFamily(current::YmccFamily::Ayanero, cls);
    const auto& profile = current::oemChordProfileFor(next2);
    check(profile.count == 9 && catalog.count == 9, "separate nine-item production profile and catalog");
    std::set<uint64_t> bits;
    std::set<std::string> ids;
    for (int i = 0; i < 9; ++i) {
        const auto& expectedKey = keys[i]; const auto& key = catalog.keys[i]; const auto& rule = profile.rules[i];
        const std::string name = expectedKey.at("id");
        check(name == key.keyId && expectedKey.at("hcName").get<std::string>() == key.label, "HC name to stable canonical ID: " + name);
        check(key.backIndex == 0 && key.form == current::OemKeyForm::KeyboardVk && !key.clickOnly, "catalog has editable press/release source, no guessed position");
        check(rule.keys == expectedKey.at("kbMask").get<uint32_t>() && rule.bit == expectedBit(expectedKey), "rule independently matches HC chord and canonical bit");
        check(rule.backIndex == 0 && rule.group == 0 && !rule.mouseChord, "nine independent rules, no implicit rear slots");
        check(bitFor(name) == expectedBit(expectedKey), "agreed canonical bit is stable");
        check(ids.insert(key.keyId).second && bits.insert(rule.bit).second, "nine OEM IDs and bits are unique");
    }
    bits.clear();
    for (const auto& code : expected.at("canonicalNames")) {
        const auto name = code.get<std::string>(); uint64_t oldBit = 0;
        check(before::nativeKeyboardShortcutMaskBit(name, oldBit), "frozen canonical name resolves");
        const auto bit = bitFor(name);
        check(bit == oldBit && bit && (bit & (bit - 1)) == 0, "all canonical IDs keep single-bit encoding");
        check(bits.insert(bit).second, "all canonical names are mutually noncolliding: " + name);
    }
    for (const char* invalid : {"LC1", "RC1", "AYANEO Button", "OEM6", "not-a-key", "M1", ""}) {
        uint64_t sentinel = 0x1234; check(!current::nativeKeyboardShortcutMaskBit(invalid, sentinel), "unknown canonical spelling rejected");
    }
    finishGroup(start);

    group("VK-to-mask-and-edge-events"); start = checks;
    check(current::oemBackHookVkBit(0x82) == (1u << 28) && current::oemBackHookVkBit(0x83) == (1u << 29), "new F19/F20 bits 28/29");
    for (DWORD vk = 0; vk < 256; ++vk) {
        if (vk == 0x82 || vk == 0x83) check(before::oemBackHookVkBit(vk) == 0, "F19/F20 were unassigned in preimage");
        else check(current::oemBackHookVkBit(vk) == before::oemBackHookVkBit(vk), "all other VK mappings unchanged");
    }
    for (unsigned k = 0; k < 9; ++k) {
        std::vector<DWORD> vks = keys[k].at("vks").get<std::vector<DWORD>>();
        for (unsigned order = 0; order < 2; ++order) {
            uint32_t down = 0;
            for (unsigned i = 0; i < vks.size(); ++i) {
                const auto vk = vks[order ? vks.size() - 1 - i : i];
                down |= current::oemBackHookVkBit(vk);
                next2Frame(evaluate(next2, down), i + 1 == vks.size() ? expectedBit(keys[k]) : 0, "press partial/full HC chord");
            }
            check(down == keys[k].at("kbMask").get<uint32_t>(), "VK-derived input matches independent bit contract");
            for (const auto vk : vks) {
                down &= ~current::oemBackHookVkBit(vk);
                next2Frame(evaluate(next2, down), 0, "release any required member releases HC chord");
            }
            next2Frame(evaluate(next2, 0, true, true), 0, "explicit clear and irrelevant mouse/gate");
        }
    }
    for (const uint32_t partial : {uint32_t(current::kVkWin), uint32_t(current::kVkD), uint32_t(0)})
        next2Frame(evaluate(next2, partial), 0, "Win/D alone is not Custom Small or standalone Win");
    next2Frame(evaluate(next2, current::kVkWin | current::kVkD), expectedBit(keys[1]), "Win+D emits only cksmall");
    check(!(evaluate(next2, current::kVkWin | current::kVkD).mask & bitFor("win")), "no standalone Win canonical event");
    current::oemChordEvalRules(next2, 0, false, false, nullptr, nullptr, nullptr, nullptr, nullptr);
    finishGroup(start);

    group("all-512-combinations-and-262144-state-transitions"); start = checks;
    current::g_machineIdentity = identity("AYANEO", "NEXT II");
    std::set<uint64_t> combinations;
    for (unsigned subset = 0; subset < 512; ++subset) {
        const auto kb = keyboardFromActualVk(keys, subset); const auto mask = expectedMask(keys, subset);
        check(kb == expectedKeyboard(keys, subset), "all combined VK masks match independent oracle");
        for (bool mouse : {false, true}) for (bool gate : {false, true})
            next2Frame(evaluate(next2, kb, mouse, gate), mask, "nine-key subset " + std::to_string(subset));
        check(combinations.insert(mask).second, "512 distinct canonical combinations");
        json names = json::array();
        for (unsigned i = 0; i < 9; ++i) if (subset & (1u << i)) names.push_back(keys[i].at("id"));
        check(current::gamepadOemPressedKeys(mask) == names, "production canonical-name publication is catalog-limited");
        check(current::gamepadOemPressedKeys(mask | bitFor("guide")) == names, "unrelated OEM bit does not leak through catalog");
        for (unsigned k = 0; k < 9; ++k)
            next2Frame(evaluate(next2, keyboardFromActualVk(keys, subset ^ (1u << k))), expectedMask(keys, subset ^ (1u << k)), "each toggle edge from every subset");
        // Poison output storage with the preceding frame. The real evaluator must replace every output.
        for (unsigned after = 0; after < 512; ++after) {
            auto result = evaluate(next2, kb);
            current::oemChordEvalRules(next2, keyboardFromActualVk(keys, after), false, false,
                &result.mask, &result.state, &result.physical, &result.base, &result.prefix);
            next2Frame(result, expectedMask(keys, after), "all ordered transitions");
        }
    }
    finishGroup(start);

    group("production-disable-prefix-and-simulated-profile-sleep-sequences"); start = checks;
    for (unsigned subset = 0; subset < 512; ++subset) {
        current::g_oemBackProfileId.store(next2);
        current::g_oemKbDownRaw.store(0); // Simulated environment: NEXT2 owns the LL keyboard channel.
        current::g_oemKbDown = keyboardFromActualVk(keys, subset);
        current::g_oemMouseLeft = current::g_oemMouseX2 = true;
        current::g_oemKeyHooksActive.store(true);
        current::oemBackHookRecompute();
        expectPublished(expectedMask(keys, subset), "production recompute before simulated disable");
        current::extractedDisablePublicationPrefix();
        check(current::g_oemKbDown == 0 && !current::g_oemMouseLeft && !current::g_oemMouseX2 && !current::g_oemKeyHooksActive.load(), "production disable prefix clears its LL/mouse ownership");
        expectPublished(0, "production disable prefix releases previous mask");
        // These assignments model dispatcher order, not a real Windows power notification.
        current::g_oemBackProfileId.store(none); current::oemBackHookRecompute();
        expectPublished(0, "simulated inactive profile generation");
        current::g_oemBackProfileId.store(next2); current::oemBackHookRecompute();
        expectPublished(0, "simulated resume/new profile must not inherit preceding mask");
    }
    // The actual prefix deliberately preserves the independent Raw Input owner.
    current::g_oemKbDownRaw.store(keys[5].at("kbMask").get<uint32_t>());
    current::g_oemKbDown = keys[0].at("kbMask").get<uint32_t>();
    current::extractedDisablePublicationPrefix();
    expectPublished(expectedBit(keys[5]), "surviving simulated Raw owner is recomputed, never falsely called fully released");
    check(current::g_oemKbDownRaw.load() == keys[5].at("kbMask").get<uint32_t>(), "LL disable does not clear another owner");
    current::g_oemKbDownRaw.store(0); current::oemBackHookRecompute(); expectPublished(0, "explicit Raw owner simulation reset");
    current::g_oemBackProfileId.store(static_cast<int>(current::OemChordProfileId::Msi));
    current::g_oemKbDown = current::oemBackHookVkBit(0x7a); current::oemBackHookRecompute();
    check(current::g_oemKeyMask.load() == bitFor("m1") && current::g_oemBackStateKb.load() == 2, "legacy MSI preceding generation is nonneutral");
    current::extractedDisablePublicationPrefix();
    current::g_oemBackProfileId.store(next2); current::oemBackHookRecompute();
    expectPublished(0, "MSI-to-NEXT2 simulated generation withdraws old mask and rear output");
    finishGroup(start);

    group("explicit-rearMap-parser-and-default-zero"); start = checks;
    current::oemRearMapRefresh(json::object());
    check(current::g_oemRearMapToL5.load() == 0 && current::g_oemRearMapToR5.load() == 0, "no default mapping into two virtual rear slots");
    for (const auto& key : keys) for (const char* target : {"left", "right", "both", "off", "unknown"}) {
        const std::string id = key.at("id"); const uint64_t bit = expectedBit(key);
        current::oemRearMapRefresh({{"oemRearMap", {{id, target}}}});
        const std::string dest = target;
        check(current::g_oemRearMapToL5.load() == (dest == "left" || dest == "both" ? bit : 0), "explicit left mapping for " + id);
        check(current::g_oemRearMapToR5.load() == (dest == "right" || dest == "both" ? bit : 0), "explicit right mapping for " + id);
    }
    current::oemRearMapRefresh({{"oemRearMap", {{" LC1 ", "left"}, {"a", "both"}, {"m1", 1}, {"ckbig", " Both "}}}});
    check(current::g_oemRearMapToL5.load() == expectedBit(keys[0]) && current::g_oemRearMapToR5.load() == expectedBit(keys[0]), "rearMap accepts canonical keys only with production whitespace normalization");
    for (const json invalid : {json(), json::array(), json{{"oemRearMap", json::array()}}, json::object()}) {
        current::oemRearMapRefresh(invalid);
        check(current::g_oemRearMapToL5.load() == 0 && current::g_oemRearMapToR5.load() == 0, "missing/invalid explicit map clears prior configuration");
    }
    finishGroup(start);

    group("independent-HC-IMU-recipes-and-production-parser"); start = checks;
    for (const auto& matrix : expected.at("matrices")) {
        const std::string recipe = matrix.at("id");
        std::vector<std::string> tokens; std::string swap;
        check(current::ymccSplitAxisRecipe("neg-x-neg-z-y", tokens, swap) && swap.empty() && tokens == std::vector<std::string>{"neg-x", "neg-z", "y"}, "production result-form tokenization");
        for (const auto& sample : matrix.at("samples")) {
            const auto in = sample.at("input").get<std::array<double, 3>>();
            const auto wanted = sample.at("output").get<std::array<double, 3>>();
            double x = 999, y = 999, z = 999;
            check(current::ymccApplyNamedImuMatrix(recipe, in[0], in[1], in[2], x, y, z), "production IMU recipe accepted");
            check(x == wanted[0] && y == wanted[1] && z == wanted[2], "output independently computed from fixed HC Axis/AxisSwap");
        }
    }
    int axis = 99, sign = 99;
    check(current::ymccParseAxisToken("neg-z", axis, sign) && axis == 2 && sign == -1, "signed axis token");
    for (const char* invalid : {"q", "neg-", "NEG-x", "xx"}) check(!current::ymccParseAxisToken(invalid, axis, sign), "invalid axis token rejected");
    for (const char* invalid : {"", "no-sensor-suffix", "hc:gyro:q-y-z", "hc:accel:x-y", "hc:gyro:x-y-z-swap-invalid"}) {
        double x, y, z; check(!current::ymccApplyNamedImuMatrix(invalid, 1, 2, 3, x, y, z), "invalid IMU recipe is not accepted");
    }
    check(!current::ymccDeviceClassSupportsLighting(cls), "NEXT2 lighting remains unsupported by production gate");
    check(before::ymccDeviceClassSupportsLighting(cls) == current::ymccDeviceClassSupportsLighting(cls), "no new lighting capability advertisement");
    finishGroup(start);

    group("frozen-legacy-profile-numbers-tables-and-evaluator-parity"); start = checks;
    check(next2 == static_cast<int>(before::OemChordProfileId::None), "NEXT2 appended at former None slot");
    // Subsequent reviewed model integration appended these two profiles after NEXT2.
    // Keep the exact approved tail order, plus every original numeric/table invariant below.
    check(static_cast<int>(current::OemChordProfileId::Ayaneo3) == next2 + 1, "reviewed AYANEO3 appended after NEXT2");
    check(static_cast<int>(current::OemChordProfileId::OneXX2MiniPro) == next2 + 2, "reviewed OneX Mini appended after AYANEO3");
    check(none == next2 + 3, "None follows the exact three reviewed profile additions");
    unsigned profileCount = 0, legacyEvaluations = 0;
    for (const auto& pair : legacyProfiles) {
        ++profileCount; check(pair.newId == pair.oldId, std::string("legacy numeric profile ID unchanged: ") + pair.name);
        const auto& old = before::oemChordProfileFor(pair.oldId); const auto& now = current::oemChordProfileFor(pair.newId);
        check(old.count == now.count, std::string("legacy rule count unchanged: ") + pair.name);
        for (int i = 0; i < old.count; ++i) {
            const auto& a = old.rules[i]; const auto& b = now.rules[i];
            check(a.keys == b.keys && a.bit == b.bit && a.backIndex == b.backIndex && a.group == b.group && a.mouseChord == b.mouseChord, "every legacy rule field unchanged");
        }
        for (unsigned subset = 0; subset < (1u << old.count); ++subset) {
            uint32_t kb = 0;
            for (int i = 0; i < old.count; ++i) if (subset & (1u << i)) kb |= old.rules[i].keys;
            for (const uint32_t prefix : {uint32_t(0), uint32_t(15)}) for (bool mouse : {false, true}) for (bool ready : {false, true}) {
                Frame wanted;
                before::oemChordEvalRules(pair.oldId, kb | prefix, mouse, ready, &wanted.mask, &wanted.state, &wanted.physical, &wanted.base, &wanted.prefix);
                check(evaluate(pair.newId, kb | prefix, mouse, ready) == wanted, std::string("legacy evaluator exact parity: ") + pair.name);
                ++legacyEvaluations;
            }
        }
    }
    for (const int invalid : {-1, none, none + 1, 1000000}) next2Frame(evaluate(invalid, UINT32_MAX, true, true), 0, "invalid/None profile outputs neutral");
    finishGroup(start);

    group("frozen-NEXT-Lite-ROG-MSI-GPD-identities-and-catalogs"); start = checks;
    unsigned legacyIdentities = 0;
    for (const auto& entry : std::vector<std::pair<std::string, std::string>>{
        {"AYANEO", "NEXT"}, {"AYANEO", "NEXT LITE"}, {"AYANEO", "NEXT LITE 4500U"}, {"AYANEO", "NEXT ADVANCE"}, {"AYANEO", "NEXT PRO"}, {"AYADEVICE", "NEXT LITE"},
        {"ASUSTEK COMPUTER INC.", "RC71L"}, {"ASUSTEK COMPUTER INC.", "RC72LA"}, {"ASUSTEK COMPUTER INC.", "RC73XA"}, {"ASUSTEK COMPUTER INC.", "RC73YA"},
        {"MICRO-STAR INTERNATIONAL CO., LTD.", "MS-1T41"}, {"MICRO-STAR INTERNATIONAL CO., LTD.", "MS-1T42"}, {"MICRO-STAR INTERNATIONAL CO., LTD.", "MS-1T52"},
        {"MICRO-STAR INTERNATIONAL CO., LTD.", "MS-1T8K"}, {"MICRO-STAR INTERNATIONAL CO., LTD.", "MS-1T91"},
        {"GPD", "G1618-03"}, {"GPD", "G1618-04"}, {"GPD", "G1618-05"}, {"GPD", "G1617-01"}, {"GPD", "G1619-04"}, {"GPD", "G1619-05"}}) {
        const auto id = identity(entry.first, entry.second);
        before::MachineIdentity old; old.manufacturer = before::ymccDmiNormalize(entry.first); old.product = before::ymccDmiNormalize(entry.second); before::ymccResolveFamily(old);
        check(old.resolved && id.resolved, "legacy regression input resolves in both snapshots: " + entry.second);
        check(id.deviceClass == old.deviceClass && static_cast<int>(id.family) == static_cast<int>(old.family) && id.inputHidKey == old.inputHidKey &&
            id.gyroMatrixId == old.gyroMatrixId && id.accelMatrixId == old.accelMatrixId, "legacy identity/IMU/source unchanged: " + entry.second);
        check(current::oemChordProfileIdFor(id.family, id.deviceClass) == before::oemChordProfileIdFor(old.family, old.deviceClass), "legacy selector numeric ID unchanged");
        const auto a = before::oemCatalogForFamily(old.family, old.deviceClass); const auto b = current::oemCatalogForFamily(id.family, id.deviceClass);
        check(a.count == b.count, "legacy catalog count unchanged");
        for (int i = 0; i < a.count; ++i) check(std::string(a.keys[i].keyId) == b.keys[i].keyId && std::string(a.keys[i].label) == b.keys[i].label && a.keys[i].backIndex == b.keys[i].backIndex &&
            static_cast<int>(a.keys[i].form) == static_cast<int>(b.keys[i].form) && a.keys[i].clickOnly == b.keys[i].clickOnly, "legacy catalog fields unchanged");
        ++legacyIdentities;
    }
    finishGroup(start);
    std::cout << json{{"status", "pass"}, {"checks", checks}, {"groups", counts}, {"identityAliases", aliases}, {"negativeIdentities", negativeIdentities},
        {"combinations", 512}, {"orderedTransitions", 262144}, {"legacyProfiles", profileCount}, {"legacyEvaluations", legacyEvaluations}, {"legacyIdentities", legacyIdentities},
        {"lifecycleEvidence", "production disable publication prefix plus in-memory simulated profile/sleep order; no Windows sleep or hook-dispatch acceptance"},
        {"hardwareEvidence", "none; no Host/HID/Steam or device I/O"}, {"fanEvidence", "not exercised; no support assertion"}}.dump() << '\n';
}
} // namespace test
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected expectations.json path");
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("cannot read expectations.json");
        json expected; input >> expected; test::run(expected); return 0;
    } catch (const std::exception& error) {
        std::cout << json{{"status", "fail"}, {"checks", test::checks}, {"section", test::section}, {"error", error.what()}}.dump() << '\n';
        return 1;
    }
}
