inline std::optional<bool> acquisition(const std::wstring&powerControl){
    const auto data=readBytes((fs::path(powerControl)/L"yeman-settings.json").wstring(),16777216,true);if(!data)return {};
    // Keep one bounded read buffer. Skipping a BOM must not clone or shift the
    // entire unified settings document on each startup/recovery transaction.
    auto first=data->cbegin();if(data->compare(0,3,"\xef\xbb\xbf")==0)first+=3;
    Json document;try{document=Json::parse(first,data->cend());}catch(...){throw Error(8,"Unified YMCC settings are malformed");}require(document.is_object(),"Unified YMCC settings must be an object");
    if(!document.contains("rtss")||document["rtss"].is_null())return {};const auto& rtss=document["rtss"];require(rtss.is_object(),"Unified rtss settings must be an object");if(!rtss.contains("monitorAcquisition")||rtss["monitorAcquisition"].is_null())return {};const auto&p=rtss["monitorAcquisition"];
    require(p.is_object()&&p.contains("version")&&p["version"].is_number_integer()&&p["version"]==1&&p.contains("enabled")&&p["enabled"].is_boolean(),"Unsupported HWiNFO acquisition settings");if(!p["enabled"].get<bool>())return {};require(p.contains("snapshotPolling")&&p["snapshotPolling"].is_boolean(),"HWiNFO acquisition choices must be booleans");return p["snapshotPolling"].get<bool>();
}
