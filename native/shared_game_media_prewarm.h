#pragma once
#include "shared_game_media.h"
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
namespace ymcc::media {
// Read only bounded cached-file ranges. No media decoder, network or timer.
class CachePrewarmer {
public:
 struct Task{std::string key;fs::path path;};
 struct Stats{uint64_t bytes=0,completed=0,hits=0,cancelled=0;size_t queued=0;bool active=false;};
 static constexpr uint64_t headBytes=512*1024,tailBytes=256*1024,rememberMs=60000;
private:
 std::mutex mutex_;std::condition_variable changed_;std::deque<Task> queue_;
 std::map<std::string,uint64_t> recent_;std::thread thread_;std::function<bool()> foreground_;
 bool stopping_=false;uint64_t generation_=0;Stats stats_;
 bool stale(uint64_t generation){std::lock_guard lock(mutex_);return stopping_||generation!=generation_;}
 void loop(){
  SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_IDLE);
  for(;;){Task task;uint64_t generation=0;
   {std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return stopping_||!queue_.empty()&&!foreground_();});if(stopping_)return;
    task=std::move(queue_.front());queue_.pop_front();const auto found=recent_.find(task.key);
    if(found!=recent_.end()&&GetTickCount64()-found->second<rememberMs){++stats_.hits;continue;}
    generation=generation_;stats_.active=true;
   }
   bool complete=true,preempted=false;uint64_t bytes=0;
   try{
    std::error_code error;const auto size=fs::file_size(task.path,error);if(error||!size||size>300ull*1024*1024)throw std::runtime_error("prewarm-file-unavailable");
    std::ifstream file(task.path,std::ios::binary);if(!file)throw std::runtime_error("prewarm-file-unavailable");std::array<char,65536> chunk;
    const uint64_t first=(std::min)(size,headBytes),last=size>first?(std::min)(size-first,tailBytes):0;
    for(const auto& range:std::array<std::pair<uint64_t,uint64_t>,2>{{{0,first},{size-last,last}}}){
     if(!range.second)continue;file.clear();file.seekg(static_cast<std::streamoff>(range.first));uint64_t left=range.second;
     while(left){if(stale(generation)){complete=false;break;}if(foreground_()){complete=false;preempted=true;break;}
      const auto wanted=(std::min)(left,static_cast<uint64_t>(chunk.size()));file.read(chunk.data(),static_cast<std::streamsize>(wanted));const auto got=static_cast<uint64_t>(file.gcount());bytes+=got;if(got!=wanted){complete=false;break;}left-=got;
     }if(!complete)break;
    }
   }catch(...){complete=false;}
   {std::lock_guard lock(mutex_);stats_.active=false;stats_.bytes+=bytes;
    if(!stopping_&&generation==generation_){if(complete){recent_[task.key]=GetTickCount64();++stats_.completed;while(recent_.size()>16){auto oldest=std::min_element(recent_.begin(),recent_.end(),[](const auto&a,const auto&b){return a.second<b.second;});recent_.erase(oldest);}}else if(preempted)queue_.push_front(std::move(task));else ++stats_.cancelled;}
    else ++stats_.cancelled;
   }
  }
 }
public:
 explicit CachePrewarmer(std::function<bool()> foreground=[] {return false;}):foreground_(std::move(foreground)){}
 ~CachePrewarmer(){stop();}
 void replace(std::vector<Task> tasks){
  {std::lock_guard lock(mutex_);if(stopping_)return;++generation_;queue_.clear();
   if(thread_.joinable())CancelSynchronousIo(thread_.native_handle());
   std::set<std::string> seen;for(auto& task:tasks){if(queue_.size()>=4)break;if(task.key.empty()||!seen.insert(task.key).second)continue;
    const auto root=task.path.root_path();const auto drive=GetDriveTypeW(root.c_str());if(drive==DRIVE_REMOTE||drive==DRIVE_UNKNOWN||drive==DRIVE_NO_ROOT_DIR)continue;queue_.push_back(std::move(task));}
   if(!queue_.empty()&&!thread_.joinable())thread_=std::thread([this]{loop();});
  }changed_.notify_all();
 }
 void foregroundChanged(){std::lock_guard lock(mutex_);changed_.notify_all();}
 Stats stats(){std::lock_guard lock(mutex_);auto value=stats_;value.queued=queue_.size();return value;}
 void stop(){
  {std::lock_guard lock(mutex_);if(stopping_)return;stopping_=true;++generation_;queue_.clear();if(thread_.joinable())CancelSynchronousIo(thread_.native_handle());}
  changed_.notify_all();if(thread_.joinable())thread_.join();
 }
};
} // namespace ymcc::media
