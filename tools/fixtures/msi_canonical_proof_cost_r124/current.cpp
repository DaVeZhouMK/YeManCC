#include "common.hpp"
namespace currentActual {
struct RequiredFields {
#include "current-fields.inc"
};
static RequiredFields g_msiDinput;
// Controlled matching context, no OS/device/Host access. The actual power
// accessor and actual Apply function execute against these atomics.
static std::atomic<unsigned long long> g_powerGeneration{17},g_msiFeedbackModeGeneration{23},g_msiMainSourceRevision{31};
static std::atomic<unsigned> g_xinputTopologyEpoch{41};
#include "current-functions.inc"
}
using namespace currentActual;
static void reset(){
 g_msiDinput.bindCount=17;g_msiDinput.canonicalSeq=0;g_msiDinput.canonicalBtnTrig=0;g_msiDinput.canonicalThumbs=0;g_msiDinput.canonicalValid=false;
 g_msiDinput.canonicalReadSequence=0;g_msiDinput.canonicalReadStartedTick=0;g_msiDinput.canonicalReadCompletedTick=0;g_msiDinput.canonicalReadPower=0;g_msiDinput.canonicalReadMode=0;g_msiDinput.canonicalReadSourceRevision=0;g_msiDinput.canonicalReadTopology=0;
}
static bool publish(const WorkItem* w,Output*){msiDinputPublishCanonicalFrame(w->pad,&w->proof);return true;}
static bool snapshot(const WorkItem*,Output* o){return msiDinputCanonicalSnapshot(o->sample.pad,&o->producer);}
static bool apply(const WorkItem*,Output* o){o->sample.readContextStable=true;msiDinputApplyCanonicalReadProof(o->sample,o->producer);return true;}
static bool pipeline(const WorkItem* w,Output* o){
 o->sample.readContextStable=true;
 msiDinputPublishCanonicalFrame(w->pad,&w->proof);
 if(!msiDinputCanonicalSnapshot(o->sample.pad,&o->producer))return false;
 msiDinputApplyCanonicalReadProof(o->sample,o->producer);
 return true;
}
static StateCounts counts(){return {g_msiDinput.canonicalSeq.load(),g_msiDinput.canonicalReadSequence.load()};}
extern const BenchApi currentApi={"current-proof",reset,publish,snapshot,pipeline,apply,counts,sizeof(RequiredFields)};