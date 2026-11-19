// This consumer's original source pairs are typed WinRT/ACPI pairs.
// Keep the new, unselected SDK leaf compilable without opening a real Provider.
// Any call is a hard scope failure; this file never supplies successful samples.
namespace ymcc::msi_sensor {
enum class Kind { Gyro, Accel };
struct Descriptor { std::string id; std::wstring devicePath; unsigned reportIntervalMs=0; };
struct Sample {
    Kind kind=Kind::Gyro; Descriptor descriptor;
    std::uint64_t generation=0,timestamp100ns=0,receiptTick=0;
    std::array<double,3> axes{}; std::string layout;
};
}
namespace ymcc::sensors::sensorapi {
inline unsigned fixtureCalls=0;
[[noreturn]] inline void unselected(){++fixtureCalls;throw std::logic_error("unselected-SensorAPI-private-reader-port");}
inline std::string sourceId(const std::string&){unselected();}
class PrivateReader {
public:
    struct Snapshot {
        bool inventoryObserved=false,pairSelected=false,retired=false;
        std::array<msi_sensor::Descriptor,2> descriptors{};
        std::uint64_t generation=0,queued=0,dropped=0;std::int64_t error=0;
    };
    bool start(const selection::PairChoice&,std::uint64_t){unselected();}
    Snapshot snapshot()const{unselected();}
    bool running()const{unselected();}
    std::vector<msi_sensor::Sample> take(){unselected();}
    void close(){unselected();}
};
}
