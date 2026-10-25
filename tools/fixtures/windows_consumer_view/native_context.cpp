// The context publisher is the exact BUS function extracted from main.cpp.
static bool g_inputCaptureReady=true;
struct FixtureMachine{std::string deviceClass=model().deviceClass;};static FixtureMachine g_machineIdentity;
struct FixtureSelector{std::string selectorId="controlled-selector",gyroMatrixId=model().gyroMatrixId,
 accelMatrixId=model().accelMatrixId,matrixSourceSha256=std::string(64,'c');};static FixtureSelector g_hcMotionSelector;
static bool inputCaptureMotionSelectorBound(){return fixtureModelBound;}
static std::string inputCaptureCalibrationStorageKey(const std::string& id){
 if(!t_windowsProcessingView||t_windowsProcessingView->privateSource||!t_windowsProcessingStamp||!ymcc::windows_motion::current(*t_windowsProcessingStamp)||
     t_windowsProcessingView->metadata.gyroDeviceId!=g_sensorMeta.gyroDeviceId)throw std::runtime_error("context borrowed a private/default-stale calibration view");
 return "pinned-default-key:"+id;
}
// @@ACTUAL_CONTEXT_PUBLISHER@@
static void contextTests(){
 ownerReset();auto sourceState=ownerSelect();onPump();auto oldPrivate=inputCaptureWindowsSensorViewSnapshot();auto built=ymcc::windows_motion::forProcessing(oldPrivate.submission,ownerCpu);
 check(sourceState.value("inputProcessingCommitted",false)&&ymcc::windows_motion::currentForOutput(built),"context revocation witness starts with a committed processed source");
 std::atomic_load(&g_windowsSelectionWorkerContext)->active->store(false);inputCaptureWindowsSelectionPublishContext();
 check(!ymcc::windows_motion::currentForOutput(built)&&ownerCpu->load(),"BUS context replacement revokes already-built output before STA polling or CPU retirement");
 check(!inputCaptureWindowsSensorViewSnapshot().coherent,"new same-fingerprint context cannot borrow the old private projection");
 ownerReset();std::atomic_store(&g_windowsSelectionWorkerContext,std::shared_ptr<const WindowsSelectionWorkerContext>{});
 inputCaptureWindowsSelectionPublishContext();auto first=std::atomic_load(&g_windowsSelectionWorkerContext);
 check(inputCaptureWindowsSelectionContextCurrent(first)&&first->defaultKey=="pinned-default-key:"+g_sensorMeta.gyroDeviceId,"actual BUS publisher exports bound default identity and calibration key");
 for(unsigned i=0;i<512;++i)inputCaptureWindowsSelectionPublishContext();
 check(std::atomic_load(&g_windowsSelectionWorkerContext)==first,"stable sampling reuses immutable context without rebuilding JSON/model each tick");
 ++g_sensorMeta.gyroIntervalMs;inputCaptureWindowsSelectionPublishContext();check(std::atomic_load(&g_windowsSelectionWorkerContext)==first,"report interval metadata does not change processing ownership context");
 {WindowsSensorView privateScope;privateScope.privateSource=true;privateScope.metadata.gyroDeviceId="foreign-private-source";
  ymcc::sensors::view::ProcessingScope<InputCaptureSensorMeta> scope(privateScope,t_windowsProcessingMeta,t_windowsProcessingStamp,&t_windowsProcessingView);
  g_sensorMeta.gyroDeviceId="ACPI#updated#1#{GYRO}";inputCaptureWindowsSelectionPublishContext();
  auto current=std::atomic_load(&g_windowsSelectionWorkerContext);
  check(current!=first&&!first->active->load()&&current->defaultKey=="pinned-default-key:"+g_sensorMeta.gyroDeviceId,"publisher explicitly pins real default calibration under a surrounding private TLS view");}
 first=std::atomic_load(&g_windowsSelectionWorkerContext);first->active->store(false);inputCaptureWindowsSelectionPublishContext();
 check(std::atomic_load(&g_windowsSelectionWorkerContext)!=first,"same fingerprint with revoked ownership mints a new immutable context");
 fixtureModelBound=false;inputCaptureWindowsSelectionPublishContext();check(!inputCaptureWindowsSelectionContextCurrent(std::atomic_load(&g_windowsSelectionWorkerContext)),"actual model binding revocation closes request context");
 fixtureModelBound=true;inputCaptureWindowsSelectionPublishContext();check(inputCaptureWindowsSelectionContextCurrent(std::atomic_load(&g_windowsSelectionWorkerContext)),"valid model binding can create a new request context");
 g_powerLifecycle=PowerLifecycle::Suspended;inputCaptureWindowsSelectionPublishContext();check(!inputCaptureWindowsSelectionContextCurrent(std::atomic_load(&g_windowsSelectionWorkerContext)),"suspended lifecycle cannot publish an admissible request context");
 g_powerLifecycle=PowerLifecycle::Ready;inputCaptureWindowsSelectionPublishContext();first=std::atomic_load(&g_windowsSelectionWorkerContext);
 ++g_windowsSourceModuleLifetime;check(!inputCaptureWindowsSelectionContextCurrent(first),"native source lifetime change immediately invalidates prior bridge");
 inputCaptureWindowsSelectionPublishContext();check(std::atomic_load(&g_windowsSelectionWorkerContext)!=first,"new native source lifetime gets a new BUS snapshot");
 g_inputCaptureReady=false;inputCaptureWindowsSelectionPublishContext();check(!inputCaptureWindowsSelectionContextCurrent(std::atomic_load(&g_windowsSelectionWorkerContext)),"disabled capture closes context even while model data is retained");g_inputCaptureReady=true;
 inputCaptureWindowsSelectionLifecycle();resetView();
}
