// The processing boundary is controlled; the source guard/getters below are production bodies.
struct FixtureProcessingScope {
 const ymcc::windows_motion::Stamp* oldStamp=t_windowsProcessingStamp;
 const InputCaptureSensorMeta* oldMeta=t_windowsProcessingMeta;
#if YMCC_FIXTURE_PROFILE_CONTEXT
 const WindowsSensorView* oldView=t_windowsProcessingView;
#endif
 FixtureProcessingScope(const ymcc::windows_motion::Stamp& stamp,const InputCaptureSensorMeta& meta){t_windowsProcessingStamp=&stamp;t_windowsProcessingMeta=&meta;
#if YMCC_FIXTURE_PROFILE_CONTEXT
 t_windowsProcessingView=nullptr;
#endif
 }
#if YMCC_FIXTURE_PROFILE_CONTEXT
 explicit FixtureProcessingScope(const WindowsSensorView& view):FixtureProcessingScope(view.submission,view.metadata){t_windowsProcessingView=&view;}
#endif
 ~FixtureProcessingScope(){t_windowsProcessingStamp=oldStamp;t_windowsProcessingMeta=oldMeta;
#if YMCC_FIXTURE_PROFILE_CONTEXT
 t_windowsProcessingView=oldView;
#endif
 }
};
