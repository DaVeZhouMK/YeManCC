#pragma once
// Optional value reuse inside the existing native collector only. No IO,
// worker, timer, producer timestamp, global identity cache or shared mutation.
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>
#include <type_traits>
namespace ymcc::monitorparse {
template<class Packet> class Reuse {
    static_assert(std::is_nothrow_copy_constructible_v<Packet>&&std::is_nothrow_copy_assignable_v<Packet>,
        "Source reuse requires a bounded nonallocating packet copy");
public:
    using Key=std::array<std::uint64_t,11>; // Full stamp plus observed period.
    static constexpr std::size_t byteLimit=1024*1024;
    static constexpr std::uint64_t minimumPeriodMs=1500,maximumPeriodMs=60000;
private:
    std::vector<unsigned char> bytes_;
    std::optional<Packet> packet_;
    Key key_{};
    std::size_t rowBytes_=0,sensorBytes_=0;
    std::uint64_t generation_=0;
    std::uint32_t source_=UINT32_MAX;
    bool detailed_=false,staged_=false;
public:
    Reuse()=default;
    Reuse(const Reuse&)=delete;
    Reuse& operator=(const Reuse&)=delete;
    void reset()noexcept{
        packet_.reset();std::vector<unsigned char>().swap(bytes_);
        rowBytes_=sensorBytes_=0;staged_=false;
    }
    void begin(std::uint64_t generation)noexcept{
        if(generation!=generation_){reset();generation_=generation;source_=UINT32_MAX;}
    }
    void bindSource(std::uint32_t source)noexcept{
        if(source!=source_){reset();source_=source;}
    }
    static bool supports(std::uint64_t period,std::size_t rows,std::size_t sensors)noexcept{
        return period>=minimumPeriodMs&&period<=maximumPeriodMs&&rows>0&&sensors>0&&
            rows<=byteLimit&&sensors<=byteLimit-rows;
    }
    bool sensorsMatch(const void* sensors,std::size_t size)const noexcept{
        return staged_&&sensors&&size==sensorBytes_&&bytes_.size()==rowBytes_+sensorBytes_&&
            std::memcmp(bytes_.data()+rowBytes_,sensors,size)==0;
    }
    bool matches(const Key& key,bool detailed,const void* rows,std::size_t rowSize,
                 const void* sensors,std::size_t sensorSize)const noexcept{
        return packet_&&key_==key&&detailed_==detailed&&rows&&rowSize==rowBytes_&&
            sensorsMatch(sensors,sensorSize)&&std::memcmp(bytes_.data(),rows,rowSize)==0;
    }
    bool restore(Packet& out)const noexcept{
        if(!packet_)return false;
        try{out=*packet_;return true;}catch(...){return false;}
    }
    bool stage(const Key& key,bool detailed,const void* rows,std::size_t rowSize,
               const void* sensors,std::size_t sensorSize)noexcept{
        if(!rows||!sensors||!supports(key.back(),rowSize,sensorSize)){reset();return false;}
        try{
            packet_.reset();staged_=false;const auto total=rowSize+sensorSize;
            if(total>bytes_.capacity()){
                std::vector<unsigned char> next(total);
                if(next.capacity()>byteLimit){reset();return false;}
                bytes_.swap(next);
            }else bytes_.resize(total);
            std::memcpy(bytes_.data(),rows,rowSize);std::memcpy(bytes_.data()+rowSize,sensors,sensorSize);
            key_=key;detailed_=detailed;rowBytes_=rowSize;sensorBytes_=sensorSize;staged_=true;return true;
        }catch(...){reset();return false;} // Reuse failure cannot break collection.
    }
    // Borrowed only by the sole parser until this staged parse finishes.
    // Neither commit nor packet restore changes the owned byte buffer.
    const unsigned char* stagedRows()const noexcept{
        return staged_&&bytes_.size()==rowBytes_+sensorBytes_?bytes_.data():nullptr;
    }
    void commit(const Packet& packet)noexcept{
        try{if(staged_)packet_=packet;}catch(...){reset();}
    }
    std::size_t retainedBytes()const noexcept{return bytes_.capacity();}
    bool hasPacket()const noexcept{return packet_.has_value();}
};
}
