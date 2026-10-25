-- YMCC is a UI/IPC adapter. Scans, pause/resume and result storage belong to CE.
-- Do not assign any scan preferences, write registry settings or call pause/unpause.
local C=YMCC_CE_SESSION
local f=getMainForm()
local function encode(v)
 local kind=type(v)
 if kind=='string' then return '"'..v:gsub('\\','\\\\'):gsub('"','\\"'):gsub('\n','\\n'):gsub('\r','\\r'):gsub('\t','\\t')..'"' end
 if kind=='number' or kind=='boolean' then return tostring(v) end
 if kind~='table' then return 'null' end
 local parts={}
 if v.__array then for i=1,#v do parts[#parts+1]=encode(v[i]) end;return '['..table.concat(parts,',')..']' end
 for k,x in pairs(v) do if k~='__array' then parts[#parts+1]=encode(k)..':'..encode(x) end end
 return '{'..table.concat(parts,',')..'}'
end
local function exists(p)local h=io.open(p,'rb');if h then h:close();return true end;return false end
local function read(p)local h=io.open(p,'rb');if not h then return nil end;local v=h:read('*a');h:close();return v end
local state={token=C.token,engine='cheat-engine',stage='idle',message='输入当前数字',count=0,rows={__array=true},scanRound=0,searchValue='',lastWriteCount=0,lastWriteValue='',scannedBytes=0,totalBytes=0,scanElapsedMs=0,temporarilyPaused=false,pauseRestored=true,dataType='int32',commandId=''}
local function publish()
 local path=C.root..'\\ce-state.json';local temp=path..'.tmp'
 local h=assert(io.open(temp,'wb'));h:write(encode(state));h:close()
 -- Readers allow missing files during replacement, never a truncated JSON receipt.
 os.remove(path);assert(os.rename(temp,path))
end
local active,started,cancelled,closing=false,0,false,false
local fatalError=nil
local lastLease,leaseAt='',getTickCount()
local function scanner()
 local ms=getCurrentMemscan()
 if not ms or not ms.FoundList then error('CE扫描器未完成初始化，请重新打开简单改数值') end
 return ms
end
local function rowList()
 local fl=scanner().FoundList
 state.count=fl.Count;state.rows={__array=true}
 for i=0,math.min(fl.Count,40)-1 do state.rows[#state.rows+1]={address=string.format('%016X',tonumber(fl.Address[i],16)),value=tostring(fl.Value[i])} end
 return fl
end
local function finish()
 scanner().waitTillDone()
 active=false;state.temporarilyPaused=false;state.pauseRestored=true;state.scanElapsedMs=getTickCount()-started
 if exists(C.root..'\\cancel') then cancelled=true;os.remove(C.root..'\\cancel') end
 if cancelled or fatalError then state.stage='failed';state.count=0;state.rows={__array=true};state.message=fatalError or '搜索已取消';state.scanRound=0
 else rowList();state.stage='ready';state.message=state.count==0 and '这次没有对上，请核对游戏里的数字' or '已记住当前数字';if state.nextScan then state.scanRound=state.scanRound+1 end end
 state.nextScan=nil;publish()
end
local function cancel()
 if active and not cancelled then
  local b=f.findComponentByName('CancelButton')
  if b and b.Enabled then b.doClick();cancelled=true;state.message='正在取消搜索…' end
 end
end
local function command(lines)
 local id,token,action,value=lines[1],lines[2],lines[3],lines[4] or ''
 if token~=C.token or not id:match('^[0-9a-f]+$') or #id~=32 then error('CE 请求身份不匹配') end
 if active then error('CE 正在搜索') end
 if getOpenedProcessID()~=C.pid then error('CE 当前游戏已改变') end
 state.commandId=id
 if action=='first' or action=='next' then
  if not value:match('^[+-]?%d+$') then error('请输入整数') end
  local n=tonumber(value);if not n or n < -2147483648 or n > 2147483647 then error('数值超出4字节整数范围') end
  if action=='next' and value==state.searchValue then error('回游戏让数字变化一次，再输入新数字') end
  if action=='first' and f.btnNextScan.Enabled then f.btnNewScan.doClick() end
  if action=='next' and not f.btnNextScan.Enabled then error('请先完成首次搜索') end
  scanner() -- Fail before dispatching a GUI handler if CE initialization is incomplete.
  if exists(C.root..'\\cancel') then os.remove(C.root..'\\cancel');state.stage='failed';state.message='搜索已取消';state.scanRound=0;state.count=0;state.rows={__array=true};return end
  f.ScanValue.Text=value
  active=true;cancelled=false;started=getTickCount();state.stage='scanning';state.message='正在由CE搜索';state.searchValue=value;state.nextScan=action=='next';state.lastWriteCount=0;state.lastWriteValue='';state.rows={__array=true};state.scannedBytes=0;state.totalBytes=0;state.scanElapsedMs=0
  if action=='first' then state.scanRound=0 end
  if action=='first' then f.btnNewScan.doClick() else f.btnNextScan.doClick() end
 elseif action=='writeAll' then
  if state.stage~='ready' or state.scanRound<1 then error('先让数字变化一次，再搜索后修改') end
  local n=tonumber(value);if not value:match('^[+-]?%d+$') or not n or n < -2147483648 or n > 2147483647 then error('请输入4字节整数范围内的数值') end
  local fl=rowList();if fl.Count<1 or fl.Count>32 then error('范围还比较大，请让数字再变化一次') end
  local addresses,original={},{ }
  for i=0,fl.Count-1 do local a=tonumber(fl.Address[i],16);local before=readInteger(a);if before~=tonumber(state.searchValue) then state.scanRound=0;error('游戏里的数字又变了，请输入最新数字继续搜索') end;addresses[#addresses+1]=a;original[#original+1]=before end
  local touched=0
  local ok,err=pcall(function()for i,a in ipairs(addresses) do if getOpenedProcessID()~=C.pid then error('CE 当前游戏已改变') end;touched=i;if not writeInteger(a,n) or readInteger(a)~=n then error('CE 未能确认数值修改') end end end)
  if not ok then for i=1,touched do writeInteger(addresses[i],original[i]) end;state.scanRound=0;error(err) end
  state.lastWriteCount=#addresses;state.lastWriteValue=value;state.searchValue=value;rowList();for _,row in ipairs(state.rows) do row.value=value end;state.message='已修改并读回确认'
 else error('不支持的CE操作') end
end
local ok,err=pcall(function()
 reloadSettingsFromRegistry()
 f.hide()
 -- Trainer startup skips FormShow. Invoke CE's own initializer without displaying
 -- the form; the host has checked the saved first-run flag before launch.
 -- Auto-attach is session state: this worker may attach only to YMCC's exact PID.
 getAutoAttachList().clear()
 if not getCurrentMemscan() then f.OnShow(f) end
 scanner()
 -- Loading .cetrainer avoids Lua trust/first-run dialogs; NOAUTORUN skips setup add-ons.
 -- Existing settings are read, never overwritten by this adapter.
 openProcess(C.pid)
 if getOpenedProcessID()~=C.pid then error('CE 无法连接锁定游戏') end
 if f.VarType.ItemIndex~=3 or f.ScanType.ItemIndex~=0 then error('CE默认搜索需要为精确数值、4字节，请在CE中确认') end
 state.ceVersion=tostring(getCEVersion());state.pauseByCE=f.cbPauseWhileScanning.Checked;publish()
 local timer=createTimer(nil,false);timer.Interval=150
 timer.OnTimer=function()
  local success,failure=pcall(function()
   local lease=read(C.root..'\\ce-lease.txt') or ''
   if lease~=lastLease and lease:sub(1,#C.token)==C.token then lastLease=lease;leaseAt=getTickCount() end
   if getTickCount()-leaseAt>8000 then closing=true end
   if exists(C.root..'\\stop') or exists(C.root..'\\ce-stop') then closing=true end
   if exists(C.root..'\\cancel') or closing then cancel() end
   if active then
    if f.btnNewScan.Enabled then finish() else
     local progress=scanner().getProgress();if type(progress)=='table' then state.scannedBytes=progress.CurrentlyScanned or 0;state.totalBytes=progress.TotalAddressesToScan or 0 end;state.scanElapsedMs=getTickCount()-started;state.temporarilyPaused=state.pauseByCE;state.pauseRestored=not state.pauseByCE;publish()
    end
   end
   if closing and not active then state.stage='closed';state.message='CE会话已结束';publish();timer.destroy();closeCE();return end
   if not active and not closing then
    local request=read(C.root..'\\ce-command.txt')
    if request then os.remove(C.root..'\\ce-command.txt');local lines={};for line in (request..'\n'):gmatch('(.-)\n') do lines[#lines+1]=line:gsub('\r$','') end;command(lines);publish() end
   end
  end)
  if not success then
   fatalError=tostring(failure);state.message=fatalError;state.count=0;state.rows={__array=true}
   -- An error must not leave active=true forever. Let CE's cancel/completion
   -- handlers finish and restore its own pause state before closing.
   closing=true
   if active and f.btnNewScan.Enabled then active=false end
   if active then pcall(cancel);state.stage='scanning' else state.stage='closed' end
   publish()
  end
 end
 timer.Enabled=true
end)
if not ok then state.stage='closed';state.message=tostring(err);publish();closeCE() end