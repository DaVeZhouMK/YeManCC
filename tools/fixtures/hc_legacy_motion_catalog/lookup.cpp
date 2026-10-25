#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include "json.hpp"
#include "@CATALOG_HEADER@"

using json = nlohmann::json;
namespace legacy = ymcc::hc_legacy_motion;

int main(int argc, char** argv) {
    if (argc != 2) return 9;
    std::ifstream file(argv[1]);
    json expected;
    file >> expected;
    int failures = 0, checks = 0;
    auto test = [&](bool ok) { ++checks; if (!ok) ++failures; };
    test(std::size(legacy::recipes) == expected["recipes"].size());
    for (const auto& entry : expected["recipes"]) {
        const std::string cls = entry["deviceClass"], gyro = entry["gyroMatrixId"], accel = entry["accelMatrixId"];
        const auto* recipe = legacy::find(cls, gyro, accel);
        test(recipe != nullptr);
        test(legacy::containsClass(cls));
        if (recipe) {
            test(recipe->selectorId == entry["selectorId"].get<std::string>());
            test(recipe->sourceScope == entry["sourceScope"].get<std::string>());
            test(recipe->sourcePath == entry["sourcePath"].get<std::string>());
            test(recipe->sourceSha256 == entry["sourceSha256"].get<std::string>());
            test(recipe->controllerPreferred == entry["controllerPreferred"].get<bool>());
        }
        test(!legacy::find(cls, gyro, accel + "-unknown"));
        test(!legacy::find(cls, gyro + "-unknown", accel));
        test(!legacy::find(cls + "-unknown", gyro, accel));
        test(!legacy::find(cls, "", ""));
        test(!legacy::find(cls.substr(cls.find_last_of('.') + 1), gyro, accel));
        test(!legacy::containsClass(cls + "-unknown"));
        for (const auto& other : expected["recipes"]) {
            const std::string otherGyro = other["gyroMatrixId"], otherAccel = other["accelMatrixId"];
            bool exists = false;
            for (const auto& valid : expected["recipes"])
                if (valid["deviceClass"] == cls && valid["gyroMatrixId"] == otherGyro && valid["accelMatrixId"] == accel) exists = true;
            test((legacy::find(cls, otherGyro, accel) != nullptr) == exists);
            exists = false;
            for (const auto& valid : expected["recipes"])
                if (valid["deviceClass"] == cls && valid["gyroMatrixId"] == gyro && valid["accelMatrixId"] == otherAccel) exists = true;
            test((legacy::find(cls, gyro, otherAccel) != nullptr) == exists);
        }
    }
    for (const auto& cls : expected["excluded"]) {
        test(!legacy::containsClass(cls.get<std::string>()));
        test(!legacy::find(cls.get<std::string>(), "", ""));
    }
    test(!legacy::containsClass(""));
    test(!legacy::find("", "", ""));
    std::cout << json({{"checks", checks}, {"failures", failures}}).dump();
    return failures ? 7 : 0;
}
