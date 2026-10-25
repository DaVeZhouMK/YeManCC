#include "common.hpp"
namespace baselineActual {
struct RequiredFields {
#include "baseline-fields.inc"
};
static RequiredFields g_msiDinput;
#include "baseline-functions.inc"
}
using namespace baselineActual;
static void reset(){g_msiDinput.bindCount=17;g_msiDinput.canonicalSeq=0;g_msiDinput.canonicalBtnTrig=0;g_msiDinput.canonicalThumbs=0;g_msiDinput.canonicalValid=false;}
// The enclosing wrappers are separate TUs with /GL-; source implementations
// remain unchanged and are free to inline inside the same wrapper boundary.
static bool publish(const WorkItem* w,Output*){msiDinputPublishCanonicalFrame(w->pad);return true;}
static bool snapshot(const WorkItem*,Output* o){return msiDinputCanonicalSnapshot(o->sample.pad);}
static bool pipeline(const WorkItem* w,Output* o){
 o->sample.readContextStable=true;
 msiDinputPublishCanonicalFrame(w->pad);
 return msiDinputCanonicalSnapshot(o->sample.pad);
}
static StateCounts counts(){return {g_msiDinput.canonicalSeq.load(),0};}
extern const BenchApi baselineApi={"r123-baseline",reset,publish,snapshot,pipeline,nullptr,counts,sizeof(RequiredFields)};