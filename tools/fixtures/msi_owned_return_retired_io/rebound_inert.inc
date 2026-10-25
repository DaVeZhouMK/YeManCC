// This retired-I/O suite has no rebound DInput source. Keep only the new
// production boundary types available; an accidental release is a test failure.
struct HidHideMsiMainSourceRecord {
    bool valid=false;unsigned generation=0,vendorId=0,productId=0;
    std::string interfacePath,usbInstance;
};
static bool msiDinputMainSourceRecordSnapshot(HidHideMsiMainSourceRecord&){return false;}
static void msiDinputReleasePad(const char*){throw std::logic_error("unexpected rebound release in retired-I/O suite");}
static void msiDinputClearOwnedState(const char*){throw std::logic_error("unexpected rebound clear in retired-I/O suite");}
