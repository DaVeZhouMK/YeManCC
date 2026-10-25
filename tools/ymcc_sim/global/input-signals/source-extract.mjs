/** Structural source extraction only. Emitted C++ snippets are byte-identical
 * to current product source; comments and literals cannot balance delimiters. */
import crypto from 'node:crypto';
const hash=text=>crypto.createHash('sha256').update(text).digest('hex');
function skip(source,i){
 if(source.startsWith('//',i)){const n=source.indexOf('\n',i+2);return n<0?source.length:n;}
 if(source.startsWith('/*',i)){const n=source.indexOf('*/',i+2);if(n<0)throw Error('UNTERMINATED_CPP_COMMENT');return n+2;}
 if(source.startsWith('R"',i)){const n=source.indexOf('(',i+2),delimiter=source.slice(i+2,n);if(n<0||delimiter.length>16)throw Error('INVALID_CPP_RAW_LITERAL');const end=source.indexOf(')'+delimiter+'"',n+1);if(end<0)throw Error('UNTERMINATED_CPP_RAW_LITERAL');return end+delimiter.length+2;}
 if(source[i]==='"'||source[i]==="'"){const q=source[i++];while(i<source.length){if(source[i]==='\\'){i+=2;continue;}if(source[i++]===q)return i;}throw Error('UNTERMINATED_CPP_LITERAL');}
 return i;
}
export function balancedEnd(source,start,open='{',close='}'){
 if(source[start]!==open)throw Error('CPP_START_DELIMITER_MISSING');let depth=0;
 for(let i=start;i<source.length;i++){const next=skip(source,i);if(next!==i){i=next-1;continue;}if(source[i]===open)depth++;if(source[i]===close&&!--depth)return i+1;}
 throw Error('CPP_END_DELIMITER_MISSING');
}
function unique(source,needle){const at=source.indexOf(needle);if(at<0||source.indexOf(needle,at+needle.length)>=0)throw Error('CPP_ANCHOR_NOT_UNIQUE:'+needle);return at;}
export function extractNativeFollowup(source,{inputShortcutHeader}={}){
 const rows=[];
 const add=(name,start,end,meaning)=>{const text=source.slice(start,end);rows.push({name,text,startLine:source.slice(0,start).split('\n').length,sha256:hash(text),meaning});return text;};
 const func=(name,signature)=>{const at=unique(source,signature),from=signature.startsWith('static ')?balancedEnd(source,source.indexOf('(',at),'(',')'):at,brace=source.indexOf('{',from);return add(name,at,balancedEnd(source,brace),'actual-product-function');};
 const declaration=(name,signature)=>{const at=unique(source,signature);return add(name,at,source.indexOf(';',at)+1,'actual-product-declaration');};
 declaration('producerEnum','enum class OemProducerId : unsigned');
 declaration('producerCount','static constexpr size_t kOemProducerCount =');
 declaration('oemBits','static constexpr uint64_t kOemBitClaw =');
 func('producerTokens','struct OemProducerTokens {');rows.at(-1).text+=';';rows.at(-1).sha256=hash(rows.at(-1).text);
 func('ruleStruct','struct NativeKeyboardShortcutRule {');rows.at(-1).text+=';';rows.at(-1).sha256=hash(rows.at(-1).text);
 func('dispatch','static bool nativeKeyboardShortcutDispatch(');
 func('desktopMapping','static bool nativeInputShortcutDesktopKey(const std::string& code, WORD& key) {');
 func('actionMapping','static bool nativeKeyboardShortcutVirtualKey(');
 func('inputFlags','static DWORD nativeKeyboardShortcutInputFlags(');
 func('maskMapping','static bool nativeKeyboardShortcutMaskBit(');
 func('requiredMask','static bool nativeKeyboardShortcutRequiredMask(');
 func('allInputs','static bool nativeKeyboardShortcutAllInputs(');
 func('desktopKeys','static bool nativeInputShortcutDesktopKeys(const json& inputs, std::vector<WORD>& keys) {');
 if(inputShortcutHeader){const at=unique(inputShortcutHeader,'inline std::string inputShortcutString('),open=balancedEnd(inputShortcutHeader,inputShortcutHeader.indexOf('(',at),'(',')'),end=balancedEnd(inputShortcutHeader,inputShortcutHeader.indexOf('{',open)),text=inputShortcutHeader.slice(at,end);rows.push({name:'inputString',text,startLine:inputShortcutHeader.slice(0,at).split('\n').length,sha256:hash(text),path:'native/input_shortcut_toggle.h',meaning:'actual-product-string-reader'});}
 const enterAt=unique(source,'rule.forceExtendedKey = key->get<std::string>() == "NumpadEnter";');
 add('numpadEnterAdmission',enterAt,source.indexOf(';',enterAt)+1,'actual-product-admission-statement');
 const clickFunction=unique(source,'static void nativeMsiWmiShortcutDrain() {');
 let clickAt=source.indexOf('appendNativeLifecycleLog("oem-shortcut-click",',clickFunction);
 while(clickAt>=0){const end=balancedEnd(source,source.indexOf('(',clickAt),'(',')')+1;const text=source.slice(clickAt,end);if(text.includes('"actionId"')){add('clickLog',clickAt,end,'actual-product-log-statement-and-status-branches');break;}clickAt=source.indexOf('appendNativeLifecycleLog("oem-shortcut-click",',end);}
 const summaryAt=source.indexOf('\n    {',unique(source,'// OEM 规则清单取证'))+5;
 add('summaryLog',summaryAt,balancedEnd(source,summaryAt),'actual-product-summary-digest-and-log-branch');
 const edgeAnchors=[...source.matchAll(/if \(oemRule\) \{\r?\n {12}const bool released = !matched && rule\.matchedLastTick;/g)];
 if(edgeAnchors.length!==1)throw Error('CPP_ANCHOR_NOT_UNIQUE:OEM_EDGE_RELEASE_BLOCK');
 const edgeAt=edgeAnchors[0].index;
 add('edgeEmitLogs',edgeAt,balancedEnd(source,source.indexOf('{',edgeAt)),'actual-product-edge-or-repeat-emit-condition-and-logs');
 if(!rows.some(r=>r.name==='clickLog'))throw Error('OEM_CLICK_LOG_NOT_FOUND');
 return rows;
}



