inline Facts fixtureHomeInformationRead(std::uint32_t mask,std::wstring_view scenario)noexcept{
    try{
        if(!mask||(mask&~Supported))return {};
        Peer caller;if(!caller.load(GetCurrentProcessId()))return {};
        const auto own=std::filesystem::path(caller.path);const auto leaf=pathKey(own.filename().wstring());
        const bool home=mask==StartupWindow&&leaf==L"yemangaminghome.exe"&&gamingHomePackagePeer(caller);
        if(!home&&(leaf!=L"customsteamlibrary.exe"&&leaf!=L"steamartworklab.exe"))return {};
        if((mask&StartupWindow)&&!home)return {};
        const auto expected=home?HomeInformationFixture::expectedProvider():pathKey((own.parent_path().parent_path()/L"YeManCC.exe").wstring());
        const auto endpoint=findProvider(caller,expected);
        DWORD pid=0;std::uint64_t birth=0;
        if(scenario.starts_with(L"bound-owner-")){
            if(!endpoint.current())throw std::runtime_error("fixture provider unavailable");
            pid=endpoint.peer->pid;birth=endpoint.peer->birth;
            if(scenario==L"bound-owner-wrong-pid")pid=caller.pid;
            if(scenario==L"bound-owner-wrong-birth")++birth;
        }
        return information_read_detail::readEndpoint(caller,endpoint,mask,pid,birth);
    }catch(...){return {};}
}
