import fs from 'node:fs';
export function refinePhysicalBridge(file){
const text=fs.readFileSync(file,'utf8'),before='static std::string W2U(const std::wstring& s){return std::string(s.begin(),s.end());}',after='static std::string W2U(const std::wstring& s){std::string value;value.reserve(s.size());for(const auto c:s){if(c<0||c>127)throw std::runtime_error("fixture-ASCII-path-required");value.push_back(static_cast<char>(c));}return value;}';
if(text.split(before).length===2)fs.writeFileSync(file,text.replace(before,after));else if(text.split(after).length!==2)throw Error('Controlled ASCII fixture terminal drift');
}
