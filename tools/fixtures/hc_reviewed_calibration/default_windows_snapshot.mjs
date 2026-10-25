import fs from 'node:fs';import path from 'node:path';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
// Reuse the exact default branch and exact processor readback in older sink
// regressions. Metadata and choice stay controlled here; private projections,
// metadata pinning and final Host/DSU admission execute in the consumer fixture.
export function defaultWindowsSnapshot(source,repo){
 if(!source.includes('static WindowsSensorView inputCaptureWindowsSensorViewSnapshot()'))return null;
 const fn=extract(source,'inputCaptureWindowsSensorViewSnapshot').code;
 const from=fn.indexOf('    const auto owner=g_inputCaptureSink;'),end=fn.lastIndexOf('    return result;');
 const begin=source.search(/^    (?:const )?auto windowsSensorView=inputCaptureWindowsSensorViewSnapshot\(\);/m),last=source.indexOf('    const auto capabilityPower =',begin);
 if(from<0||end<from||begin<0||last<begin)throw Error('Actual default snapshot/readback moved');
 const snapshot=source.slice(begin,last);
 const declarations=`
#include "${path.join(repo,'native/windows_source_view.h').replaceAll('\\','/')}"
struct InputCaptureSensorMeta {};
using WindowsSensorView=ymcc::sensors::view::Snapshot<InputCaptureSensorMeta>;
static thread_local const WindowsSensorView* t_windowsProcessingView=nullptr;
static std::mutex g_sensorMetaMx;static InputCaptureSensorMeta g_sensorMeta;
static thread_local const InputCaptureSensorMeta* t_windowsProcessingMeta=nullptr;
static thread_local const ymcc::windows_motion::Stamp* t_windowsProcessingStamp=nullptr;
struct FixtureChoice {std::atomic<std::uint64_t> activeBindingGeneration{1};};
static auto g_windowsChoiceCounter=std::make_shared<FixtureChoice>();
static std::shared_ptr<const ymcc::windows_motion::PublicationFence> g_windowsDefaultPublicationFence;
// Explicit controlled CPU boundary: this fixture tests raw numeric capture only.
static bool inputCaptureLegionControllerMotionRoute(){return false;}
static bool inputCaptureWindowsProcessingAdopt(WindowsSensorView& view){return ymcc::sensors::view::current(view);}
static void inputCaptureWindowsProcessingRetire(){}
static bool inputCaptureWindowsProcessingFrameCurrent(const WindowsSensorView& view){return ymcc::sensors::view::current(view);}

static WindowsSensorView inputCaptureWindowsSensorViewSnapshot(){
 WindowsSensorView result;
${fn.slice(from,end)}
 return result;
}
`;
 return {snapshot,declarations,inputs:['windows_source_view.h','windows_motion_submission.h'].map(n=>path.join(repo,'native',n)),actualDefaultBranch:fn.slice(from,end)};
}
