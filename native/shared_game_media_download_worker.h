#pragma once
#include "shared_game_media_download_queue.h"
#include "shared_game_media_identity_worker.h"
#include <regex>
#include <cmath>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <shellapi.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace ymcc::media::downloads {
struct InternetHandle {
  HINTERNET value = nullptr;
  explicit InternetHandle(HINTERNET h) : value(h) {
    if (!h)
      throw std::runtime_error("media-network-open-failed");
  }
  ~InternetHandle() {
    if (value)
      WinHttpCloseHandle(value);
  }
  InternetHandle(const InternetHandle &) = delete;
};
inline std::wstring sourceUrl(const std::string &raw) {
  auto url = wide(raw);
  if (raw.size() > 8192 || raw.find_first_of("\r\n\\") != std::string::npos)
    throw std::runtime_error("media-source-denied");
  URL_COMPONENTS p{};
  p.dwStructSize = sizeof p;
  p.dwHostNameLength = p.dwUserNameLength = p.dwPasswordLength = DWORD(-1);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &p) ||
      p.nScheme != INTERNET_SCHEME_HTTPS || p.nPort != 443 ||
      p.dwUserNameLength || p.dwPasswordLength)
    throw std::runtime_error("media-source-denied");
  std::wstring host(p.lpszHostName, p.dwHostNameLength);
  for (auto &c : host)
    c = static_cast<wchar_t>(towlower(c));
  bool good = host == L"shared.cdn.queniuqe.com";
  for (const auto *domain :
       {L"steamstatic.com", L"steampowered.com", L"steamusercontent.com",
        L"akamaihd.net", L"eccdnx.com"}) {
    std::wstring d = domain;
    good = good || host == d ||
           (host.size() > d.size() && host.ends_with(L"." + d));
  }
  if (!good)
    throw std::runtime_error("media-source-denied");
  return url;
}
// Modern Valve trailer paths include the owning AppID. Check before every
// request/redirect, including HLS child playlists and segments. Legacy movie-ID
// paths keep their original metadata validation because they do not encode AppID.
inline bool sourceGameMatches(std::wstring path,const std::string &key) {
  if(!key.starts_with("steam-")) return true;
  std::wstring decoded;decoded.reserve(path.size());
  const auto hex=[](wchar_t c)->int {if(c>=L'0'&&c<=L'9')return c-L'0';if(c>=L'a'&&c<=L'f')return c-L'a'+10;if(c>=L'A'&&c<=L'F')return c-L'A'+10;return -1;};
  for(size_t n=0;n<path.size();++n) {
    auto c=path[n];if(c==L'%'&&n+2<path.size()) {const auto a=hex(path[n+1]),b=hex(path[n+2]);if(a>=0&&b>=0){c=wchar_t(a*16+b);n+=2;}}
    decoded.push_back(c==L'\\'?L'/':c);
  }
  std::vector<std::wstring> pieces;std::wistringstream stream(decoded);std::wstring piece;
  while(std::getline(stream,piece,L'/')) {
    if(piece.empty()||piece==L".")continue;
    if(piece==L"..") {if(!pieces.empty())pieces.pop_back();} else pieces.push_back(piece);
  }
  if(pieces.empty())return true;
  auto prefix=pieces[0];for(auto &c:prefix)c=static_cast<wchar_t>(towlower(c));
  return prefix!=L"store_trailers"||(pieces.size()>1&&utf8(pieces[1])==key.substr(6));
}
inline std::string combineUrl(const std::string &base,
                              const std::string &relative) {
  if (relative.starts_with("https://")) {
    sourceUrl(relative);
    return relative;
  }
  if (relative.starts_with("//")) {
    auto result = "https:" + relative;
    sourceUrl(result);
    return result;
  }
  const auto scheme = base.find("://"),
             authorityEnd = base.find('/', scheme + 3);
  const auto origin =
      authorityEnd == std::string::npos ? base : base.substr(0, authorityEnd);
  auto clean = base.substr(0, base.find_first_of("?#"));
  auto result = relative.starts_with('/')
                    ? origin + relative
                    : clean.substr(0, clean.rfind('/') + 1) + relative;
  sourceUrl(result);
  return result;
}
inline std::string decodeMediaUrl(std::string url) {
  for (const auto &pair : {std::pair{std::string{"\\/"}, std::string{"/"}},
                           {std::string{"\\u0026"}, std::string{"&"}},
                           {std::string{"&amp;"}, std::string{"&"}},
                           {std::string{"&quot;"}, std::string{"\""}},
                           {std::string{"&#39;"}, std::string{"'"}}}) {
    size_t p = 0;
    while ((p = url.find(pair.first, p)) != std::string::npos) {
      url.replace(p, pair.first.size(), pair.second);
      p += pair.second.size();
    }
  }
  return url;
}
// Whole-page fallbacks can contain recommendations or short description loops.
// Only a formal video path explicitly bound to this AppID is admissible here;
// metadata-provided legacy movie IDs keep the original trusted metadata path.
inline bool fallbackVideoMatches(const std::string& raw,int64_t appId) {
  if(appId<=0||appId>INT32_MAX)return false;
  try {
    const auto url=sourceUrl(raw);URL_COMPONENTS parts{};parts.dwStructSize=sizeof parts;parts.dwUrlPathLength=DWORD(-1);
    if(!WinHttpCrackUrl(url.c_str(),0,0,&parts)||!parts.lpszUrlPath)return false;
    std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength),decoded;
    const auto hex=[](wchar_t c)->int{if(c>=L'0'&&c<=L'9')return c-L'0';if(c>=L'a'&&c<=L'f')return c-L'a'+10;if(c>=L'A'&&c<=L'F')return c-L'A'+10;return -1;};
    for(size_t n=0;n<path.size();++n){auto c=path[n];if(c==L'%'&&n+2<path.size()){const auto a=hex(path[n+1]),b=hex(path[n+2]);if(a>=0&&b>=0){c=wchar_t(a*16+b);n+=2;}}decoded.push_back(static_cast<wchar_t>(towlower(c)));}
    if(decoded.find(L"\\")!=std::wstring::npos||decoded.find(L"/../")!=std::wstring::npos||decoded.find(L"/./")!=std::wstring::npos||decoded.find(L"/extras/")!=std::wstring::npos||decoded.find(L"microtrailer")!=std::wstring::npos)return false;
    const auto owner=std::to_wstring(appId)+L"/";
    for(const auto*prefix:{L"/store_trailers/",L"/store_item_assets/steam/apps/",L"/steam/apps/"})if(decoded.starts_with(std::wstring(prefix)+owner))return true;
  }catch(const std::exception&){}
  return false;
}
inline std::vector<std::string> fallbackPageVideos(const std::string& html,int64_t appId) {
  const auto page=decodeMediaUrl(html);std::vector<std::string> result;
  static const std::regex pattern(R"(https://[^\s"'<>\\]+\.(?:webm|mp4|m3u8)(?:\?[^\s"'<>\\]*)?)");
  for(std::sregex_iterator i(page.begin(),page.end(),pattern),end;i!=end&&result.size()<16;++i){const auto url=i->str();if(fallbackVideoMatches(url,appId)&&std::find(result.begin(),result.end(),url)==result.end())result.push_back(url);}
  return result;
}
struct HlsVariant {
  std::string url;
  int height=0;
  uint64_t bandwidth=0;
};
struct HlsPlan {
  std::string variant;
  std::vector<std::string> segments;
  int height=0;
  uint64_t bandwidth=0;
  double duration=0;
};
// A zero ceiling preserves the preferred official source, including a lone
// higher-resolution source. Explicit ceilings are used only after a size failure.
inline HlsPlan hlsPlan(const std::string &text, const std::string &base, int ceiling=0,int preferredHeight=720) {
  if (text.size() > 1024 * 1024 ||
      text.find("#EXTM3U") != text.find_first_not_of(" \r\n\t"))
    throw std::runtime_error("media-playlist-invalid");
  std::vector<std::string> lines;
  std::istringstream stream(text);
  for (std::string line; std::getline(stream, line);) {
    const auto a = line.find_first_not_of(" \t\r"),
               b = line.find_last_not_of(" \t\r");
    if (a != std::string::npos)
      lines.push_back(line.substr(a, b - a + 1));
  }
  std::vector<HlsVariant> variants;
  std::smatch match;
  for (size_t i = 0; i + 1 < lines.size(); ++i)
    if (lines[i].starts_with("#EXT-X-STREAM-INF:") &&
        !lines[i + 1].starts_with('#')) {
      HlsVariant v;
      v.url = combineUrl(base, lines[i + 1]);
      if (std::regex_search(lines[i], match,
                            std::regex("RESOLUTION=[0-9]+x([0-9]+)")))
        v.height = std::stoi(match[1]);
      if (std::regex_search(lines[i], match, std::regex("(?:[:,])BANDWIDTH=([0-9]+)")))
        v.bandwidth = std::stoull(match[1]);
      variants.push_back(v);
    }
  if (!variants.empty()) {
    if(ceiling>0) {
      std::erase_if(variants,[&](const auto &v){return v.height<=0||v.height>ceiling;});
      if(variants.empty()) throw std::runtime_error("media-size-invalid");
    }
    std::stable_sort(variants.begin(),variants.end(),[preferredHeight](const auto& a,const auto& b){
      const auto rank=[preferredHeight](const auto& v){const int group=v.height==preferredHeight?0:preferredHeight>=1080&&v.height==720?1:preferredHeight>=720&&v.height==480?2:v.height>0&&v.height<preferredHeight?3:v.height>0?4:5;
        return std::tuple{group,group==3?-v.height:v.height,-static_cast<int64_t>((std::min)(v.bandwidth,uint64_t{INT64_MAX}))};};return rank(a)<rank(b);
    });
    return {variants.front().url, {},variants.front().height,variants.front().bandwidth};
  }
  std::string init;
  std::vector<std::string> fragments;
  bool ended = false;
  double duration=0;
  for (const auto &line : lines) {
    if(line.starts_with("#EXTINF:")) {
      const auto seconds=std::stod(line.substr(8));
      if(!std::isfinite(seconds)||seconds<=0||seconds>86400) throw std::runtime_error("media-playlist-invalid");
      duration+=seconds;
    }
    if (line == "#EXT-X-ENDLIST")
      ended = true;
    if (line == "#EXT-X-DISCONTINUITY" ||
        line.starts_with("#EXT-X-BYTERANGE:") ||
        (line.starts_with("#EXT-X-KEY:") &&
         !std::regex_search(line, std::regex("METHOD=NONE(,|$)"))))
      throw std::runtime_error("media-playlist-unsupported");
    if (line.starts_with("#EXT-X-MAP:")) {
      if (line.find("BYTERANGE=") != std::string::npos ||
          !std::regex_search(line, match, std::regex("URI=\"([^\"]+)\"")))
        throw std::runtime_error("media-playlist-unsupported");
      const auto next = combineUrl(base, match[1]);
      if (!init.empty() && init != next)
        throw std::runtime_error("media-playlist-unsupported");
      init = next;
    }
    if (!line.starts_with('#'))
      fragments.push_back(combineUrl(base, line));
  }
  if (!ended || init.empty() || fragments.empty() || fragments.size() > 600)
    throw std::runtime_error("media-playlist-incomplete");
  fragments.insert(fragments.begin(), init);
  return {{}, fragments,0,0,duration};
}
// BANDWIDTH is the official peak bitrate, so this conservatively avoids a
// large transfer before falling back. Unknown duration/bitrate keeps byte checks.
inline bool hlsExceedsLimit(const HlsPlan &plan,uint64_t bandwidth,uint64_t limit) {
  return plan.duration>0&&bandwidth>0&&
    static_cast<long double>(plan.duration)*bandwidth/8*1.02L+1024*1024>limit;
}
inline int lowerHlsHeight(int height) { return height>720?720:height>480?480:0; }
// Structural checks only: seek across declared boxes/elements without decoding
// frames or reading the full payload. HTTP close-delimited success alone cannot
// establish that the last MP4 box or WebM block arrived in full.
inline void validateDownloadedVideo(const fs::path &path) {
  const auto kind = mediaKind(path);
  if (kind != "mp4" && kind != "webm")
    throw std::runtime_error("media-slot-container-mismatch");
  const auto size = fs::file_size(path);
  std::ifstream input(path, std::ios::binary);
  auto readAt = [&](uint64_t offset, unsigned char *data, size_t count) {
    if (offset > size || count > size - offset)
      throw std::runtime_error("media-video-truncated");
    input.seekg(static_cast<std::streamoff>(offset));
    input.read(reinterpret_cast<char *>(data), count);
    if (!input) throw std::runtime_error("media-video-truncated");
  };
  if (kind == "mp4") {
    bool metadata = false, payload = false;
    uint64_t offset = 0;
    size_t boxes = 0;
    while (offset < size) {
      if (++boxes > 16384) throw std::runtime_error("media-video-structure-limit");
      unsigned char bytes[16]{};
      readAt(offset, bytes, 8);
      uint64_t length = (uint64_t(bytes[0]) << 24) | (uint64_t(bytes[1]) << 16) |
                        (uint64_t(bytes[2]) << 8) | bytes[3];
      uint64_t header = 8;
      if (length == 1) {
        readAt(offset + 8, bytes + 8, 8);
        header = 16; length = 0;
        for (size_t n = 8; n < 16; ++n) length = (length << 8) | bytes[n];
      } else if (length == 0) length = size - offset;
      if (length < header || length > size - offset)
        throw std::runtime_error("media-video-truncated");
      const std::string type(reinterpret_cast<char *>(bytes + 4), 4);
      metadata = metadata || type == "moov";
      payload = payload || type == "mdat" && length > header;
      offset += length;
    }
    if (!metadata || !payload) throw std::runtime_error("media-video-incomplete");
    return;
  }
  struct Element { uint64_t id, body, end; bool unknown; };
  size_t elements = 0;
  auto element = [&](uint64_t offset, uint64_t limit) -> Element {
    if (++elements > 65536) throw std::runtime_error("media-video-structure-limit");
    auto vint = [&](bool id) {
      unsigned char first{}; readAt(offset++, &first, 1);
      unsigned char mask = 0x80; size_t count = 1;
      while (mask && !(first & mask)) { mask >>= 1; ++count; }
      if (!mask || count > (id ? 4u : 8u))
        throw std::runtime_error("media-video-structure-invalid");
      uint64_t value = id ? first : first & (mask - 1);
      for (size_t n = 1; n < count; ++n) {
        unsigned char byte{}; readAt(offset++, &byte, 1); value = (value << 8) | byte;
      }
      return std::pair{value, !id && value == ((uint64_t{1} << (count * 7)) - 1)};
    };
    const auto id = vint(true).first;
    const auto length = vint(false);
    if (offset > limit || !length.second && length.first > limit - offset)
      throw std::runtime_error("media-video-truncated");
    return {id, offset, length.second ? limit : offset + length.first, length.second};
  };
  auto header = element(0, size);
  if (header.id != 0x1a45dfa3 || header.unknown)
    throw std::runtime_error("media-video-structure-invalid");
  auto segment = element(header.end, size);
  // Void/CRC before Segment are legal; keep the search bounded.
  for (size_t n = 0; segment.id != 0x18538067 && n < 8; ++n) {
    if (segment.unknown || (segment.id != 0xec && segment.id != 0xbf))
      throw std::runtime_error("media-video-structure-invalid");
    segment = element(segment.end, size);
  }
  if (segment.id != 0x18538067 || segment.end != size)
    throw std::runtime_error("media-video-incomplete");
  bool info = false, tracks = false, cluster = false;
  uint64_t offset = segment.body;
  auto segmentLevel = [](uint64_t id) {
    return id == 0x1f43b675 || id == 0x114d9b74 || id == 0x1549a966 ||
           id == 0x1654ae6b || id == 0x1c53bb6b || id == 0x1941a469 ||
           id == 0x1043a770 || id == 0x1254c367;
  };
  while (offset < segment.end) {
    auto child = element(offset, segment.end);
    info = info || child.id == 0x1549a966;
    tracks = tracks || child.id == 0x1654ae6b;
    cluster = cluster || child.id == 0x1f43b675;
    if (!child.unknown) { offset = child.end; continue; }
    if (child.id != 0x1f43b675)
      throw std::runtime_error("media-video-structure-unsupported");
    offset = child.body;
    while (offset < segment.end) {
      auto block = element(offset, segment.end);
      if (segmentLevel(block.id)) break;
      if (block.unknown) throw std::runtime_error("media-video-structure-unsupported");
      offset = block.end;
    }
  }
  if (!info || !tracks || !cluster) throw std::runtime_error("media-video-incomplete");
}

