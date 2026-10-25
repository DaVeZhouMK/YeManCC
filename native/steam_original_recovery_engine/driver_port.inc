#ifndef YMCC_STEAM_ORIGINAL_DRIVER_VALIDATION_ONLY
std::uint64_t exportRva(const std::wstring &file, const char *name) {
  std::ifstream f{std::filesystem::path(file), std::ios::binary};
  std::vector<unsigned char> b;
  constexpr std::streamsize blockSize = 64 * 1024;
  char block[blockSize];
  for (;;) {
    const auto count = f.rdbuf()->sgetn(block, blockSize);
    b.insert(b.end(), block, block + count);
    // Stop at the first short read, including EOF or a file read error.
    if (count < blockSize)
      break;
  }
  need(b.size() > 4096, "dll-file");
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(b.data());
  const auto *nt =
      reinterpret_cast<const IMAGE_NT_HEADERS64 *>(b.data() + dos->e_lfanew);
  need(dos->e_magic == IMAGE_DOS_SIGNATURE &&
           nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC,
       "dll-pe");
  const auto at = [&](DWORD r) -> const unsigned char * {
    const auto *sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
      if (r >= sec[i].VirtualAddress &&
          r - sec[i].VirtualAddress < sec[i].SizeOfRawData) {
        const auto off = sec[i].PointerToRawData + r - sec[i].VirtualAddress;
        need(off < b.size(), "pe-bound");
        return b.data() + off;
      }
    throw std::runtime_error("pe-rva");
  };
  const auto &d =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  const auto *ex =
      reinterpret_cast<const IMAGE_EXPORT_DIRECTORY *>(at(d.VirtualAddress));
  const auto *names = reinterpret_cast<const DWORD *>(at(ex->AddressOfNames));
  const auto *ords =
      reinterpret_cast<const WORD *>(at(ex->AddressOfNameOrdinals));
  const auto *funcs =
      reinterpret_cast<const DWORD *>(at(ex->AddressOfFunctions));
  for (DWORD i = 0; i < ex->NumberOfNames; ++i)
    if (std::strcmp(reinterpret_cast<const char *>(at(names[i])), name) == 0) {
      const auto r = funcs[ords[i]];
      need(r < d.VirtualAddress || r >= d.VirtualAddress + d.Size,
           "forwarded-export-refused");
      return r;
    }
  throw std::runtime_error("export-not-found");
}
std::uint64_t remoteSystemExport(DWORD pid, const char *name) {
  auto fn = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
  need(fn != nullptr, "local-system-export");
  HMODULE owner = nullptr;
  need(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(fn), &owner),
       "system-export-owner");
  wchar_t path[32768]{};
  need(GetModuleFileNameW(owner, path, 32768) != 0, "system-owner-path");
  rw::Module remote;
  need(rw::exactModule(pid, path, remote), "same-system-module-in-target");
  return remote.base + reinterpret_cast<std::uint64_t>(fn) -
         reinterpret_cast<std::uint64_t>(owner);
}
// A timeout leaves the handle present: never free inputs or unload the DLL
// then.
DWORD remoteCall(HANDLE process, std::uint64_t fn, std::uint64_t arg,
                 unsigned ms, H &active, bool* dispatched = nullptr) {
  requireTargetMutation();
  need(!active.good(), "previous-management-call-unresolved");
  active = H(CreateRemoteThread(process, nullptr, 0,
                                reinterpret_cast<LPTHREAD_START_ROUTINE>(fn),
                                reinterpret_cast<void *>(arg), 0, nullptr));
  need(active.good(), "own-trial-management-thread");
  // Before this point a revoked guard means known-not-dispatched. From this
  // point every exception/timeout must retain the inputs and loaded image.
  if (dispatched) *dispatched = true;
  const auto waited = WaitForSingleObject(active.h, ms);
  need(waited == WAIT_OBJECT_0,
       waited == WAIT_TIMEOUT
           ? "management-thread-timeout-retain-input-and-dll"
           : "management-thread-wait-failed-retain-input-and-dll");
  DWORD code = 0;
  need(GetExitCodeThread(active.h, &code), "management-thread-result");
  active = H();
  return code;
}
// exactModule(false) conflates absence with an incomplete/failed census.
bool trialModule(DWORD pid, const std::wstring &path, rw::Module &found) {
  H snapshot(
      CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
  need(snapshot.good(), "trial-module-census");
  MODULEENTRY32W row{};
  row.dwSize = sizeof row;
  need(Module32FirstW(snapshot.h, &row) != FALSE, "trial-module-census-first");
  const auto expectedPath = rw::pathKey(path);
  const auto expectedBasename =
      rw::pathKey(std::filesystem::path(path).filename().wstring());
  unsigned matches = 0;
  do {
    const auto observedPath = rw::pathKey(row.szExePath);
    if (observedPath != expectedPath &&
        rw::pathKey(row.szModule) == expectedBasename)
      throw std::runtime_error("trial-basename-already-loaded-at-other-path");
    if (observedPath == expectedPath) {
      ++matches;
      found.base = reinterpret_cast<std::uint64_t>(row.modBaseAddr);
      found.size = row.modBaseSize;
      found.path = row.szExePath;
    }
  } while (Module32NextW(snapshot.h, &row));
  need(GetLastError() == ERROR_NO_MORE_FILES, "trial-module-census-complete");
  need(matches <= 1, "trial-module-ambiguous");
  return matches == 1;
}
void checkPacket(const tr::Packet &p, tr::Op op, osd::Nonce nonce) {
  need(p.size == sizeof p && p.version == tr::abi && p.op == op &&
           p.nonce.low == nonce.low && p.nonce.high == nonce.high && tr::validQualificationRoute(p.qualificationRoute),
       "control-packet-identity");
  need(p.status.size == sizeof p.status &&
           p.status.abiVersion == osd::kAbiVersion &&
           static_cast<unsigned>(p.status.state) <=
               static_cast<unsigned>(osd::State::Cleaned),
       "control-status-abi");
}
struct Mem {
  HANDLE process = nullptr;
  std::uint64_t address = 0;
  Mem() = default;
  Mem(HANDLE h, std::size_t bytes) : process(h) {
    requireCurrent();
    address = reinterpret_cast<std::uint64_t>(VirtualAllocEx(
        h, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    need(address != 0, "trial-input-allocation");
  }
  void release() {
    if (address) {
      need(VirtualFreeEx(process, reinterpret_cast<void *>(address), 0,
                         MEM_RELEASE),
           "free-trial-input");
      address = 0;
    }
  }
};
J statusJson(const tr::Packet &p) {
  const auto &s = p.status;
  return {{"controlCode", p.returnCode},
          {"qualificationRoute",tr::qualificationRouteName(p.qualificationRoute)},
          {"trialAbi",p.version},
          {"state", static_cast<unsigned>(s.state)},
          {"inFlight", s.inFlight},
          {"hookCalls", s.hookCalls},
          {"ownerRejects", s.ownerRejects},
          {"accountRejects", s.accountRejects},
          {"threadRejects", s.threadIdRejects},
          {"callbackAttempts", s.callbackAttempts},
          {"callbackCompleted", s.callbackCompleted},
          {"callbackRejected", s.callbackRejected},
          {"callbackAborted", s.callbackAborted},
          {"trampolineAllocated", s.trampolineAllocated},
          {"trialStage", p.telemetry.stage},
          {"guardBefore", p.telemetry.guardBefore},
          {"guardAfter", p.telemetry.guardAfter},
          {"publisherCalls", p.telemetry.publisherCalls},
          {"publisherReturned", p.telemetry.publisherReturned},
          {"callbackResult", p.telemetry.callbackResult},
          {"sourceThread", p.telemetry.sourceThread},
          {"flagsBefore", p.telemetry.flagsBefore},
          {"flagsAfterImmediate", p.telemetry.flagsAfter},
          {"primaryBefore", p.telemetry.primaryBefore},
          {"primaryAfter", p.telemetry.primaryAfter},
          {"rejectCode", p.telemetry.rejectCode}};
}

// Save a restorable DR image before invoking the reviewed observer's installer.
void armChecked(SavedThread &t, std::uint64_t address) {
  CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
  need(GetThreadContext(t.handle.h, &c) != FALSE, "save-debug-registers");
  need(c.Dr0 == 0 && c.Dr1 == 0 && c.Dr2 == 0 && c.Dr3 == 0 && (c.Dr7 & 0xff) == 0,
       "existing-debug-register-refused");
  t.old = c; t.armed = true;
  arm(t, address);
}
void restoreStopped(std::vector<SavedThread> &threads) {
  reviewedSupportRestoreStopped(threads);
  for (auto &t : threads) {
    if (!t.armed || WaitForSingleObject(t.handle.h, 0) == WAIT_OBJECT_0) continue;
    CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    need(GetThreadContext(t.handle.h, &c) && c.Dr0 == t.old.Dr0 && c.Dr1 == t.old.Dr1 &&
         c.Dr2 == t.old.Dr2 && c.Dr3 == t.old.Dr3 &&
         (c.Dr6 & 0xf) == (t.old.Dr6 & 0xf) &&
         (c.Dr7 & debugMask) == (t.old.Dr7 & debugMask), "restored-debug-registers-confirmed");
  }
}
#endif // production process/debugger operations
std::uint64_t decimalInput(const J &j) {
  need(j.is_string(), "decimal-string-required");
  const auto text = j.get<std::string>();
  need(!text.empty() && text.size() <= 20, "invalid-decimal");
  std::uint64_t value = 0;
  for (const char c : text) {
    need(c >= '0' && c <= '9', "invalid-decimal");
    const auto digit = static_cast<std::uint64_t>(c - '0');
    need(value <= (std::numeric_limits<std::uint64_t>::max() - digit) / 10, "decimal-overflow");
    value = value * 10 + digit;
  }
  need(value != 0, "zero-identity"); return value;
}
DWORD dwordInput(const J &j, bool zero = false) {
  need(j.is_number_integer(), "integer-required");
  if (!j.is_number_unsigned()) need(j.get<std::int64_t>() >= 0, "negative-integer");
  const auto value = j.get<std::uint64_t>();
  need(value <= MAXDWORD && (zero || value != 0), "invalid-dword");
  return static_cast<DWORD>(value);
}
// An optional host deadline is already in this machine's GetTickCount64 domain.
// It can shorten this attempt, never extend it or grant another reservation.
std::uint64_t mutationDeadlineForRequest(const J &req,std::uint64_t now) {
  need(now<=(std::numeric_limits<std::uint64_t>::max)()-30000,"mutation-deadline-overflow");
  const auto localLimit=now+30000;
  if(!req.contains("mutationDeadlineTick"))return localLimit;
  const auto requested=decimalInput(req.at("mutationDeadlineTick"));
  need(requested>now,"mutation-deadline-expired");
  return (std::min)(localLimit,requested);
}
void requireMutationDeadline(std::uint64_t deadline,std::uint64_t now) {
  need(deadline!=0&&now<deadline,"mutation-deadline-expired-before-arm");
}
tr::QualificationRoute qualificationRouteForRequest(const J& req){
  const auto route=req.value("recoveryRoute",std::string("semi-static"));
  need(route=="legacy-dynamic"||route=="semi-static","recovery-route-unsupported");
  return route=="legacy-dynamic"?tr::QualificationRoute::LegacyDynamic:tr::QualificationRoute::SemiStatic;
}
void bindQualificationRoute(tr::Packet& packet,const J& req){packet.qualificationRoute=qualificationRouteForRequest(req);}
void validateRequest(const J &req) {
  need(req.is_object(), "request-object-required");
  (void)qualificationRouteForRequest(req);
  (void)mutationDeadlineForRequest(req,GetTickCount64());
  decimalInput(req.at("account64"));
  const auto gameId=decimalInput(req.at("gameId"));
  const auto appid=dwordInput(req.at("appid"));
  need(tr::shortcutAppId(gameId)==appid&&ymcc::steamappstatewin::gameIdMatches(gameId,appid),
       "bound-type2-shortcut-game-required");
  const auto nonce = req.at("nonce").get<std::string>();
  need(nonce.size() == 36, "uuid-required");
  bool nonzero = false;
  for (std::size_t i = 0; i < nonce.size(); ++i) {
    const char c = nonce[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) need(c == '-', "uuid-hyphens");
    else {
      need((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'), "uuid-hex");
      nonzero = nonzero || c != '0';
    }
  }
  need(nonzero, "zero-nonce");
  const auto &processes = req.at("processes");
  DWORD session = 0; std::set<DWORD> pids;
  for (const char *role : {"steam", "game"}) {
    const auto &p = processes.at(role);
    const auto pid = dwordInput(p.at("pid"));
    need(pids.insert(pid).second, "distinct-process-pids-required");
    decimalInput(p.at("birthFileTime"));
    const auto current = dwordInput(p.at("session"), true);
    if (std::string(role) == "steam") session = current;
    need(current == session, "same-session-required");
    const auto path = utf16(p.at("path").get<std::string>());
    need(path.size() > 3 && path.size() < 520 && path[1] == L':' && path[2] == L'\\' &&
         path.find(L'\0') == std::wstring::npos, "absolute-process-path-required");
    if (std::string(role) == "steam") need(tr::validSteamPath(path), "bound-steam-executable-path-required");
  }
  dwordInput(req.at("sourceThreadId")); decimalInput(req.at("sourceThreadBirth"));
  need(req.at("noPublish").is_boolean() && !req.at("noPublish").get<bool>(), "recovery-only-no-publish-refused");
  need(req.contains("healthyProof") && req.at("healthyProof").is_object(), "external-healthy-proof-required");
  for (const char *role : {"ymcc", "cef"}) {
    const auto &p = processes.at(role);
    need(pids.insert(dwordInput(p.at("pid"))).second, "distinct-process-pids-required");
    decimalInput(p.at("birthFileTime"));
    need(dwordInput(p.at("session"), true) == session, "same-session-required");
    const auto path = utf16(p.at("path").get<std::string>());
    need(path.size() > 3 && path.size() < 520 && path[1] == L':' && path[2] == L'\\' &&
         path.find(L'\0') == std::wstring::npos, "absolute-process-path-required");
  }
}

void bindExternalHealthyProof(tr::Packet &packet, const J &req) {
  const auto &proof = req.at("healthyProof");
  const auto pinNonce = proof.at("pinNonce").get<std::string>();
  need(pinNonce.size() == 36, "proof-pin-nonce-length");
  bool nonzero = false;
  for (std::size_t i = 0; i < pinNonce.size(); ++i) {
    const char c = pinNonce[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) need(c == '-', "proof-pin-nonce-hyphens");
    else {
      need((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), "proof-pin-nonce-hex");
      nonzero = nonzero || c != '0';
    }
  }
  need(nonzero, "proof-pin-nonce-zero");
  std::copy(pinNonce.begin(), pinNonce.end(), packet.healthyProof.pinNonce);
  for (const auto &binding : {std::pair{"ownerPin", packet.healthyProof.ownerPin},
                             std::pair{"recordPin", packet.healthyProof.recordPin},
                             std::pair{"uiObjectPin", packet.healthyProof.uiObjectPin}}) {
    const auto pin = proof.at(binding.first).get<std::string>();
    need(pin.size() == 64, "proof-pin-length");
    for (const char c : pin) need((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), "proof-pin-hex");
    std::copy(pin.begin(), pin.end(), binding.second);
  }
  need(dwordInput(proof.at("healthyFlags")) == 8196, "proof-healthy-flags-required");
  packet.healthyProof.healthyFlags = 8196;
}
#ifndef YMCC_STEAM_ORIGINAL_DRIVER_VALIDATION_ONLY
std::wstring trialDirectory() { need(!gTrialDirectory.empty(), "trial-directory-not-bound"); return gTrialDirectory; }
std::wstring selectedTrialPath(const J &req) {
  const auto root = trialDirectory();
  std::filesystem::path selected = req.contains("trialDllPath") ?
      std::filesystem::path(utf16(req.at("trialDllPath").get<std::string>())) :
      std::filesystem::path(root) / L"wuxia-publish-trial.dll";
  const auto text = selected.wstring();
  need(selected.is_absolute() && text.size() > 3 && text[1] == L':' && text[2] == L'\\' &&
       text.find(L'\0') == std::wstring::npos, "absolute-trial-dll-path-required");
  selected = std::filesystem::canonical(selected);
  need(rw::pathKey(selected.parent_path().wstring()) == rw::pathKey(root), "trial-dll-outside-controlled-directory");
  const auto name = selected.filename().wstring();
  need(name.size() > 4 && name.size() <= 160 && rw::pathKey(selected.extension().wstring()) == L".dll", "trial-dll-basename");
  for (const auto c : name) need((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
      (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' || c == L'.', "trial-dll-basename");
  need(std::filesystem::is_regular_file(selected), "trial-dll-file-required");
  return selected.wstring();
}
struct LifetimeMutex {
  H handle; bool owned = false;
  ~LifetimeMutex() { if (owned) ReleaseMutex(handle.h); }
  void acquire(DWORD pid, std::uint64_t birth) {
    const auto name = L"Local\\YMCCWuxiaRecovery-" + std::to_wstring(pid) + L"-" + std::to_wstring(birth);
    handle = H(CreateMutexW(nullptr, FALSE, name.c_str()));
    need(handle.good(), "lifetime-mutex-create");
    const auto waited = WaitForSingleObject(handle.h, 0);
    owned = waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED;
    need(waited == WAIT_OBJECT_0, waited == WAIT_TIMEOUT ? "lifetime-mutex-busy" : "lifetime-mutex-abandoned-or-failed");
  }
};
void bindAuxiliary(tr::AuxiliaryIdentity &out, const BoundLive &p) {
  out.pid = p.pid; out.session = p.session; out.birth = p.born;
  need(p.path.size() < std::size(out.path), "auxiliary-path-limit");
  std::copy(p.path.begin(), p.path.end(), out.path);
}
void requireAttemptCapacity(DWORD pid, const std::wstring &selected) {
  rw::FileStamp selectedStamp{};
  need(rw::fileStamp(selected, selectedStamp), "trial-file-identity");
  const auto root = rw::pathKey(trialDirectory());
  H snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
  need(snapshot.good(), "attempt-census");
  MODULEENTRY32W row{}; row.dwSize = sizeof row;
  need(Module32FirstW(snapshot.h, &row), "attempt-census-first");
  unsigned retained = 0;
  do {
    const std::filesystem::path image(row.szExePath);
    if (rw::pathKey(image.parent_path().wstring()) == root && rw::pathKey(image.extension().wstring()) == L".dll") {
      ++retained;
      rw::FileStamp loadedStamp{};
      need(rw::fileStamp(image.wstring(), loadedStamp), "retained-image-file-identity");
      need(loadedStamp.volume != selectedStamp.volume || loadedStamp.indexHi != selectedStamp.indexHi ||
           loadedStamp.indexLo != selectedStamp.indexLo, "trial-image-already-retained-under-alias");
    }
  } while (Module32NextW(snapshot.h, &row));
  need(GetLastError() == ERROR_NO_MORE_FILES, "attempt-census-complete");
  need(retained < 16, "per-steam-attempt-capacity");
}

J runTrial(const J &req, QueryActor &query, Current beforeMutation = {}) {
  J out = {{"schema", "wuxia-single-original-publisher-trial.v1"},
           {"sleep", 0},
           {"gameLaunches", 0},
           {"directFlagsWrites", 0},
           {"directPidWrites", 0},
           {"publisherCalls", 0},
           {"repairAccepted", false},
           {"openProcessAttempted", false},
           {"mutationBoundarySchema", "ymcc.steam-original-driver-publication-boundary.v2"},
           {"mutationBoundary", "after-driver-readonly-qualification-before-first-remote-allocation"},
           {"mutationCommitCallbackProvided", bool(beforeMutation)},
           {"driverReadOnlyQualificationPassed", false},
           {"mutationCommitCallbackInvoked", false},
           {"mutationCommitAccepted", false},
           {"remoteAllocationAttempted", false},
           {"dllUnloadAttempted", false},
           {"dependencyReleasePolicy", "next-normal-steam-process-exit"},
           {"writes", J::array()},
           {"sourceEntries", J::array()}};
  H process;
  BoundLive steam, game, ymcc, cef;
  LifetimeMutex lifetimeMutex;
  Mem packetMemory, pathMemory;
  tr::Packet packet{};
  std::uint64_t control = 0, dllBase = 0, getter = 0, dllSize = 0,
                activeWatchAddress = 0;
  bool loaded = false, preparedTrial = false, attached = false,
       pendingNow = false, patched = false;
  bool loadAttempted = false, loadCompleted = false, armAttempted = false,
       entryEverExposed = false, controlUnknown = false;
  bool restorationOrDetachStarted = false, initialBreakpointSeen = false;
  std::string sessionNonce;
  H managementThread;
  std::wstring trialPath;
  DWORD pendingStatus = DBG_CONTINUE;
  DEBUG_EVENT pending{};
  std::vector<SavedThread> threads;
  DWORD pid = 0;
  std::array<unsigned char, 14> original{};
  DWORD originalProtection = 0;
  const auto controlCall = [&](tr::Op op) {
    need(!controlUnknown, "control-outcome-unresolved");
    packet.op = op;
    const auto nonce = packet.nonce;
    const auto healthyProof = packet.healthyProof;const auto expectedRoute=packet.qualificationRoute;
    const auto expectedYmcc = packet.ymcc, expectedCef = packet.cef;
    if (op == tr::Op::Arm) {
      requireMutationDeadline(packet.deadlineTick,GetTickCount64());
      armAttempted = true;
    }
    writeExact(process.h, packetMemory.address, &packet, sizeof packet);
    const auto code = remoteCall(process.h, control, packetMemory.address, 5000,
                                 managementThread, &controlUnknown);
    readExact(process.h, packetMemory.address, &packet, sizeof packet);
    checkPacket(packet, op, nonce);
    need(packet.qualificationRoute==expectedRoute&&!std::memcmp(&packet.healthyProof, &healthyProof, sizeof healthyProof) &&
         !std::memcmp(&packet.ymcc, &expectedYmcc, sizeof expectedYmcc) &&
         !std::memcmp(&packet.cef, &expectedCef, sizeof expectedCef), "control-proof-and-auxiliary-identity");
    need(code == packet.returnCode, "control-return-consistent");
    controlUnknown = false;
    return code;
  };
  const auto restoreEntry = [&] {
    if (!patched)
      return;
    std::array<unsigned char, 14> ownedDetour{};
    readExact(process.h, getter, ownedDetour.data(), ownedDetour.size());
    const bool stillOriginal = ownedDetour == original;
    const bool ourDetour = !std::memcmp(ownedDetour.data(), packet.prepared.detourBytes, ownedDetour.size());
    // Protection can have changed before the guarded entry write started.
    // An exact original entry needs only its protection restored. A partial or
    // foreign entry is still unsupported and must not be overwritten.
    need(stillOriginal || ourDetour, "restore-owned-or-original-entry-required");
    MEMORY_BASIC_INFORMATION beforeProtection{};
    need(VirtualQueryEx(process.h, reinterpret_cast<void *>(getter), &beforeProtection,
                        sizeof beforeProtection) == sizeof beforeProtection &&
             (beforeProtection.Protect == PAGE_EXECUTE_READWRITE ||
              beforeProtection.Protect == originalProtection),
         "restore-owned-entry-protection-required");
    if (ourDetour) {
      DWORD old = 0;
      requireTargetMutation();
      need(VirtualProtectEx(process.h, reinterpret_cast<void *>(getter),
                            original.size(), PAGE_EXECUTE_READWRITE, &old),
           "restore-code-protect");
      writeExact(process.h, getter, original.data(), original.size());
      need(FlushInstructionCache(process.h, reinterpret_cast<void *>(getter),
                                 original.size()),
           "restore-code-cache");
    }
    out["entryAlreadyOriginalAtCleanup"] = stillOriginal;
    DWORD ignored = 0;
    requireTargetMutation();
    need(VirtualProtectEx(process.h, reinterpret_cast<void *>(getter),
                          original.size(), originalProtection, &ignored),
         "restore-code-original-protection");
    std::array<unsigned char, 14> check{};
    readExact(process.h, getter, check.data(), check.size());
    need(check == original, "restore-code-readback");
    MEMORY_BASIC_INFORMATION region{};
    need(VirtualQueryEx(process.h, reinterpret_cast<void *>(getter), &region,
                        sizeof region) == sizeof region &&
             region.Protect == originalProtection,
         "restore-code-protection-readback");
    patched = false;
    out["getterEntryRestored"] = true;
  };
  try {
    nativeFailureBoundary([&] {
    requireCurrent();
    validateRequest(req);
    bindQualificationRoute(packet,req); // Explicitly set the wire route; never inherit the Packet default.
    need(req.value("recoveryRoute",std::string("semi-static"))==routeName(),"host-driver-route-mismatch");
    out["recoveryRoute"]=tr::qualificationRouteName(packet.qualificationRoute);out["trialAbi"]=tr::abi;
    bindExternalHealthyProof(packet, req);
    trialPath = selectedTrialPath(req);
    lifetimeMutex.acquire(dwordInput(req.at("processes").at("steam").at("pid")),
                          decimalInput(req.at("processes").at("steam").at("birthFileTime")));
    const auto account = decimalInput(req.at("account64"));
    const auto nonce = req.at("nonce").get<std::string>();
    sessionNonce = nonce;
    const auto gameId=decimalInput(req.at("gameId"));
    const auto appid=dwordInput(req.at("appid"));
    need(nonce.size()==36&&tr::shortcutAppId(gameId)==appid,"bound-game-request");
    out["openProcessAttempted"] = true;
    steam = bindLive(req.at("processes").at("steam"), PROCESS_ALL_ACCESS);
    pid = steam.pid;
    game = bindLive(req.at("processes").at("game"),
                    PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE);
    ymcc = bindLive(req.at("processes").at("ymcc"), PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE);
    cef = bindLive(req.at("processes").at("cef"), PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE);
    bindAuxiliary(packet.ymcc, ymcc); bindAuxiliary(packet.cef, cef);
    process = duplicate(steam.handle.h);
    BOOL debugger = TRUE;
    need(CheckRemoteDebuggerPresent(process.h, &debugger) && !debugger,
         "no-existing-debugger");
    rw::Module client, ui;
    need(originalImagePair(pid,
                           tr::steamModulePath(steam.path,L"steamclient64.dll"),
                           tr::steamModulePath(steam.path,L"SteamUI.dll"), client, ui),
         "client-ui-module-pair");
    const auto clientCompatibility=ymcc::steamcompat::qualifyFile(client.path,ymcc::steamcompat::ModuleKind::Client,false);
    const auto uiCompatibility=ymcc::steamcompat::qualifyFile(ui.path,ymcc::steamcompat::ModuleKind::Ui,false);
    need(clientCompatibility.layout.has_value()&&uiCompatibility.layout.has_value(),"client-ui-compatibility-contract");
    auto nativeProfile=tr::profileForCompatibility(*clientCompatibility.layout,*uiCompatibility.layout);
    if(nativeProfile){nativeProfile->getterRva=ymcc::steamcompat::resolvedRva(clientCompatibility,nativeProfile->getterRva);
        nativeProfile->publisherRva=ymcc::steamcompat::resolvedRva(clientCompatibility,nativeProfile->publisherRva);}
    const std::optional<ymcc::steamrecord::ProfileId> clientProfile=*clientCompatibility.layout==ymcc::steamcompat::Layout::Public ?
        ymcc::steamrecord::ProfileId::Public1792618285 : ymcc::steamrecord::ProfileId::Legacy1791918634;
    const std::optional<app::ProfileId> uiProfile=*uiCompatibility.layout==ymcc::steamcompat::Layout::Public ?
        app::ProfileId::Public1792618285 : app::ProfileId::Legacy1791918634;
    need(nativeProfile.has_value()&&nativeProfile->getterRva&&nativeProfile->publisherRva,"module-compatibility-selectors");
    const auto loadedRead=[&](const rw::Module& module){return ymcc::steamcompat::ReadRva([&,module](std::uint64_t rva,void* target,std::size_t bytes){
      if(rva>=module.size||bytes>module.size-rva)return false;SIZE_T got=0;
      return ReadProcessMemory(process.h,reinterpret_cast<const void*>(module.base+rva),target,bytes,&got)&&got==bytes;});};
    // Same named-slot import resolver and diagnostics as snapshot/DLL prepare.
    // Legacy wrapper returns its original raw guard before using this resolver.
    ymcc::steamcompat::LoadedDiagnostic clientDiagnostic,uiDiagnostic;
    const auto loadedClient=ymcc::steamcompat::qualifyLoaded(clientCompatibility,ymcc::steamcompat::ModuleKind::Client,
        loadedRead(client),client.base,client.size,{},
        [&](const ymcc::steamcompat::ImportSlot& slot,std::uint64_t target){return ymcc::steamcompat::resolvedImportTarget(process.h,slot,target,&clientDiagnostic);},&clientDiagnostic);
    const auto loadedUi=ymcc::steamcompat::qualifyLoaded(uiCompatibility,ymcc::steamcompat::ModuleKind::Ui,
        loadedRead(ui),ui.base,ui.size,{},
        [&](const ymcc::steamcompat::ImportSlot& slot,std::uint64_t target){return ymcc::steamcompat::resolvedImportTarget(process.h,slot,target,&uiDiagnostic);},&uiDiagnostic);
    out["loadedModuleQualification"]={{"client",compatibilityEvidence(loadedClient,clientDiagnostic)},
        {"ui",compatibilityEvidence(loadedUi,uiDiagnostic)}};
    if(!loadedClient.matched||!loadedUi.matched)throw DiagnosticFailure("loaded-client-ui-compatibility-contract",
        {{"stage","driver-loaded-code-and-data-contract"},{"client",out["loadedModuleQualification"]["client"]},
         {"ui",out["loadedModuleQualification"]["ui"]}});
    requireCurrent();
    need(ymcc::steamcompat::resolvedRva(clientCompatibility,ymcc::steamrecord::detail::profileLayout(*clientProfile).getterRva) == nativeProfile->getterRva,
         "getter-profile-contract");
    getter = client.base + nativeProfile->getterRva;
    readExact(process.h, getter, original.data(), original.size());
    need(!std::memcmp(original.data(), osd::kGetterBytes, 14),
         "unmodified-getter-required");
    const auto owned = [&] {
      return current() && rw::exactProcess(process.h, steam.pid, steam.born, steam.path,
                              steam.session) &&
             rw::exactProcess(game.handle.h, game.pid, game.born, game.path,
                              game.session) &&
             rw::exactProcess(ymcc.handle.h, ymcc.pid, ymcc.born, ymcc.path, ymcc.session) &&
             rw::exactProcess(cef.handle.h, cef.pid, cef.born, cef.path, cef.session);
    };
    const app::Read read = [&](std::uint64_t a, void *p, std::size_t n) {
      SIZE_T got = 0;
      return ymcc::steamappstatewin::readableRegion(process.h, a, n) &&
             ReadProcessMemory(process.h, reinterpret_cast<void *>(a), p, n,
                               &got) &&
             got == n;
    };
    const auto uiRead=ymcc::steamcompat::adaptProcessRead(uiCompatibility,ui.base,ui.size,read);
    const auto recordRead=ymcc::steamcompat::adaptProcessRead(clientCompatibility,client.base,client.size,read);
    app::Query aq{ui.base, ymcc::steamcompat::referenceImageSize(uiCompatibility,ui.size), appid, true, *uiProfile};
    need(!app::observe(aq, uiRead, owned).value("runningBitSet", true),
         "lost-baseline-before-prepare");
    app::detail::Reader ar(uiRead);
    need(app::detail::readFlags(ar, aq) == 4, "lost-flags");
    const auto flagAddress = ar.fields.back().address;
    activeWatchAddress = flagAddress;
    ymcc::steamrecord::Query rq{client.base, ymcc::steamcompat::referenceImageSize(clientCompatibility,client.size),
                                account,     gameId,
                                game.pid,    true, *clientProfile};
    need(ymcc::steamrecord::observe(rq, recordRead, owned)
             .value("originalPrimaryMatched", false),
         "original-primary-required");
    ymcc::steamrecord::detail::Reader rr(recordRead);
    const auto selected = ymcc::steamrecord::detail::selectUser(rr, rq);
    const auto manager = selected.user + 0x3800;
    const auto utf = trialPath;
    const auto controlRva = exportRva(utf, "RunningTrialControl");
    rw::Module existing;
    need(!trialModule(pid, utf, existing), "preexisting-trial-dll-refused");
    requireAttemptCapacity(pid, utf);
    need(moduleCapacity(pid) < 16, "global-steam-attempt-capacity");
    packet.account = account;
    packet.gameId = gameId;
    packet.gamePid = game.pid;
    packet.gameBirth = game.born;
    packet.steamPid = steam.pid;
    packet.steamBirth = steam.born;
    packet.session = steam.session;
    packet.threadId = req.at("sourceThreadId");
    packet.threadBirth =
        std::stoull(req.at("sourceThreadBirth").get<std::string>());
    packet.noPublish = req.at("noPublish").get<bool>() ? 1u : 0u;
    packet.deadlineTick = mutationDeadlineForRequest(req,GetTickCount64());
    need(game.path.size() < std::size(packet.gamePath), "game-path-limit");
    std::copy(game.path.begin(), game.path.end(), packet.gamePath);
    need(steam.path.size()<std::size(packet.steamPath),"steam-path-limit");
    std::copy(steam.path.begin(),steam.path.end(),packet.steamPath);
    packet.nonce.low = std::stoull(nonce.substr(0, 8) + nonce.substr(9, 4) +
                                       nonce.substr(14, 4),
                                   nullptr, 16);
    packet.nonce.high =
        std::stoull(nonce.substr(19, 4) + nonce.substr(24, 12), nullptr, 16);
    const auto loadLibrary = remoteSystemExport(pid, "LoadLibraryW");
    requireCurrent();need(owned(), "cohort-before-mutation-commit");
    requireMutationDeadline(packet.deadlineTick,GetTickCount64());
    out["driverReadOnlyQualificationPassed"] = true;
    // The callback is the sole publication reservation boundary. No allocation,
    // target write, management call, hook exposure or attach precedes it.
    // Legacy two-argument callers retain their prior behavior; handle always
    // supplies the durable commit callback on both routes.
    if(beforeMutation){out["mutationCommitCallbackInvoked"] = true;
      need(beforeMutation(), "publication-commit-rejected");}
    out["mutationCommitAccepted"] = true;
    requireCurrent();need(owned(), "cohort-after-mutation-commit");
    requireMutationDeadline(packet.deadlineTick,GetTickCount64());
    out["remoteAllocationAttempted"] = true; // Set before even a failed allocation.
    pathMemory = Mem(process.h, (utf.size() + 1) * sizeof(wchar_t));
    writeExact(process.h, pathMemory.address, utf.c_str(),
               (utf.size() + 1) * sizeof(wchar_t));
    remoteCall(process.h, loadLibrary, pathMemory.address, 5000,
               managementThread, &loadAttempted);
    loadCompleted = true;
    rw::Module module;
    need(trialModule(pid, utf, module), "own-trial-dll-loaded");
    loaded = true;
    dllBase = module.base;
    dllSize = module.size;
    pathMemory.release();
    need(controlRva < dllSize, "control-export-inside-module");
    control = dllBase + controlRva;
    packetMemory = Mem(process.h, sizeof packet);
    out["externalHealthyProofRequired"] = true;
    const auto prepareCode = controlCall(tr::Op::Prepare);
    out["prepareReturnCode"] = prepareCode;
    out["prepareStatus"] = statusJson(packet);
    preparedTrial = prepareCode == 0;
    need(prepareCode == 0, "trial-prepare");
    need(packet.status.state == osd::State::Prepared &&
             packet.status.trampolineAllocated == 1 &&
             packet.prepared.size == sizeof packet.prepared &&
             packet.prepared.abiVersion == osd::kAbiVersion &&
             packet.prepared.trampoline != nullptr,
         "trial-prepare-result");
    out["dllLoaded"] = true;
    out["prepared"] = statusJson(packet);
    out["nonce"] = nonce;
    out["gamePid"] = game.pid;
    out["steamPid"] = pid;
    out["noPublish"] = packet.noPublish != 0;
    publish(J{{"event", "prepared"},
                   {"nonce", nonce},
                   {"gamePid", game.pid},
                   {"noPublish", packet.noPublish != 0}});
    requireCurrent();
    need(owned(), "cohort-before-install");
    const auto expected = 4u;
    need(app::observe(aq, uiRead, owned).at("nativeAppFlags").get<unsigned>() ==
             expected,
         "expected-native-state");
    need(controlCall(tr::Op::Arm) == 0, "trial-arm");
    requireCurrent();
    // Context capture already established this owner thread's debug object.
    need(DebugSetProcessKillOnExit(FALSE), "same-thread-debug-object-exit-policy-required");
    need(DebugActiveProcess(pid), "trial-debug-attach");
    attached = true;
    need(DebugSetProcessKillOnExit(FALSE), "trial-kill-policy");
    sourceEntryAddress = client.base + nativeProfile->publisherRva;
    const auto images = imagesLive(pid);
    bool started = false, breaking = false, queryComplete = false;
    ULONGLONG armedTick = 0;
    unsigned events = 0;
    std::string reason;
    const auto attachTick = GetTickCount64();
    for (;;) {
      const auto now = GetTickCount64();
      if (started && !current() && !breaking) {
        enterCleanup();
        need(DebugBreakProcess(process.h), "cancel-cleanup-stop");
        breaking = true; reason = "cancelled";
      }
      if (started && !queryComplete && query.completed()) { query.check(); queryComplete = true; }
      if (started && !breaking && (queryComplete || now - armedTick >= 8000)) {
        need(DebugBreakProcess(process.h), "restore-stop-request");
        breaking = true;
        reason = queryComplete ? "query-completed" : "collection-deadline";
      }
      need(now - attachTick < 20000, "trial-watch-total-bound");
      DEBUG_EVENT e{};
      if (!WaitForDebugEvent(&e, 25)) {
        need(GetLastError() == ERROR_SEM_TIMEOUT, "trial-wait");
        continue;
      }
      pending = e;
      pendingNow = true;
      pendingStatus = e.dwDebugEventCode == EXCEPTION_DEBUG_EVENT
                          ? DBG_EXCEPTION_NOT_HANDLED
                          : DBG_CONTINUE;
      ++events;
      closeEventFile(e);
      need(e.dwProcessId == pid, "single-debuggee-scope");
      if (e.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT ||
          e.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT) {
        SavedThread t;
        t.tid = e.dwThreadId;
        t.handle = duplicate(e.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT
                                 ? e.u.CreateProcessInfo.hThread
                                 : e.u.CreateThread.hThread);
        threads.push_back(std::move(t));
        if (!gCleanupMode) armChecked(threads.back(), flagAddress);
      }
      if (e.dwDebugEventCode == EXIT_THREAD_DEBUG_EVENT)
        for (auto &t : threads)
          if (t.tid == e.dwThreadId)
            t.restored = true;
      need(e.dwDebugEventCode != EXIT_PROCESS_DEBUG_EVENT,
           "steam-exited-during-trial");
      DWORD continueStatus = DBG_CONTINUE;
      bool finish = false;
      if (e.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
        const auto code = e.u.Exception.ExceptionRecord.ExceptionCode;
        if (code == EXCEPTION_BREAKPOINT && e.u.Exception.dwFirstChance && !started) {
          initialBreakpointSeen = true;
          pendingStatus = DBG_CONTINUE;
          need(owned(), "cohort-at-install");
          H snap(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
          THREADENTRY32 te{};
          te.dwSize = sizeof te;
          need(snap.good() && Thread32First(snap.h, &te), "thread-census");
          unsigned count = 0;
          do {
            if (te.th32OwnerProcessID == pid) {
              ++count;
              const auto it =
                  std::find_if(threads.begin(), threads.end(), [&](auto &t) {
                    return t.tid == te.th32ThreadID && t.armed && !t.restored;
                  });
              need(it != threads.end(), "every-thread-armed");
              CONTEXT c{};
              c.ContextFlags = CONTEXT_CONTROL;
              need(GetThreadContext(it->handle.h, &c),
                   "install-thread-context");
              need(c.Rip < getter || c.Rip >= getter + 14,
                   "getter-instruction-not-in-flight");
            }
          } while (Thread32Next(snap.h, &te));
          std::array<unsigned char, 14> check{};
          readExact(process.h, getter, check.data(), check.size());
          need(check == original, "getter-still-original-at-install");
          requireTargetMutation();
    need(VirtualProtectEx(process.h, reinterpret_cast<void *>(getter), 14,
                                PAGE_EXECUTE_READWRITE, &originalProtection),
               "install-code-protect");
          patched = true;
          writeExact(process.h, getter, packet.prepared.detourBytes, 14,
                     &entryEverExposed);
          need(FlushInstructionCache(process.h,
                                     reinterpret_cast<void *>(getter), 14),
               "install-code-cache");
          DWORD ignored = 0;
          requireTargetMutation();
    need(VirtualProtectEx(process.h, reinterpret_cast<void *>(getter), 14,
                                originalProtection, &ignored),
               "install-code-original-protection");
          readExact(process.h, getter, check.data(), check.size());
          need(!std::memcmp(check.data(), packet.prepared.detourBytes, 14),
               "installed-entry-readback");
          started = true;
          armedTick = GetTickCount64();
          out["getterEntryPatched"] = true;
          out["threadsAtInstall"] = count;
          publish(J{{"event", "installed"},
                         {"nonce", nonce},
                         {"gamePid", game.pid}});
          query.fire();
        } else if (code == EXCEPTION_SINGLE_STEP && started) {
          auto it = std::find_if(threads.begin(), threads.end(), [&](auto &t) {
            return t.tid == e.dwThreadId && !t.restored;
          });
          need(it != threads.end(), "trial-watch-thread");
          CONTEXT c{};
          c.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
          need(GetThreadContext(it->handle.h, &c), "trial-watch-context");
          if ((c.Dr6 & 0xf) == 2) {
            std::uint64_t full = 0;
            need(c.Rip == sourceEntryAddress && read(c.Rdx, &full, 8),
                 "source-entry-exact");
            if (full == gameId) {
              J row = {{"elapsedMs", GetTickCount64() - armedTick},
                       {"tid", e.dwThreadId},
                       {"managerMatched", c.Rcx == manager},
                       {"runningArgument", (c.R8 & 0xff) != 0}};
              out["sourceEntries"].push_back(row);
            }
            c.EFlags |= 0x10000u;
          } else {
            need((c.Dr6 & 0xf) == 1, "watched-write-only");
            DWORD flags = 0;
            need(read(flagAddress, &flags, 4), "flags-at-write");
            out["writes"].push_back(
                J{{"elapsedMs", GetTickCount64() - armedTick},
                  {"tid", e.dwThreadId},
                  {"flags", flags},
                  {"postWriteInstruction", locationLive(c.Rip, images)}});
          }
          c.Dr6 &= ~0xfULL;
          requireTargetMutation();
          need(SetThreadContext(it->handle.h, &c), "clear-trial-dr6");
          pendingStatus = DBG_CONTINUE;
        } else if (code == EXCEPTION_BREAKPOINT && e.u.Exception.dwFirstChance && breaking) {
          pendingStatus = DBG_CONTINUE;
          finish = true;
        } else if (threadname_policy::classify(
                       threadname_policy::Architecture::x64, code,
                       e.u.Exception.dwFirstChance,
                       e.u.Exception.ExceptionRecord.ExceptionFlags,
                       e.u.Exception.ExceptionRecord.NumberParameters,
                       e.u.Exception.ExceptionRecord.ExceptionInformation)
                       .recognized()) {
          continueStatus = DBG_EXCEPTION_NOT_HANDLED;
          out["threadNameExceptionsForwarded"] =
              out.value("threadNameExceptionsForwarded", 0u) + 1;
        } else {
          continueStatus = DBG_EXCEPTION_NOT_HANDLED;
          reason = "unexpected-exception-forwarded";
          out["unexpectedExceptionCode"] = code;
          finish = true;
        }
      }
      if (finish) {
        enterCleanup();
        restorationOrDetachStarted = true;
        restoreEntry();
        bool noParkedHook = true;
        for (auto &t : threads) {
          if (t.restored || WaitForSingleObject(t.handle.h, 0) == WAIT_OBJECT_0)
            continue;
          CONTEXT c{};
          c.ContextFlags = CONTEXT_CONTROL;
          need(GetThreadContext(t.handle.h, &c), "restore-thread-context");
          const auto tramp =
              reinterpret_cast<std::uint64_t>(packet.prepared.trampoline);
          if ((c.Rip >= dllBase && c.Rip < dllBase + dllSize) ||
              (c.Rip >= tramp && c.Rip < tramp + 14))
            noParkedHook = false;
        }
        out["noThreadParkedInHookModuleAtRestore"] = noParkedHook;
        out["hookFrameQuiescenceProven"] = false;
        out["queryCompletedBeforeRestore"] = queryComplete;
        restoreStopped(threads);
        out["debugRegistersRestored"] = true;
        continuation(e, continueStatus);
        pendingNow = false;
        break;
      }
      continuation(e, continueStatus);
      pendingNow = false;
    }
    need(DebugActiveProcessStop(pid), "trial-debug-detach");
    attached = false;
    BOOL detachedCheck = TRUE;
    need(CheckRemoteDebuggerPresent(process.h, &detachedCheck) && !detachedCheck, "trial-detach-confirmation");
    restorationOrDetachStarted = false;
    out["debuggerDetached"] = true;
    need(controlCall(tr::Op::Disarm) == 0, "trial-disarm-after-restore");
    out["statusAfterDisarm"] = statusJson(packet);
    out["disarmConfirmed"] = true;
    out["publisherCalls"] = packet.telemetry.publisherCalls;
    // inFlight excludes pre-increment entrants and post-decrement Hook
    // epilogues. RIP outside this DLL does not exclude a return address into
    // it. Retain it.
    out["hookFrameQuiescenceProven"] = false;
    out["unloadUnsupported"] =
        "hook-frames-and-parked-returns-not-proven-quiescent";
    need(packet.status.state != osd::State::Armed &&
             packet.status.state != osd::State::Running,
         "disarmed-state-required-for-dependency-retention");
    need(packet.status.trampolineAllocated == 1,
         "retained-trampoline-required");
    out["mutationResult"] =
        J{{"publisherCalls", packet.telemetry.publisherCalls},
          {"publisherReturned", packet.telemetry.publisherReturned != 0},
          {"callbackCompleted", packet.status.callbackCompleted},
          {"callbackRejected", packet.status.callbackRejected},
          {"callbackAborted", packet.status.callbackAborted},
          {"guardBefore", packet.telemetry.guardBefore != 0},
          {"guardAfter", packet.telemetry.guardAfter != 0}};
    out["entryRestored"] = !patched && out.value("getterEntryRestored", false);
    out["disarmConfirmed"] = true;
    out["dependencyRetained"] = true;
    out["trampolineRetained"] = true;
    out["dllUnloadAttempted"] = false;
    out["dependencyReleasePolicy"] = "next-normal-steam-process-exit";
    packetMemory.release();
    out["packetMemoryReleased"] = true;
    BOOL after = TRUE;
    need(CheckRemoteDebuggerPresent(process.h, &after) && !after,
         "trial-final-no-debugger");
    out["sameOriginalCohortAtExit"] = owned();
    out["finalNativeApp"] = app::observe(aq, uiRead, owned);
    out["finalRecord"] = ymcc::steamrecord::observe(rq, recordRead, owned);
    out["debugEvents"] = events;
    out["endReason"] = reason;
    out["status"] = reason == "query-completed"
                        ? "trial-finished-dependencies-retained"
                        : "partial-dependencies-retained";
    out["trialDllMayRemainLoaded"] = loaded;
    }, "trial-native-exception");
  } catch (const std::exception &e) {
    out["status"] = "failed";
    out["reason"] = e.what();
    if (const auto* diagnostic = dynamic_cast<const DiagnosticFailure*>(&e))
      out["failureDiagnostic"] = diagnostic->detail;
    bool recoveryKnown = true;
    if (entryEverExposed && restorationOrDetachStarted) {
      out["debuggerAttachedOrUncertain"] = true;
      ownerHeld(process.h, sessionNonce, out, pendingNow);
      recoveryKnown = false;
    }
    const auto recoverPending = [&] {
      need(pendingNow, "pending-event-required");
      need(pending.dwProcessId == pid, "recovery-event-scope");
      if (pending.dwProcessId == pid) {
        // A failed observation can still be our hardware single-step. Verify
        // ownership before restoring DRs; forwarding that event could fault
        // Steam.
        if (pending.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
            pending.u.Exception.ExceptionRecord.ExceptionCode ==
                EXCEPTION_SINGLE_STEP) {
          const auto it =
              std::find_if(threads.begin(), threads.end(), [&](const auto &t) {
                return t.tid == pending.dwThreadId && t.armed && !t.restored;
              });
          if (it != threads.end()) {
            CONTEXT c{};
            c.ContextFlags = CONTEXT_CONTROL | CONTEXT_DEBUG_REGISTERS;
            need(GetThreadContext(it->handle.h, &c) != FALSE,
                 "error-single-step-context");
            const auto causes = c.Dr6 & 0xf;
            if (c.Dr0 == activeWatchAddress && c.Dr1 == sourceEntryAddress &&
                ((causes == 1) || (causes == 2 && c.Rip == sourceEntryAddress)))
              pendingStatus = DBG_CONTINUE;
          }
        }
        enterCleanup();
        restorationOrDetachStarted = true;
        restoreEntry();
        restoreStopped(threads);
      }
      continuation(pending, pendingStatus);
      pendingNow = false;
    };
    try {
      if (!recoveryKnown) throw std::runtime_error("owner-held-target-exited");
      if (pendingNow)
        recoverPending();
      if (attached) {
        const bool watchDirty =
            std::any_of(threads.begin(), threads.end(),
                        [](const auto &t) { return t.armed && !t.restored; });
        if (patched || watchDirty || !initialBreakpointSeen) {
          if (initialBreakpointSeen) {
            enterCleanup();
            need(DebugBreakProcess(process.h) != FALSE, "error-restoration-stop-request");
          }
          bool stopped = false;
          const auto begin = GetTickCount64();
          while (GetTickCount64() - begin < 4000) {
            DEBUG_EVENT event{};
            if (!WaitForDebugEvent(&event, 50)) {
              need(GetLastError() == ERROR_SEM_TIMEOUT,
                   "error-restoration-stop-wait");
              continue;
            }
            pending = event;
            pendingNow = true;
            pendingStatus = event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT
                                ? DBG_EXCEPTION_NOT_HANDLED
                                : DBG_CONTINUE;
            closeEventFile(event);
            if (event.dwProcessId == pid) {
              const bool requestedBreakpoint = event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
                  event.u.Exception.dwFirstChance &&
                  event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT;
              if (requestedBreakpoint) { pendingStatus = DBG_CONTINUE; initialBreakpointSeen = true; }
              recoverPending();
              // DebugBreakProcess may first report its new thread. Consume its
              // actual breakpoint before detach so none is left for the app.
              if (requestedBreakpoint) { stopped = true; break; }
              continue;
            }
            throw std::runtime_error("single-debuggee-error-scope");
          }
          need(stopped, "error-restoration-stop-unavailable-unsupported");
        }
        need(!patched, "error-entry-restoration-required");
        enterCleanup();
        need(DebugActiveProcessStop(pid) != FALSE, "error-detach-failed");
        attached = false;
        BOOL detachedCheck = TRUE;
        need(CheckRemoteDebuggerPresent(process.h, &detachedCheck) && !detachedCheck, "error-detach-confirmation");
        restorationOrDetachStarted = false;
        out["errorDetach"] = true;
      }
    } catch (const std::exception &cleanup) {
      recoveryKnown = false;
      if (out.value("status", "") != "unsupported-target-exited") out["status"] = "unsupported";
      out["cleanupError"] = cleanup.what();
      out["automaticContinuationSuppressed"] = pendingNow;
      out["pendingDebugEventUnresolved"] = pendingNow;
      // Stay on this debugger's owning thread; no unwinding while Steam lives.
      if (out.value("status", "") != "unsupported-target-exited")
        ownerHeld(process.h, sessionNonce, out, pendingNow);
    }
    if (recoveryKnown && preparedTrial && !attached && !patched &&
               !managementThread.good() && !controlUnknown) {
      try {
        // Cleanup of this invocation's known control is independent of a
        // revoked game/rules/power guard. It still pins the original Steam.
        enterCleanup();
        const auto code = controlCall(tr::Op::Disarm);
        out["errorDisarmCode"] = code;
        out["errorStatus"] = statusJson(packet);
        need(code == 0 && packet.status.state != osd::State::Armed &&
             packet.status.state != osd::State::Running, "error-disarm-failed");
        out["disarmConfirmed"] = true;
      } catch (const std::exception &disarm) {
        out["errorDisarmUnknown"] = true;
        out["errorDisarmReason"] = disarm.what();
      }
    }
    if (process.good() && WaitForSingleObject(process.h, 0) == WAIT_TIMEOUT &&
        (managementThread.good() || controlUnknown ||
         ((preparedTrial || armAttempted) && !out.value("disarmConfirmed", false)))) {
      ownerHeld(process.h, sessionNonce, out, pendingNow);
      recoveryKnown = false;
    }
    if (recoveryKnown && !managementThread.good()) {
      try {
        packetMemory.release();
        pathMemory.release();
        out["packetMemoryReleased"] = true;
        out["pathMemoryReleased"] = true;
      } catch (const std::exception &memory) {
        out["inputMemoryReleaseError"] = memory.what();
      }
    }
    out["managementCallUnresolved"] = managementThread.good();
    out["controlOutcomeUnknown"] = controlUnknown;
    out["trialDllMayRemainLoaded"] =
        loaded || (loadAttempted && !loadCompleted);
    out["entryMayRemainPatched"] = patched;
    out["armAttempted"] = armAttempted;
    out["entryEverExposed"] = entryEverExposed;
    out["packetMemoryRetained"] = packetMemory.address != 0;
    out["pathMemoryRetained"] = pathMemory.address != 0;
  }
  publish(J{{"event", "result"}, {"result", out}});
  return out;
}

#endif // production process/debugger operations
