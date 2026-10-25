#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
@PRODUCTION_BLOCKS@
int main(int argc, char** argv) {
    if (argc != 2) return 9;
    std::ifstream file(argv[1]);
    json inputs;
    file >> inputs;
    json rows = json::array();
    const json samples = json::parse(R"ymcc(@SAMPLES@)ymcc");
    for (const auto& input : inputs) {
        MachineIdentity id;
        id.resolved = true;
        id.deviceClass = "stale";
        id.gyroMatrixId = "stale";
        id.accelMatrixId = "stale";
        id.manufacturer = ymccDmiNormalize(input["manufacturer"]);
        id.product = ymccDmiNormalize(input["product"]);
        id.systemProduct = ymccDmiNormalize(input["systemProduct"]);
        id.processor = ymccDmiNormalize(input["processor"]);
        id.systemVersion = ymccDmiNormalize(input["version"]);
        ymccResolveFamily(id);
        json vectors = json::array();
        for (const auto& sample : samples) {
            double gx = 0, gy = 0, gz = 0, ax = 0, ay = 0, az = 0;
            const bool gyroOk = ymccApplyNamedImuMatrix(id.gyroMatrixId, sample[0], sample[1], sample[2], gx, gy, gz);
            const bool accelOk = ymccApplyNamedImuMatrix(id.accelMatrixId, sample[0], sample[1], sample[2], ax, ay, az);
            vectors.push_back({{"gyroOk", gyroOk}, {"accelOk", accelOk}, {"gyro", {gx, gy, gz}}, {"accel", {ax, ay, az}}});
        }
        rows.push_back({{"id", input["id"]}, {"resolved", id.resolved}, {"deviceClass", id.deviceClass},
            {"gyroMatrixId", id.gyroMatrixId}, {"accelMatrixId", id.accelMatrixId}, {"vectors", vectors}});
    }
    std::cout << rows.dump();
}
