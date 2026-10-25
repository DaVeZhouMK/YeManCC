static const ymcc::hc_model_motion::Recipe* inputCaptureGenericHcMotionRecipe(const MachineIdentity& identity) {
    if(!identity.attempted||identity.resolved||identity.family!=YmccFamily::Unknown||
        !identity.deviceClass.empty()||!identity.gyroMatrixId.empty()||!identity.accelMatrixId.empty()||!identity.inputHidKey.empty())return nullptr;
    MachineIdentity resolved=identity;ymccResolveFamily(resolved);
    if(resolved.resolved||resolved.family!=YmccFamily::Unknown||!resolved.deviceClass.empty()||
        !resolved.gyroMatrixId.empty()||!resolved.accelMatrixId.empty()||!resolved.inputHidKey.empty())return nullptr;
    return normalizeHcDmi(identity.manufacturer)=="ASUSTEK COMPUTER INC."
        ? &ymcc::hc_generic_motion::asus : &ymcc::hc_generic_motion::defaultDevice;
}
