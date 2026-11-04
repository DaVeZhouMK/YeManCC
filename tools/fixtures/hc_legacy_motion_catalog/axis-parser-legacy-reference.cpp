// Immutable comparison: actual pre-optimization mainline mathematics.
// Do not relabel it to match a later consumer; compare new behavior against it.
static bool ymccParseAxisToken(const std::string& tok, int& axis, int& sign) {
    sign = 1;
    std::string t = tok;
    if (t.rfind("neg-", 0) == 0) { sign = -1; t = t.substr(4); }
    if (t == "x") axis = 0;
    else if (t == "y") axis = 1;
    else if (t == "z") axis = 2;
    else return false;
    return true;
}

static bool ymccSplitAxisRecipe(const std::string& recipe, std::vector<std::string>& tokens, std::string& swap) {
    tokens.clear();
    swap.clear();
    std::string core = recipe;
    const auto swapAt = recipe.rfind("-swap-");
    if (swapAt != std::string::npos) {
        swap = recipe.substr(swapAt + 6);
        core = recipe.substr(0, swapAt);
    }
    size_t i = 0;
    while (i < core.size()) {
        if (core.compare(i, 4, "neg-") == 0 && i + 4 < core.size()) {
            tokens.emplace_back(core.substr(i, 5));
            i += 5;
        } else if (core[i] == 'x' || core[i] == 'y' || core[i] == 'z') {
            tokens.emplace_back(core.substr(i, 1));
            i += 1;
        } else return false;
        if (i < core.size()) {
            if (core[i] != '-') return false;
            ++i;
        }
    }
    return tokens.size() == 3;
}

static bool ymccApplyNamedImuMatrix(const std::string& matrixId, double ix, double iy, double iz,
                                   double& ox, double& oy, double& oz) noexcept {
    ox = ix; oy = iy; oz = iz;
    if (matrixId.empty()) return false;
    const auto gyroAt = matrixId.rfind(":gyro:");
    const auto accelAt = matrixId.rfind(":accel:");
    size_t recipeAt = std::string::npos;
    if (gyroAt != std::string::npos) recipeAt = gyroAt + 6;
    else if (accelAt != std::string::npos) recipeAt = accelAt + 7;
    if (recipeAt == std::string::npos) return false;
    const std::string recipe = matrixId.substr(recipeAt);
    std::vector<std::string> tokens;
    std::string swap;
    if (!ymccSplitAxisRecipe(recipe, tokens, swap)) return false;
    int axis[3]{}, sign[3]{};
    for (int k = 0; k < 3; ++k)
        if (!ymccParseAxisToken(tokens[k], axis[k], sign[k])) return false;
    const double in[3] = { ix, iy, iz };
    double scaled[3];
    if (swap.empty()) {
        // Result form: token k names which input (and sign) becomes output k.
        ox = sign[0] * in[axis[0]];
        oy = sign[1] * in[axis[1]];
        oz = sign[2] * in[axis[2]];
        return true;
    }
    // Axis+swap form: scale XYZ in place, then permute like HC AxisSwap.
    scaled[0] = sign[0] * in[0];
    scaled[1] = sign[1] * in[1];
    scaled[2] = sign[2] * in[2];
    double out[3] = { scaled[0], scaled[1], scaled[2] };
    if (swap == "yz") { out[1] = scaled[2]; out[2] = scaled[1]; }
    else if (swap == "xy") { out[0] = scaled[1]; out[1] = scaled[0]; }
    else if (swap == "yzx") { out[0] = scaled[1]; out[1] = scaled[2]; out[2] = scaled[0]; }
    else if (swap == "zyx") { out[0] = scaled[2]; out[1] = scaled[0]; out[2] = scaled[1]; }
    else return false;
    ox = out[0]; oy = out[1]; oz = out[2];
    return true;
}