struct WorkerContext {
  std::mutex gate, budget,waitGate;std::condition_variable changed;uint64_t revision=0;
  void wakeLanes(){std::lock_guard lock(waitGate);++revision;changed.notify_all();}
  uint64_t checkedAt=0, diskAt=0, sinceDisk=0, used=0, available=0;
  std::atomic<bool> cancelled{false};
  std::string failure;
  static constexpr uint64_t stagingLimit=6ull*1024*1024*1024;
  explicit WorkerContext(const fs::path &root) {
    std::error_code ec;size_t count=0;
    for(fs::directory_iterator it(root/L"cache"/L"jobs"/L"game-media",ec),end;
        !ec&&it!=end&&count<8192;it.increment(ec),++count) {
      if(it->is_symlink(ec)||!it->is_regular_file(ec)||it->path().extension()!=L".part") continue;
      const auto bytes=it->file_size(ec);if(!ec) used+=bytes;ec.clear();
    }
  }
  void trim(const fs::path &root) {
    struct Item {fs::path file;uint64_t bytes;fs::file_time_type at;};std::vector<Item> old;
    std::error_code ec;size_t count=0;const auto folder=root/L"cache"/L"jobs"/L"game-media";
    const auto target=stagingLimit-2*capacity::maxAssetBytes;
    if(used<=target) return;
    for(fs::directory_iterator it(folder,ec),end;!ec&&it!=end&&count<8192;it.increment(ec),++count) {
      const auto name=it->path().filename().wstring();
      if(name.size()!=37||name.substr(32)!=L".part"||name.substr(0,32).find_first_not_of(L"0123456789abcdef")!=std::wstring::npos||
          it->is_symlink(ec)||!it->is_regular_file(ec)) continue;
      try {
        auto metadata=it->path();metadata.replace_extension(L".resume.json");
        const auto info=read(metadata);if(info.value("resumeProtocol",0)!=1||stringField(info,"identity").empty()) continue;
        old.push_back({it->path(),it->file_size(),it->last_write_time()});
      }catch(...){}
    }
    std::sort(old.begin(),old.end(),[](const auto&a,const auto&b){return a.at<b.at;});
    for(const auto &item:old) {
      if(used<=target) break;ec.clear();
      if(fs::remove(item.file,ec)&&!ec) {used=item.bytes>used?0:used-item.bytes;auto metadata=item.file;metadata.replace_extension(L".resume.json");fs::remove(metadata,ec);}
    }
  }
  void reserve(const fs::path &root,uint64_t bytes) {
    std::lock_guard lock(budget);
    if(bytes>stagingLimit||used>stagingLimit-bytes) throw std::runtime_error("media-staging-budget-full");
    const auto tick=GetTickCount64();
    if(!diskAt||tick-diskAt>=1000) {
      std::error_code ec;const auto space=fs::space(root,ec);
      if(ec) throw std::runtime_error("media-storage-unavailable");
      available=space.available;sinceDisk=0;diskAt=tick;
    }
    if(available<capacity::diskReserveBytes||sinceDisk>available-capacity::diskReserveBytes||
        bytes>available-capacity::diskReserveBytes-sinceDisk) throw std::runtime_error("media-disk-space-low");
    used+=bytes;sinceDisk+=bytes;
  }
  void release(uint64_t bytes) {std::lock_guard lock(budget);used=bytes>used?0:used-bytes;}
};

