#pragma once
#include "windows_motion_submission.h"
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
namespace ymcc::sensors::processing {
struct Recipe {std::string deviceClass,selector,gyro,accel,digest;bool bound=false;};
inline bool sameId(const std::string& a,const std::string& b) noexcept {
 if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i){const auto upper=[](unsigned char c){return c>='a'&&c<='z'?c-'a'+'A':c;};if(upper(a[i])!=upper(b[i]))return false;}return true;
}
struct Metadata {std::string gyroId,accelId,gyroApi,accelApi;bool gyroLegacy=false,accelLegacy=false;};
struct Identity {
 windows_motion::Stamp source;Recipe recipe;Metadata metadata;
 std::string profile,key;std::uint64_t power=0,epoch=0,captureRequest=0,choiceRequest=0;bool privateSource=false;
};
struct FrameProof {std::uint64_t processedTick=0,gyroSequence=0,accelSequence=0,gyroReceipt=0,accelReceipt=0,gyroTimestamp=0,accelTimestamp=0;std::shared_ptr<const windows_motion::PublicationFence> publication;};
struct Receipt {Identity identity;FrameProof frame;};
// Readers see values and revocation tokens only, never a GM/native sensor handle.
class ReceiptCache final {
 const Identity identity_;mutable std::mutex mutex_;std::optional<FrameProof> frame_;
public:
 explicit ReceiptCache(Identity identity):identity_(std::move(identity)){}
 void publish(const FrameProof& frame){std::lock_guard<std::mutex> lock(mutex_);frame_=frame;}
 std::optional<Receipt> read(std::uint64_t now,std::uint64_t power) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if(!frame_||!power||identity_.power!=power||!windows_motion::current(identity_.source)||
     !frame_->processedTick||frame_->processedTick>now||now-frame_->processedTick>1500||
     !frame_->gyroSequence||!frame_->accelSequence||!frame_->gyroReceipt||!frame_->accelReceipt||
     frame_->gyroReceipt>now||frame_->accelReceipt>now||!frame_->gyroTimestamp||!frame_->accelTimestamp)return std::nullopt;
  // Existing default captures keep their established latest-value semantics.
  if(identity_.privateSource&&(now-frame_->gyroReceipt>1500||now-frame_->accelReceipt>1500))return std::nullopt;
  return Receipt{identity_,*frame_};
 }
};
struct Owner {
 Identity identity;windows_motion::Stamp raw;
 std::shared_ptr<std::atomic<bool>> lease;std::shared_ptr<ReceiptCache> receipt;
 void* handle=nullptr;std::chrono::steady_clock::time_point session{};
 FrameProof pending;bool hasPending=false;
 bool valid=false,everProcessed=false,mustReset=false;
};
} // namespace ymcc::sensors::processing