class Worker {
  Queue queue_;
  IdentityPorts identity_;
  Limits limits_;
  std::string token_;
  HANDLE parent_;
  std::shared_ptr<WorkerContext> context_;
  std::string key_;
  std::unique_ptr<InternetHandle> session_;
  uint64_t started_ = GetTickCount64(), taskStarted_ = 0, taskProgressAt_ = 0, lastReport_ = 0,
           lastCheck_ = 0;
  uint64_t bytes_ = 0, taskBudget_ = automaticAttemptMs;
  std::string lastPhase_;
  void check() {
    const auto tick=GetTickCount64();
    if(context_->cancelled.load()||WaitForSingleObject(parent_,0)!=WAIT_TIMEOUT) throw std::runtime_error("media-download-cancelled");
    if(taskStarted_&&tick-taskStarted_>=taskBudget_)throw std::runtime_error("media-task-time-budget");
    if(tick-started_>=limits_.session||(taskStarted_&&tick-(std::max)(taskStarted_,taskProgressAt_)>=limits_.task)) throw std::runtime_error("media-task-timeout");
    std::lock_guard lock(context_->gate);
    if(tick-context_->checkedAt<1000) return;
    context_->checkedAt=tick;
    if(!queue_.allowed(token_)) throw std::runtime_error("media-download-cancelled");
    SYSTEM_POWER_STATUS power{};
    if(!GetSystemPowerStatus(&power)||power.ACLineStatus!=1||power.SystemStatusFlag==1) throw std::runtime_error("media-download-power-paused");
  }
  void progress(const std::string &phase, uint64_t bytes, uint64_t total,
                bool force = false) {
    if (GetTickCount64() - lastReport_ < 1000 &&
        (!force || phase == lastPhase_))
      return;
    check();
    lastReport_ = GetTickCount64();
    lastPhase_ = phase;
    if (!queue_.progress(token_, key_, phase, bytes, total))
      throw std::runtime_error("media-download-cancelled");
  }
  struct Reply {
    std::unique_ptr<InternetHandle> connection, request;
    std::string source;
    DWORD status=0;
    std::string header(DWORD field) const {
      wchar_t value[8192]{};DWORD size=sizeof value;
      if(!WinHttpQueryHeaders(request->value,field,WINHTTP_HEADER_NAME_BY_INDEX,value,&size,WINHTTP_NO_HEADER_INDEX)) return {};
      return utf8(value);
    }
  };
  std::unique_ptr<Reply> open(const std::string &source,const std::wstring &headers={}) {
    check();
    if(!session_) {
      session_=std::make_unique<InternetHandle>(WinHttpOpen(L"YeManCC Shared Wallpaper/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
      if(!WinHttpSetTimeouts(session_->value,10000,15000,15000,15000)) throw std::runtime_error("media-network-timeouts-unavailable");
    }
    std::string current=source;
    for(int redirects=0;redirects<4;++redirects) {
      const auto url=sourceUrl(current);URL_COMPONENTS p{};p.dwStructSize=sizeof p;
      p.dwHostNameLength=p.dwUrlPathLength=p.dwExtraInfoLength=DWORD(-1);
      if(!WinHttpCrackUrl(url.c_str(),0,0,&p)) throw std::runtime_error("media-source-denied");
      std::wstring host(p.lpszHostName,p.dwHostNameLength),path(p.lpszUrlPath,p.dwUrlPathLength);
      if(!sourceGameMatches(path,key_)) throw std::runtime_error("media-source-identity-mismatch");
      if(p.dwExtraInfoLength) path.append(p.lpszExtraInfo,p.dwExtraInfoLength);
      auto reply=std::make_unique<Reply>();reply->source=current;
      reply->connection=std::make_unique<InternetHandle>(WinHttpConnect(session_->value,host.c_str(),p.nPort,0));
      reply->request=std::make_unique<InternetHandle>(WinHttpOpenRequest(reply->connection->value,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
      DWORD responseTimeout=15000,redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
      if(!WinHttpSetTimeouts(reply->request->value,10000,15000,15000,15000)||
          !WinHttpSetOption(reply->request->value,WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT,&responseTimeout,sizeof responseTimeout)) throw std::runtime_error("media-network-timeouts-unavailable");
      if(!WinHttpSetOption(reply->request->value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof redirect)) throw std::runtime_error("media-redirect-policy-failed");
      if(!headers.empty()&&!WinHttpAddRequestHeaders(reply->request->value,headers.c_str(),DWORD(-1),WINHTTP_ADDREQ_FLAG_ADD|WINHTTP_ADDREQ_FLAG_REPLACE)) throw std::runtime_error("media-network-headers-failed");
      if(!WinHttpSendRequest(reply->request->value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(reply->request->value,nullptr)) throw std::runtime_error("media-network-request-failed");
      DWORD size=sizeof reply->status;
      if(!WinHttpQueryHeaders(reply->request->value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&reply->status,&size,WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("media-http-response-invalid");
      if(reply->status>=300&&reply->status<400) {
        const auto location=reply->header(WINHTTP_QUERY_LOCATION);if(location.empty()) throw std::runtime_error("media-redirect-invalid");
        current=combineUrl(current,location);continue;
      }
      return reply;
    }
    throw std::runtime_error("media-too-many-redirects");
  }
  static uint64_t number(const std::string &value) {
    if(value.empty()||value.size()>20||value.find_first_not_of("0123456789")!=std::string::npos) throw std::runtime_error("media-http-length-invalid");
    try{return std::stoull(value);}catch(...){throw std::runtime_error("media-http-length-invalid");}
  }
  std::string request(const std::string &source,uint64_t cap=2*1024*1024) {
    auto reply=open(source);
    if(reply->status!=200) throw std::runtime_error("media-http-"+std::to_string(reply->status));
    const auto length=reply->header(WINHTTP_QUERY_CONTENT_LENGTH);const auto expected=length.empty()?0:number(length);
    if(expected>cap) throw std::runtime_error("media-size-invalid");
    std::string body;char buffer[64*1024];
    for(;;) {
      check();DWORD count=0;
      if(!WinHttpReadData(reply->request->value,buffer,sizeof buffer,&count)) throw std::runtime_error("media-network-read-failed");
      if(!count) break;
      if(!capacity::downloadFits(body.size(),count,cap)) throw std::runtime_error("media-size-invalid");
      body.append(buffer,count);progress("resolving",body.size(),expected);
    }
    if(expected&&body.size()!=expected) throw std::runtime_error("media-download-truncated");
    if(body.empty()) throw std::runtime_error("media-download-empty");return body;
  }
  static fs::path resumeMetadata(const fs::path &part) {
    auto path=part;path.replace_extension(L".resume.json");return path;
  }
  static bool usableValidator(const std::string &value) {
    return !value.empty()&&value.size()<=512&&value.find_first_of("\r\n") == std::string::npos;
  }
  static bool strongModifiedDate(const std::string &modified,const std::string &date) {
    if(!usableValidator(modified)||!usableValidator(date)) return false;
    SYSTEMTIME m{},d{};FILETIME mf{},df{};
    if(!WinHttpTimeToSystemTime(wide(modified).c_str(),&m)||!WinHttpTimeToSystemTime(wide(date).c_str(),&d)||
        !SystemTimeToFileTime(&m,&mf)||!SystemTimeToFileTime(&d,&df)) return false;
    ULARGE_INTEGER mt{},dt{};mt.LowPart=mf.dwLowDateTime;mt.HighPart=mf.dwHighDateTime;
    dt.LowPart=df.dwLowDateTime;dt.HighPart=df.dwHighDateTime;
    // RFC 9110: a cached Last-Modified needs at least a 60-second Date gap.
    return dt.QuadPart>=mt.QuadPart&&dt.QuadPart-mt.QuadPart>=600000000ull;
  }
  void discard(const fs::path &part) {
    std::error_code ec;const auto bytes=fs::file_size(part,ec);ec.clear();
    if(fs::remove(part,ec)&&!ec) context_->release(bytes);
    ec.clear();fs::remove(resumeMetadata(part),ec);
  }
  bool retained(const fs::path &part,const std::string &identity) {
    try {
      const auto metadata=read(resumeMetadata(part));std::error_code ec;const auto bytes=fs::file_size(part,ec);
      const auto expected=metadata.find("expectedSize");
      const auto type=stringField(metadata,"validatorType");
      const auto tag=stringField(metadata,"validator");
      if(expected==metadata.end()||!expected->is_number_integer()||expected->get<uint64_t>()>capacity::maxAssetBytes||
          !metadata.contains("complete")||!metadata["complete"].is_boolean()) return false;
      if(type!="etag"&&type!="last-modified") return false;
      if(type=="etag"&&(tag.size()<2||tag.front()!='"'||tag.back()!='"')) return false;
      if(type=="last-modified"&&!strongModifiedDate(tag,stringField(metadata,"validatorDate"))) return false;
      sourceUrl(stringField(metadata,"finalSource"));
      return !ec&&bytes>0&&bytes<=capacity::maxAssetBytes&&metadata.value("resumeProtocol",0)==1&&
        stringField(metadata,"identity")==identity&&usableValidator(tag);
    }catch(...){return false;}
  }
  fs::path downloadFile(const std::string &source,const std::string &binding,uint64_t cap,bool segmented) {
    const auto identity=binding+"\n"+source;
    const auto hex=[](std::string v){return std::string(16-v.size(),'0')+v;};
    const auto id=hex(digestKey(identity))+hex(digestKey("resume:"+identity));
    const auto folder=queue_.root()/L"cache"/L"jobs"/L"game-media";fs::create_directories(folder);
    const auto part=folder/wide(id+".part");const auto metadataPath=resumeMetadata(part);
    std::error_code ec;
    if(fs::is_symlink(fs::symlink_status(part,ec))) throw std::runtime_error("media-cache-file-unsafe");
    Json metadata;try{metadata=read(metadataPath);}catch(...){metadata=Json::object();}
    ec.clear();auto offset=fs::file_size(part,ec);if(ec) offset=0;
    if(!retained(part,identity)||offset>cap) {discard(part);metadata=Json::object();offset=0;}
    queue_.trackTemporary(token_,utf8(part.filename().wstring()),true);
    const auto before=bytes_;bool restarted=false,reported=false;
    try {
      for(int window=0;window<64;++window) {
        check();std::wstring headers=L"Accept-Encoding: identity\r\n";
        if(offset) headers+=L"Range: bytes="+std::to_wstring(offset)+L"-\r\nIf-Range: "+wide(stringField(metadata,"validator"))+L"\r\n";
        auto reply=open(source,headers);
        const auto etag=reply->header(WINHTTP_QUERY_ETAG),modified=reply->header(WINHTTP_QUERY_LAST_MODIFIED);
        const auto priorType=stringField(metadata,"validatorType");
        const auto sameValidator=usableValidator(stringField(metadata,"validator"))&&
          (priorType=="etag"?etag:modified)==stringField(metadata,"validator")&&
          (priorType!="last-modified"||etag.empty())&&reply->source==stringField(metadata,"finalSource");
        const auto encoded=reply->header(WINHTTP_QUERY_CONTENT_ENCODING);
        if(!encoded.empty()&&encoded!="identity") {
          if(offset&&!restarted){discard(part);metadata=Json::object();offset=0;restarted=true;continue;}
          throw std::runtime_error("media-encoding-unsupported");
        }
        const auto rawLength=reply->header(WINHTTP_QUERY_CONTENT_LENGTH);
        uint64_t expected=rawLength.empty()?0:number(rawLength),total=expected;
        if(reply->status==416&&offset) {
          const auto range=reply->header(WINHTTP_QUERY_CONTENT_RANGE);
          if(sameValidator&&metadata.value("complete",false)&&range=="bytes */"+std::to_string(offset)&&
              metadata.value("expectedSize",uint64_t{0})==offset) {
            if(!reported){queue_.resumed(token_,key_,offset);reported=true;}bytes_=before+offset;progress("downloading",bytes_,segmented?0:offset,true);
            queue_.releaseTemporary(token_,utf8(part.filename().wstring()));return part;
          }
        }
        if(reply->status==206) {
          std::smatch match;const auto range=reply->header(WINHTTP_QUERY_CONTENT_RANGE);
          bool valid=offset&&sameValidator&&std::regex_match(range,match,std::regex(R"(^bytes ([0-9]+)-([0-9]+)/([0-9]+)$)"));
          uint64_t first=0,last=0;
          if(valid) {first=number(match[1]);last=number(match[2]);total=number(match[3]);
            valid=first==offset&&last>=first&&last<total&&total<=cap&&
              (!metadata.value("expectedSize",uint64_t{0})||metadata.value("expectedSize",uint64_t{0})==total)&&
              (!expected||expected==last-first+1);expected=last-first+1;}
          if(!valid) {
            if(restarted||!offset) throw std::runtime_error("media-http-range-invalid");
            discard(part);metadata=Json::object();offset=0;restarted=true;continue;
          }
          if(!reported){queue_.resumed(token_,key_,offset);reported=true;}
        } else if(reply->status==200) {
          if(offset) {discard(part);offset=0;metadata=Json::object();}
          total=expected;
        } else if(reply->status==416&&offset&&!restarted) {
          discard(part);metadata=Json::object();offset=0;restarted=true;continue;
        } else throw std::runtime_error("media-http-"+std::to_string(reply->status));
        if(total>cap||expected>cap-offset) throw std::runtime_error("media-size-invalid");
        std::string type,validator;
        if(usableValidator(etag)&&etag.size()>1&&etag.front()=='"'&&etag.back()=='"') {type="etag";validator=etag;}
        else if(etag.empty()&&strongModifiedDate(modified,reply->header(WINHTTP_QUERY_DATE))) {type="last-modified";validator=modified;}
        metadata={{"resumeProtocol",1},{"identity",identity},{"finalSource",reply->source},
          {"validatorType",type},{"validator",validator},{"validatorDate",reply->header(WINHTTP_QUERY_DATE)},{"expectedSize",total},{"complete",false},{"updatedAt",now()}};
        atomicWrite(metadataPath,metadata);
        std::ofstream output(part,std::ios::binary|(offset?std::ios::app:std::ios::trunc));
        if(!output) throw std::runtime_error("media-write-failed");
        progress("downloading",before+offset,segmented?0:total,true);
        uint64_t received=0;char buffer[64*1024];
        for(;;) {
          check();DWORD count=0;
          if(!WinHttpReadData(reply->request->value,buffer,sizeof buffer,&count)) throw std::runtime_error("media-network-read-failed");
          if(!count) break;
          if(offset>cap||!capacity::downloadFits(offset+received,count,cap)) throw std::runtime_error("media-size-invalid");
          context_->reserve(queue_.root(),count);output.write(buffer,count);
          if(!output) throw std::runtime_error("media-write-failed");
          received+=count;bytes_=before+offset+received;taskProgressAt_=GetTickCount64();progress("downloading",bytes_,segmented?0:total);
        }
        output.flush();output.close();if(!output) throw std::runtime_error("media-write-failed");
        offset+=received;
        if(expected&&received!=expected) throw std::runtime_error("media-download-truncated");
        if(!offset) throw std::runtime_error("media-download-empty");
        metadata["complete"]=total&&offset==total;metadata["updatedAt"]=now();atomicWrite(metadataPath,metadata);
        if(reply->status==206&&offset<total) continue;
        bytes_=before+offset;progress("downloading",bytes_,segmented?0:total,true);
        queue_.releaseTemporary(token_,utf8(part.filename().wstring()));return part;
      }
      throw std::runtime_error("media-http-range-limit");
    } catch(const std::exception &e) {
      const std::string reason=e.what();
      if(reason=="media-size-invalid"||reason=="media-staging-budget-full"||reason=="media-disk-space-low"||reason=="media-http-range-invalid"||
          !retained(part,identity)) discard(part);
      try{queue_.releaseTemporary(token_,utf8(part.filename().wstring()));}catch(...){}
      throw;
    }
  }
  Json install(Json lease,const std::string &url,const std::string &slot) {
    check();capacity::require(queue_.root(),1,true,capacity::admissionBytes);bytes_=0;
    const auto binding=lease.value("key",key_)+"\n"+slot;
    std::vector<fs::path> parts;fs::path assembled;int selectedHeight=0;
    auto clearAssembly=[&] {
      if(assembled.empty()) return;
      discard(assembled);
      try{queue_.releaseTemporary(token_,utf8(assembled.filename().wstring()));}catch(...){}
    };
    try {
      const auto isHls=url.substr(0,url.find_first_of("?#")).ends_with(".m3u8");
      fs::path source;
      if(!isHls) {
        source=downloadFile(url,binding,capacity::maxAssetBytes,false);parts.push_back(source);
      } else {
        // At most three official quality choices: preferred, <=720, <=480.
        // Keep tiny manifests in this call only; no persistent decoder or timer.
        std::map<std::string,std::string> playlists;
        std::set<std::string> attempted;
        int ceiling=0;
        for(int qualityAttempt=0;qualityAttempt<3;++qualityAttempt) {
          std::string current=url,playlistIdentity;HlsPlan plan;uint64_t bandwidth=0;
          selectedHeight=0;
          for(int depth=0;depth<3;++depth) {
            check();
            if(!playlists.contains(current))playlists[current]=request(current,1024*1024);
            const auto &text=playlists.at(current);playlistIdentity+=current+"\n"+text+"\n";
            plan=hlsPlan(text,current,ceiling,preferredVideoHeight(lease));
            if(!plan.variant.empty()) {selectedHeight=plan.height;bandwidth=plan.bandwidth;current=plan.variant;continue;}break;
          }
          if(plan.segments.empty()) throw std::runtime_error("media-playlist-depth");
          if(!attempted.insert(current).second)throw std::runtime_error("media-size-invalid");
          try {
            if(hlsExceedsLimit(plan,bandwidth,capacity::maxAssetBytes))throw std::runtime_error("media-size-invalid");
            const auto segmentBinding=binding+"\nhls:"+digestKey(playlistIdentity);
            uint64_t total=0;
            for(const auto &segment:plan.segments) {
              if(total>=capacity::maxAssetBytes)throw std::runtime_error("media-size-invalid");
              const auto file=downloadFile(segment,segmentBinding,capacity::maxAssetBytes-total,true);
              parts.push_back(file);total+=fs::file_size(file);
            }
            break;
          } catch(const std::exception &e) {
            if(std::string(e.what())!="media-size-invalid")throw;
            // This quality cannot be published. Do not retain its resumable
            // fragments while repeatedly trying an officially lower variant.
            for(const auto &part:parts)discard(part);parts.clear();bytes_=0;
            ceiling=lowerHlsHeight(selectedHeight);
            if(!ceiling||qualityAttempt==2)throw;
            progress("resolving",0,0,true);
          }
        }
        const auto folder=queue_.root()/L"cache"/L"jobs"/L"game-media";
        assembled=folder/wide(randomId()+".part");queue_.trackTemporary(token_,utf8(assembled.filename().wstring()));
        std::ofstream output(assembled,std::ios::binary|std::ios::trunc);
        char buffer[128*1024];
        for(const auto &part:parts) {
          std::ifstream input(part,std::ios::binary);if(!input) throw std::runtime_error("media-resume-cache-unreadable");
          while(input) {
            check();input.read(buffer,sizeof buffer);const auto count=input.gcount();if(!count) break;
            context_->reserve(queue_.root(),count);output.write(buffer,count);
            if(!output) throw std::runtime_error("media-write-failed");
          }
          if(!input.eof()) throw std::runtime_error("media-resume-cache-unreadable");
        }
        output.flush();output.close();if(!output) throw std::runtime_error("media-write-failed");source=assembled;
      }
      if(slot=="dynamic") validateDownloadedVideo(source);
      lease["slot"]=slot;lease["verified"]=true;if(slot=="dynamic"){lease["videoQualityPolicy"]=videoQualityPolicy;if(selectedHeight>0)lease["selectedHeight"]=selectedHeight;else lease.erase("selectedHeight");}
      progress("publishing",bytes_,bytes_,true);check();
      auto published=Store(queue_.root()).publish(lease,source,false);
      clearAssembly();for(const auto &part:parts) discard(part);return published;
    } catch(const std::exception &e) {
      const std::string reason=e.what();clearAssembly();
      const bool invalid=reason=="media-size-invalid"||reason.find("slot-container")!=std::string::npos||reason.find("unsupported-container")!=std::string::npos||
        reason.find("video-structure")!=std::string::npos||reason.find("video-incomplete")!=std::string::npos||
        reason.find("stale")!=std::string::npos;
      for(const auto &part:parts) {
        bool keep=false;
        try{const auto metadata=read(resumeMetadata(part));
          keep=!invalid&&usableValidator(stringField(metadata,"validator"))&&
            (reason!="media-video-truncated"||metadata.value("expectedSize",uint64_t{0})==0);
        }catch(...){}
        if(!keep) discard(part);
      }
      throw;
    }
  }
  void prepare(const Json &target) {
    check();
    Store store(queue_.root());
    auto requestTarget = target;
    requestTarget["demandConsumer"] = "background";
    const auto intent=queue_.snapshot();
    const auto manualRequest=intent.value("session",std::string{})==token_&&manualRun(intent)&&manualTarget(intent,stringField(target,"key"))?stringField(intent,"manualRequestId"):std::string{};
    auto lease = store.begin(requestTarget,manualRequest);
    if (!lease.value("acquired", false)) {
      const auto resolved = store.resolve(target);
      if (complete(resolved, now()) || resolved.value("deleted", false))
        queue_.result(token_, target["key"], true);
      else
        queue_.result(token_, target["key"], false, "cache-owned-or-deferred",
                      (std::max)(lease.value("retryAt", int64_t{0}),
                                 lease.value("leaseUntil", int64_t{0})));
      return;
    }
    try {
      const auto appId = target.value("appId", int64_t{0});
      Json document;
      // Some verified games are unavailable in a storefront region. Only an
      // explicit success=false allows a bounded alternate-region metadata read;
      // malformed, mismatched or non-game data still fails the original gates.
      for(const auto *region:{"us","cn","jp"}) {
        document=Json::parse(request("https://store.steampowered.com/api/appdetails?appids="+
            std::to_string(appId)+"&l=english&cc="+region));
        const auto key=std::to_string(appId);const auto found=document.find(key);
        if(found==document.end()||!found->is_object()||!found->contains("success")||
            !(*found)["success"].is_boolean()||(*found)["success"].get<bool>())break;
      }
      auto entry = document.value(std::to_string(appId), Json::object());
      auto data = entry.value("data", Json::object());
      if(entry.contains("success")&&entry["success"].is_boolean()&&!entry["success"].get<bool>()){
        lease["videoUnavailable"]=true;lease["staticUnavailable"]=true;store.finish(lease);queue_.result(token_,target["key"],true,"store-unavailable");return;
      }
      if(!entry.value("success",false)||data.value("steam_appid",int64_t{0})!=appId)throw std::runtime_error("media-identity-unconfirmed");
      if(data.value("type",std::string{})!="game"){
        lease["videoUnavailable"]=true;lease["staticUnavailable"]=true;store.finish(lease);queue_.result(token_,target["key"],true,"not-game-or-no-video");return;
      }
      lease["name"] = data.value("name", target.value("name", std::string{}));
      if(target.value("catalogSource",std::string{})=="popular")queue_.update(token_,[&](Json &j){
        const auto key=target.value("key",std::string{});if(j["jobs"].contains(key))j["jobs"][key]["name"]=lease["name"];
        if(j["active"].contains(key))j["active"][key]["name"]=lease["name"];
      });
      std::vector<std::string> videos;
      auto add = [&](const std::string &raw) {
        if (raw.empty()||videos.size()>=16)
          return;
        auto url = decodeMediaUrl(raw);
        try {
          sourceUrl(url);
        } catch (...) {
          return;
        }
        if (std::find(videos.begin(), videos.end(), url) == videos.end())
          videos.push_back(url);
      };
      const auto movies=data.value("movies",Json::array());
      for(const auto& movie:movies) {
        add(movie.value("hls_h264",std::string{}));add(movie.value("hlsManifest",std::string{}));
        for(const auto* quality:{"720","480","1080","max"})for(const auto* format:{"mp4","webm"})
          add(movie.value(format,Json::object()).value(quality,std::string{}));
      }
      if (videos.empty()) {
        auto page = request("https://store.steampowered.com/app/" +
                                std::to_string(appId) + "/?l=english",
                            4 * 1024 * 1024);
        for(const auto& url:fallbackPageVideos(page,appId))add(url);
      }
      std::string poster =
          data.value("background_raw", data.value("background", std::string{}));
      if (poster.empty()) {
        auto screenshots = data.value("screenshots", Json::array());
        if (!screenshots.empty())
          poster = screenshots.front().value("path_full", std::string{});
      }
      const auto slots = lease.value("taskSlots", Json::array());
      bool dynamicNeeded =
               std::find(slots.begin(), slots.end(), "dynamic") != slots.end(),
           staticNeeded =
               std::find(slots.begin(), slots.end(), "static") != slots.end();
      // The acquired store lease is authoritative if a newer selection won
      // after this queue item was claimed. Static-only work ignores video expiry.
      const auto selected=lease.value("selectedVideo",Json::object());
      if(dynamicNeeded&&!selected.empty()){
        const auto choice=decodeMediaUrl(selected.value("url",std::string{}));
        if(selected.value("appId",int64_t{0})!=appId||std::find(videos.begin(),videos.end(),choice)==videos.end())throw std::runtime_error("media-video-selection-expired");
        videos={choice};
      }
      std::string failure;
      if (staticNeeded && !poster.empty())
        try {
          lease = install(lease, decodeMediaUrl(poster), "static");
        } catch (const std::exception &e) {
          failure = e.what();
          if (failure.find("stale") != std::string::npos ||
              failure.find("cancelled") != std::string::npos)
            throw;
        }
      bool published = !dynamicNeeded;
      if (dynamicNeeded)
        for (size_t i = 0; i < (std::min)(size_t{16}, videos.size()); ++i) {
          try {
            lease = install(lease, videos[i], "dynamic");
            published = true;
            break;
          } catch (const std::exception &e) {
            failure = e.what();
            if (failure.find("stale") != std::string::npos ||
                failure.find("cancelled") != std::string::npos)
              throw;
          }
        }
      if (!published && !videos.empty())
        throw std::runtime_error(failure.empty() ? "media-download-failed"
                                                 : failure);
      lease["videoUnavailable"] = videos.empty();
      lease["staticUnavailable"] = poster.empty();
      store.finish(lease);
      if (!failure.empty() &&
          !store.resolve(target).value("staticReady", false) && !poster.empty())
        throw std::runtime_error(failure);
      queue_.result(token_, target["key"], true,
                    videos.empty() ? "no-dynamic-video" : "");
    } catch (const std::exception &e) {
      auto reason = std::string(e.what());
      if (reason == "media-download-cancelled" ||
          reason == "media-download-power-paused") {
        store.finish(lease);
        throw;
      }
      lease["reason"] = reason;
      auto failed = store.fail(lease);
      queue_.result(token_, target["key"], false, reason,
                    failed.value("retryAt", int64_t{0}));
      if(reason.find("media-disk-")==0||reason.find("media-cache-")==0||
          reason.find("media-staging-")==0||reason=="media-storage-unavailable") {
        std::lock_guard lock(context_->gate);context_->cancelled.store(true);
        if(context_->failure.empty())context_->failure=reason;
        throw;
      }
    }
  }

public:
  Worker(fs::path root,std::string token,HANDLE parent,std::shared_ptr<WorkerContext> context={},IdentityPorts identity={},Limits limits={})
      : queue_(root),identity_(std::move(identity)),limits_(limits),token_(std::move(token)),parent_(parent),
        context_(context?std::move(context):std::make_shared<WorkerContext>(root)) {}
  int run() {
    const auto mutexName = eventName(queue_.root(), L"worker-lock");
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!mutex)
      return 12;
    struct Guard {
      HANDLE handle;
      ~Guard() {
        ReleaseMutex(handle);
        CloseHandle(handle);
      }
    } guard{mutex};
    const auto locked = WaitForSingleObject(mutex, 0);
    if (locked != WAIT_OBJECT_0 && locked != WAIT_ABANDONED)
      return 13;
    try {
      queue_.cleanupTemporary();capacity::collectTemporary(queue_.root());context_->trim(queue_.root());
      queue_.update(token_, [](Json &j) {
        j["workerPid"] = GetCurrentProcessId();
        j["workerBirth"] = processBirthToken(GetCurrentProcessId());
      });
      const auto admission=queue_.snapshot();
      const bool parallelOwner=admission.value("ownerProtocol",0)>=2&&admission.value("ownerProtocolSession",std::string{})==token_;
      auto parallel=parallelOwner?(std::clamp)(admission.value("maxParallel",maximumParallel),1,maximumParallel):1;
      if(scopedManual(admission))parallel=(std::min)(parallel,static_cast<int>(admission["manualTargets"].size()));
      parallel=(std::min)(parallel,int((std::max)(uint64_t{1},
        (WorkerContext::stagingLimit-(std::min)(context_->used,WorkerContext::stagingLimit))/(2*capacity::maxAssetBytes))));
      const auto space=capacity::inspect(queue_.root());
      if(space.diskKnown) {
        const auto spare=space.available>capacity::diskReserveBytes?space.available-capacity::diskReserveBytes:0;
        parallel=(std::min)(parallel,int((std::max)(uint64_t{1},spare/(2*capacity::maxAssetBytes))));
      }
      queue_.update(token_,[&](Json &j){j["parallelSlots"]=parallel;});
      std::vector<std::thread> lanes;
      struct JoinLanes {
        std::vector<std::thread> &lanes;std::shared_ptr<WorkerContext> context;
        ~JoinLanes(){context->cancelled.store(true);context->wakeLanes();for(auto &lane:lanes)if(lane.joinable())lane.join();}
      } join{lanes,context_};
      for(int n=0;n<parallel;++n) lanes.emplace_back([this] {
        SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_IDLE);
        Worker lane(queue_.root(),token_,parent_,context_,identity_,limits_);
        try {
          for (;;) {
            if(GetTickCount64()-lane.started_>=limits_.session) break;
            lane.check();uint64_t observed;{std::lock_guard lock(context_->waitGate);observed=context_->revision;}
            auto target=queue_.claim(token_);if(target.is_null()){
              const auto state=queue_.snapshot();if(state.value("activeCount",size_t{0})==0){const auto due=state.value("nextDueAt",int64_t{-1});if(state.value("remaining",size_t{0})>0&&due>=0&&due<=now())continue;break;}
              std::unique_lock lock(context_->waitGate);context_->changed.wait(lock,[&]{return context_->cancelled.load()||context_->revision!=observed;});continue;
            }
            lane.key_=target.value("key",std::string{});lane.taskStarted_=GetTickCount64();lane.taskProgressAt_=0;lane.taskBudget_=(std::min)(limits_.attempt,target.value("attemptBudgetMs",uint64_t(automaticAttemptMs)));
            if(target.value("identityPending",false)) {
              try{identifyShortcut(queue_.root(),target,identity_,[&]{lane.progress("identifying",0,0,true);});lane.check();
                if(!identity_.current(target))throw std::runtime_error("media-shortcut-no-longer-current");
                if(!queue_.promoteIdentity(token_,target))throw std::runtime_error("media-identity-unconfirmed");
              }catch(const std::exception& error){const std::string reason=error.what();if(reason.find("cancelled")!=std::string::npos||reason.find("power-paused")!=std::string::npos)throw;queue_.result(token_,lane.key_,false,reason);}
            } else {if(stringField(target,"catalogSource")=="steam-shortcut"&&!identity_.current(target))queue_.result(token_,lane.key_,false,"media-shortcut-no-longer-current");else lane.prepare(target);}lane.taskStarted_=0;context_->wakeLanes();
          }
        } catch(const std::exception &e) {
          std::lock_guard lock(context_->gate);
          if(context_->failure.empty()) context_->failure=e.what();
          context_->cancelled.store(true);context_->wakeLanes();
        }
      });
      for(auto &lane:lanes) lane.join();
      if(!context_->failure.empty()) throw std::runtime_error(context_->failure);
      const auto finished=queue_.document();
      queue_.end(token_, manualWorkComplete(finished)||(queue_.snapshot().value("remaining", size_t{0}) == 0&&!queue_.snapshot().value("catalogPending",false))
                             ? "completed" : "deferred");
      return 0;
    } catch (const std::exception &e) {
      const auto reason = std::string(e.what());
      queue_.end(token_, "deferred", reason);
      return reason == "media-download-cancelled" || reason == "media-download-power-paused" ||
             reason.find("media-disk-")==0 || reason.find("media-cache-")==0 ||
             reason.find("media-staging-")==0 || reason=="media-storage-unavailable" ? 0 : 14;
    }
  }
};
// Called before input-owner parsing, the visible single-instance mutex,
// WebView2, window creation, splash, XInput timers or the parent input session.
inline std::optional<int> backgroundEntrypoint(const fs::path &appRoot) {
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv)
    return std::nullopt;
  struct Free {
    wchar_t **p;
    ~Free() { LocalFree(p); }
  } free{argv};
  if (argc < 2 || std::wstring_view(argv[1]) != L"--media-background")
    return std::nullopt;
  if (argc != 5)
    return 10;
  wchar_t *end = nullptr;
  const auto parentPid = wcstoul(argv[2], &end, 10);
  if (!parentPid || !end || *end)
    return 10;
  const auto birth = utf8(argv[3]), token = utf8(argv[4]);
  if (token.size() != 32 || !safeKey(token))
    return 10;
  HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                              FALSE, parentPid);
  if (!parent)
    return 11;
  struct Close {
    HANDLE p;
    ~Close() { CloseHandle(p); }
  } close{parent};
  if (processBirthToken(parentPid) != birth)
    return 11;
  try {
    const auto root = custom_steam_library::defaultDataRoot(appRoot);
    if (!Queue(root).allowed(token))
      return 11;
    SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);
    // Windows background mode may report/restore Normal for the class.
    // Reassert Idle after entering that mode, preserving its IO/memory hints.
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
    return Worker(root, token, parent).run();
  } catch (...) {
    return 15;
  }
}
} // namespace ymcc::media::downloads
