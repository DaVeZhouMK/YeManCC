import fs from'node:fs';import path from'node:path';import{createRequire}from'node:module';import assert from'node:assert/strict';const root=process.cwd(),lane=process.argv[2],ui=root+'/CustomSteamLibrary/workspace-ui',samples='G:/YeManCC-Work/Build/Tasks/YMCC-Decky-SharedWallpaper-20261017/preview';const{chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json')('playwright'),browser=await chromium.launch({channel:'msedge',headless:true}),errors=[],rows=[],assertions=[];let checks=0;const check=(ok,label)=>{checks++;assertions.push({label,passed:Boolean(ok)});assert.ok(ok,label)};let fixture=fs.readFileSync(samples+'/library-fixture.js','utf8').replace('const listeners=new Set();','window.__qualityCalls=[];const listeners=new Set();').replace('result={appId:media.appId,query:media.name', 'result={entryKey:media.key,appId:media.appId,query:media.name').replace('media.selectedVideo={id:args.candidateId};','media.selectedVideo={id:args.candidateId,preferredHeight:args.preferredHeight};');
// Extend the existing preview fixture only at its memory IPC transport.
fixture += ';(' + function () {
  const webview = window.chrome.webview;
  const postMessage = webview.postMessage.bind(webview);
  const addEventListener = webview.addEventListener.bind(webview);
  const listeners = [];
  window.__selectionPlans = {};
  window.__selectionPending = [];
  webview.addEventListener = (type, listener) => {
    if (type === 'message') listeners.push(listener);
    addEventListener(type, listener);
  };
  webview.postMessage = message => {
    const command = message.command, args = message.arguments || {};
    window.__qualityCalls.push({ command, args: structuredClone(args) });
    const plan = window.__selectionPlans[command]?.shift();
    const reply = (ok, result, error) => queueMicrotask(() => {
      for (const listener of listeners) listener({ data: { kind: 'response', id: message.id, ok, result, error } });
    });
    if (plan === 'hold') {
      window.__selectionPending.push({ command, args, reply, run: () => postMessage(message) });
    } else if (plan?.error) reply(false, undefined, plan.error);
    else if (plan && 'result' in plan) reply(true, plan.result);
    else postMessage(message);
  };
}.toString() + ')();';

async function verifyDownloadFeedback(page,viewport){
 const rows=await page.evaluate(()=>{
  const savedGame=selectedActionGame,savedSnapshot=snapshot,savedRequests=new Map(sharedWallpaperDownloadRequests),rows=[];
  const entry={key:'steam-feedback',bindingRevision:10,wallpaperMode:'dynamic',refreshRequested:true,refreshSlot:'dynamic',assets:{dynamic:{id:'old'}},selectedVideo:{url:'https://video.steamstatic.com/example.mp4'}};
  selectedActionGame={...savedGame,gameMedia:entry};const uiKey=keyFor(selectedActionGame);
  const state={active:{},manualItems:{},manualUntil:Date.now()+60000};snapshot={...snapshot,gameMedia:{entries:{[entry.key]:entry}},mediaDownloads:state};
  const inspect=label=>{renderSharedWallpaperDownloadStatus();const panel=document.querySelector('#shared-wallpaper-download-status'),bar=panel.querySelector('progress');rows.push({label,visible:!panel.classList.contains('hidden'),status:panel.dataset.status,text:panel.textContent,value:bar.hasAttribute('value')?bar.value:null,retry:!panel.querySelector('button').classList.contains('hidden')});};
  try{
   setSharedWallpaperDownloadRequest({key:uiKey},{phase:'submitting'});inspect('submitting');
   setSharedWallpaperDownloadRequest({key:uiKey},{phase:'queued',revision:10,assetId:'old'});inspect('queued');
   state.active[entry.key]={key:entry.key,phase:'downloading',bytes:50,totalBytes:100};inspect('progress');
   state.active[entry.key]={key:entry.key,phase:'downloading',bytes:1048576,totalBytes:0};inspect('unknown-size');
   state.active[entry.key].phase='retrying';inspect('immediate-retry');
   suspendMediaDownloadPaint();rows.push({label:'background-progress-paused',paused:document.querySelector('#shared-wallpaper-download-status progress').hasAttribute('value')});
   state.active={};state.manualItems[entry.key]={state:'pending',lastError:'media-network-request-failed-12002',retryAt:Date.now()+20000,requestBindingRevision:10};inspect('retry-backoff');
   state.manualItems[entry.key]={state:'unavailable',autoStopped:true,lastError:'media-network-request-failed-12002',requestBindingRevision:10};inspect('stopped-error');
   state.manualItems[entry.key]={state:'unavailable',autoStopped:true,lastError:'media-task-time-budget',stopReason:'automatic-time-budget-exhausted',requestBindingRevision:10};state.manualUntil=0;state.pauseReason='game-running';inspect('budget-error-during-game');
   state.resourcePauseReason='on-battery';inspect('budget-error-on-battery');state.resourcePauseReason='';
   state.manualItems[entry.key]={state:'pending',lastError:'',requestBindingRevision:10};state.manualState='cancelled';inspect('cancelled-during-game');
   state.manualState='running';state.manualUntil=Date.now()+60000;state.active[entry.key]={key:entry.key,phase:'downloading',bytes:50,totalBytes:100};inspect('manual-progress-during-game');
   state.resourcePauseReason='cpu-busy';inspect('manual-progress-with-cpu-gate');state.resourcePauseReason='on-battery';inspect('manual-power-gate');
   state.active={};state.pauseReason='';state.resourcePauseReason='';state.manualItems={};state.manualState='';sharedWallpaperDownloadRequests.delete(uiKey);inspect('retained-choice-without-queue-receipt');
   setSharedWallpaperDownloadRequest({key:uiKey},{phase:'queued',revision:10,assetId:'old'});state.manualItems[entry.key]={state:'done',requestBindingRevision:10};entry.refreshRequested=false;entry.assets.dynamic.id='new';inspect('completed');
   selectedActionGame={...savedGame,gameDirectory:'C:/another',primaryExecutable:'C:/another/game.exe',gameMedia:{key:'steam-other',wallpaperMode:'dynamic'}};snapshot.gameMedia.entries={};inspect('other-game');
  }finally{selectedActionGame=savedGame;snapshot=savedSnapshot;sharedWallpaperDownloadRequests.clear();for(const[key,value]of savedRequests)sharedWallpaperDownloadRequests.set(key,value);renderSharedWallpaperDownloadStatus();}
  return rows;
 });
 const get=label=>rows.find(row=>row.label===label);
 check(get('submitting').visible&&get('submitting').value===null,'manual wallpaper click immediately shows pending feedback');
 check(get('queued').status==='pending'&&get('queued').visible,'accepted wallpaper request shows its own queued state');
 check(get('progress').status==='downloading'&&get('progress').value===0.5,'current artwork shows actual per-game byte progress');
 check(get('unknown-size').value===null&&get('unknown-size').text.includes('1.0 MB'),'HLS progress without total reports downloaded bytes');
 check(get('immediate-retry').status==='retrying'&&get('immediate-retry').value===null,'weak-network retry is visible in the current artwork');
 check(get('background-progress-paused').paused,'wallpaper progress animation is suspended with existing background paint policy');
 check(get('retry-backoff').status==='retrying'&&get('retry-backoff').retry,'recoverable failure keeps choice and explains bounded retry');
 check(get('stopped-error').status==='error'&&get('stopped-error').retry,'exhausted failure is displayed and offers explicit candidate reselection');
 check(get('budget-error-during-game').status==='error'&&get('budget-error-during-game').retry&&get('budget-error-during-game').text.includes('时间'),'terminal per-game time budget failure is not hidden by a global game pause');
 check(get('budget-error-on-battery').status==='error','a later power pause cannot turn an exhausted transfer back into resumable work');
 check(get('cancelled-during-game').text.includes('已停止')&&get('cancelled-during-game').retry,'cancelled manual work is identified before the global game-running gate');
 check(get('manual-progress-during-game').status==='downloading'&&get('manual-progress-during-game').value===0.5,'manual download keeps actual progress while game-running soft gate is bypassed');
 check(get('manual-progress-with-cpu-gate').status==='downloading','manual download feedback follows the native CPU soft-gate exemption');
 check(get('manual-power-gate').status==='waiting','manual feedback retains the native power protection gate');
 check(get('retained-choice-without-queue-receipt').status==='waiting'&&get('retained-choice-without-queue-receipt').retry&&!get('retained-choice-without-queue-receipt').text.includes('已加入下载队列'),'persisted replacement intent alone cannot claim durable queue admission');
 check(get('completed').status==='done'&&get('completed').value===1,'publication receipt completes current artwork progress');
 check(!get('other-game').visible,'another game never inherits the previous game download status');
 await page.evaluate(()=>{window.__feedbackScreenshotSnapshot=snapshot;const entry={...selectedActionGame.gameMedia,refreshRequested:true,refreshSlot:'dynamic'};snapshot={...snapshot,gameMedia:{entries:{[entry.key]:entry}},mediaDownloads:{active:{[entry.key]:{key:entry.key,phase:'downloading',bytes:10485760,totalBytes:20971520}}}};renderSharedWallpaperDownloadStatus();});
 await page.screenshot({path:lane+'/feedback-'+viewport.height+'.png',fullPage:true});
 await page.evaluate(()=>{snapshot=window.__feedbackScreenshotSnapshot;delete window.__feedbackScreenshotSnapshot;renderSharedWallpaperDownloadStatus();});return rows;
}

async function verifySelectionBoundaries(page, viewport) {
  const cases = [];
  const modal = page.locator('#artwork-search-modal');
  const cards = page.locator('[data-video-candidate]');
  const summary = page.locator('#artwork-search-summary');
  const reset = async () => {
    await page.reload();
    await page.locator('#edit-modal:not(.hidden)').waitFor();
    await page.waitForFunction(() => Boolean(selectedActionGame?.gameMedia?.key));
    await page.evaluate(() => {
      window.__beforeWallpaper = structuredClone(selectedActionGame.gameMedia.assets);
      window.__originalMedia = structuredClone(selectedActionGame.gameMedia);
    });
  };
  const search = async () => {
    await page.evaluate(() => searchSharedWallpaperVideos(captureSharedWallpaperWrite()));
    await cards.first().waitFor();
  };
  const settled = () => page.waitForFunction(() => !sharedWallpaperBusy);
  const noSuccess = () => page.evaluate(() => ![toastNotice.textContent, ...noticeQueue.map(n => n.message)].some(s => s.includes('已加入下载队列')));
  const unchanged = () => page.evaluate(() => JSON.stringify(selectedActionGame.gameMedia.assets) === JSON.stringify(__beforeWallpaper));
  const hold = command => page.evaluate(command => { __selectionPlans[command] = ['hold']; }, command);
  const pending = command => page.waitForFunction(command => __selectionPending.some(p => p.command === command), command);
  const release = (command, error) => page.evaluate(({ command, error }) => {
    const index = __selectionPending.findIndex(p => p.command === command);
    const item = __selectionPending.splice(index, 1)[0];
    if (error) item.reply(false, undefined, error); else item.run();
  }, { command, error });
  const switchGame = async () => {
    await page.evaluate(() => openEdit(mergedGames().find(g => g.mediaOnly)));
    await page.waitForFunction(() => selectedActionGame?.mediaOnly && Boolean(selectedActionGame.gameMedia?.key));
  };

  await reset();
  await page.evaluate(() => { __selectionPlans.mediaSearchVideos = [{ error: 'catalog-network-failed' }]; });
  await page.evaluate(() => searchSharedWallpaperVideos(captureSharedWallpaperWrite()));
  check((await summary.textContent()).includes('动态壁纸搜索未完成：catalog-network-failed'), 'search failure retains its own error category');
  await page.getByRole('button', { name: '重试搜索', exact: true }).click();
  await cards.first().waitFor();
  check(await cards.count() === 2, 'search retry restores the original candidate consumer');
  await page.evaluate(() => { __selectionPlans.mediaSearchVideos = [{ result: { query: 'No trailers', entryKey: selectedActionGame.gameMedia.key, bindingRevision: selectedActionGame.gameMedia.bindingRevision, searchToken: 'empty-search', candidates: [] } }]; });
  await page.evaluate(() => searchSharedWallpaperVideos(captureSharedWallpaperWrite()));
  check(await cards.count() === 0 && await page.locator('#artwork-search-empty').evaluate(node => !node.classList.contains('hidden')), 'successful empty search shows the empty state');
  check((await summary.textContent()).includes('0 个动态壁纸候选') && await noSuccess() && await unchanged(), 'empty results preserve wallpaper without a success notice');
  cases.push('search-error-retry-and-empty');

  await reset();
  await search();
  await page.evaluate(() => { window.__candidateNodes = [...document.querySelectorAll('[data-video-candidate]')]; });
  for (const error of ['queue-enqueue-failed', 'manual-request-failed']) {
    await page.evaluate(error => { __selectionPlans.mediaSelectVideo = [{ error }]; }, error);
    await cards.first().click();
    await settled();
    check((await summary.textContent()).includes(error) && !(await summary.textContent()).includes('搜索未完成'), error + ' is a selection/download request failure');
    check(await page.evaluate(() => __candidateNodes.every((node, i) => node === document.querySelectorAll('[data-video-candidate]')[i] && !node.disabled)), error + ' preserves candidate nodes and restores buttons');
    check(await noSuccess() && await unchanged(), error + ' does not claim queued or replace the existing wallpaper');
    check(await cards.first().evaluate(node => node === document.activeElement), error + ' restores focus to the retryable candidate');
  }
  await page.screenshot({ path: lane + '/selection-failure-' + viewport.height + '.png' });
  await cards.first().click();
  await settled();
  check(await modal.evaluate(n => n.classList.contains('hidden')), 'failed candidate can be retried directly to completion');
  check(!await noSuccess() && await unchanged(), 'only the successful retry reports queued and preserves old assets');
  cases.push('enqueue-and-manual-failure-direct-retry');

  await reset();
  await search();
  await hold('mediaSelectVideo');
  await cards.first().click();
  await pending('mediaSelectVideo');
  await page.evaluate(() => { for (const node of document.querySelectorAll('[data-video-candidate]')) node.dispatchEvent(new Event('click')); });
  check(await page.evaluate(() => __qualityCalls.filter(c => c.command === 'mediaSelectVideo').length === 1), 'repeated same and other candidate clicks share one in-flight request');
  check(await cards.first().isDisabled(), 'selected candidate is disabled while its request is pending');
  await release('mediaSelectVideo');
  await settled();
  check(await unchanged() && !await noSuccess(), 'confirmed selection queues once while retaining the current wallpaper');
  cases.push('duplicate-click-and-retained-wallpaper');

  await reset();
  await search();
  // Model only the IPC failure boundary: native saved +1 but has not accepted the media request yet.
  await page.evaluate(() => {
    const media = structuredClone(__originalMedia);
    media.bindingRevision++;
    media.selectedVideo = { id: '0', preferredHeight: 720 };
    __LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[media.key] = media;
    window.__acceptedMedia = structuredClone(media);
    __selectionPlans.mediaSelectVideo = [{ error: 'manual-request-failed-after-save' }];
  });
  await cards.first().click();
  await settled();
  check(await noSuccess() && await cards.count() === 2, 'saved selection without an accepted media request retains candidates without claiming queued');
  await page.evaluate(() => { __selectionPlans.mediaSelectVideo = [{ result: __acceptedMedia }]; });
  await cards.first().click();
  await settled();
  check(await page.evaluate(() => {
    const requests = __qualityCalls.filter(c => c.command === 'mediaSelectVideo');
    return requests.length === 2 && JSON.stringify(requests[0].args) === JSON.stringify(requests[1].args) && selectedActionGame.gameMedia.bindingRevision === __originalMedia.bindingRevision + 1;
  }), 'idempotent retry keeps the exact token/candidate/quality/revision and applies the accepted +1 receipt');
  check(await unchanged() && !await noSuccess(), 'recovered request announces success only after the accepted retry receipt');
  cases.push('partial-save-same-request-idempotent-retry');

  await reset();
  await search();
  await page.evaluate(() => {
    const media = structuredClone(__originalMedia);
    media.bindingRevision++;
    media.selectedVideo = { id: '0', preferredHeight: 720 };
    __LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[media.key] = media;
    __selectionPlans.mediaSelectVideo = [{ error: 'manual-request-failed-after-save' }];
  });
  await cards.first().click();
  await settled();
  for (const change of ['candidate', 'quality']) {
    await page.evaluate(() => { __selectionPlans.mediaSelectVideo = [{ error: '壁纸设置已更新，请重新搜索后选择' }]; });
    if (change === 'quality') {
      await page.locator('[data-select-id="artwork-video-quality"] .workspace-dropdown-trigger').click();
      await page.getByRole('option', { name: '1080p 优先', exact: true }).click();
    }
    await cards.nth(change === 'candidate' ? 1 : 0).click();
    await settled();
    check(await cards.count() === 2 && await noSuccess() && (await summary.textContent()).includes('请重新搜索后选择'), change + ' change on old revision is visibly rejected without discarding candidates');
  }
  await page.locator('[data-video-search-retry]').click();
  await cards.first().waitFor();
  await cards.nth(1).click();
  await settled();
  check(await page.evaluate(() => {
    const requests = __qualityCalls.filter(c => c.command === 'mediaSelectVideo');
    return requests.length === 4 && requests.slice(0, 3).every(r => r.args.bindingRevision === __originalMedia.bindingRevision) && requests[3].args.bindingRevision === __originalMedia.bindingRevision + 1 && requests[3].args.candidateId === '1';
  }), 'explicit re-search uses the native search revision for a changed candidate after partial save');
  check(await modal.evaluate(n => n.classList.contains('hidden')) && !await noSuccess() && await unchanged(), 'changed candidate completes only after re-search and accepted receipt');
  cases.push('partial-save-changed-request-requires-research');

  await reset();
  await search();
  await page.evaluate(() => {
    const state = structuredClone(__LIBRARY_H5_FIXTURE__.snapshot);
    const entry = state.gameMedia.entries[selectedActionGame.gameMedia.key];
    entry.bindingRevision++;
    entry.selectedVideo = { id: '0', preferredHeight: 720 };
    entry.stateRefreshMarker = 'consumed-after-owner-advance';
    __selectionPlans.mediaState = [{ result: state }];
  });
  await cards.first().click();
  await settled();
  check(await page.evaluate(() => selectedActionGame.gameMedia.bindingRevision === __originalMedia.bindingRevision + 1 && selectedActionGame.gameMedia.stateRefreshMarker === 'consumed-after-owner-advance'), 'owner revision advances to the accepted receipt and allows the subsequent mediaState refresh');
  cases.push('accepted-owner-revision-state-refresh-consumed');

  for (const result of [null, { key: 'another-entry' }]) {
    await reset();
    await search();
    await page.evaluate(result => { __selectionPlans.mediaSelectVideo = [{ result }]; }, result);
    await cards.first().click();
    await settled();
    check(await cards.count() === 2 && await noSuccess() && await unchanged(), 'missing or different-entry selection receipt cannot claim queued or apply media');
  }
  cases.push('invalid-selection-receipt');

  await reset();
  await search();
  await page.evaluate(() => { __selectionPlans.mediaState = [{ error: 'media-state-refresh-failed' }]; });
  await cards.first().click();
  await settled();
  check(await modal.evaluate(n => n.classList.contains('hidden')), 'confirmed selection closes even when the subsequent state read fails');
  check(await page.evaluate(() => selectedActionGame.gameMedia.selectedVideo?.id === '0'), 'confirmed selection receipt is applied before optional state refresh');
  check(await page.evaluate(() => [toastNotice.textContent, ...noticeQueue.map(n => n.message)].some(s => s.includes('状态刷新未完成') && s.includes('media-state-refresh-failed'))), 'state refresh failure is reported separately from search and selection');
  check(await unchanged(), 'state refresh failure preserves the old wallpaper');
  cases.push('post-selection-state-refresh-failure');

  // Older hosts return the durable selection entry without mapped previews.
  await reset();await search();
  await page.evaluate(()=>{
    const receipt=structuredClone(__originalMedia);receipt.bindingRevision++;receipt.selectedVideo={id:'0',preferredHeight:720};
    delete receipt.previewUrl;delete receipt.dynamicPreviewUrl;delete receipt.staticPreviewUrl;
    __selectionPlans.mediaSelectVideo=[{result:receipt}];__selectionPlans.mediaState=[{error:'optional-state-unavailable'}];
  });
  await cards.first().click();await settled();
  check(await page.evaluate(()=>selectedActionGame.gameMedia.dynamicPreviewUrl===__originalMedia.dynamicPreviewUrl&&document.querySelector('#artwork-wallpaper-preview video')?.getAttribute('src')===__originalMedia.dynamicPreviewUrl),'raw accepted receipt plus failed optional read retains the exact existing video preview');
  check(await unchanged()&&!await noSuccess(),'preview retention does not replace assets or lose the accepted queued receipt');
  cases.push('raw-receipt-preview-retention');

  await reset();await search();await hold('mediaState');await cards.first().click();await pending('mediaState');
  await page.evaluate(()=>{
    snapshot.manualOverrides={...snapshot.manualOverrides,concurrentEditMarker:'new-edit-after-read'};
    snapshot.gameMedia.entries['other-concurrent-entry']={key:'other-concurrent-entry',marker:'new-other-entry'};
    window.__concurrentOverrides=snapshot.manualOverrides;
    window.__concurrentDownload=snapshot.mediaDownloads={...snapshot.mediaDownloads,statusRevision:Number(snapshot.mediaDownloads?.statusRevision||0)+10,concurrentProgress:true};
  });
  await release('mediaState');await settled();
  check(await page.evaluate(()=>snapshot.manualOverrides===__concurrentOverrides&&snapshot.manualOverrides.concurrentEditMarker==='new-edit-after-read'),'optional wallpaper refresh cannot replace a newer unrelated library edit');
  check(await page.evaluate(()=>snapshot.gameMedia.entries['other-concurrent-entry']?.marker==='new-other-entry'),'targeted wallpaper refresh preserves independently changed media entries');
  check(await page.evaluate(()=>__qualityCalls.filter(c=>c.command==='mediaState').at(-1).args.entryOnly===true),'post-selection refresh requests only the selected media entry');
  cases.push('targeted-state-read-preserves-concurrent-library-data');
  check(await page.evaluate(()=>snapshot.mediaDownloads===__concurrentDownload&&snapshot.mediaDownloads.concurrentProgress),'optional media read cannot overwrite a newer download progress event');

  await reset();await search();
  await page.evaluate(()=>{
    const entry=structuredClone(__originalMedia);entry.bindingRevision++;entry.sparseReadMarker='single-entry';
    window.__sparseOverrides=snapshot.manualOverrides;
    __selectionPlans.mediaState=[{result:{gameMedia:{entries:{[entry.key]:entry}},mediaDownloads:structuredClone(snapshot.mediaDownloads)}}];
  });
  await cards.first().click();await settled();
  check(await page.evaluate(()=>selectedActionGame.gameMedia.sparseReadMarker==='single-entry'),'new host single-entry response updates the accepted game media');
  check(await page.evaluate(()=>snapshot.manualOverrides===__sparseOverrides&&Array.isArray(snapshot.library?.games)),'single-entry response preserves the rest of the library snapshot');
  cases.push('single-entry-native-response-consumed');



  await reset();
  await hold('mediaSearchVideos');
  await page.evaluate(() => { window.__lateSearch = searchSharedWallpaperVideos(captureSharedWallpaperWrite()); });
  await pending('mediaSearchVideos');
  await switchGame();
  await search();
  await page.evaluate(() => { window.__newCandidate = document.querySelector('[data-video-candidate]'); });
  await release('mediaSearchVideos');
  await page.evaluate(() => __lateSearch);
  check(await page.evaluate(() => selectedActionGame.mediaOnly && __newCandidate === document.querySelector('[data-video-candidate]')), 'late search for another game cannot replace current candidates');
  cases.push('late-search-game-switch');

  for (const error of [undefined, 'late-selection-failed']) {
    await reset();
    await search();
    await hold('mediaSelectVideo');
    await cards.first().click();
    await pending('mediaSelectVideo');
    await switchGame();
    await search();
    await page.evaluate(() => { window.__newCandidate = document.querySelector('[data-video-candidate]'); window.__newSummary = document.querySelector('#artwork-search-summary').textContent; });
    await release('mediaSelectVideo', error);
    await settled();
    check(await page.evaluate(() => selectedActionGame.mediaOnly && __newCandidate === document.querySelector('[data-video-candidate]') && __newSummary === document.querySelector('#artwork-search-summary').textContent), 'late selection ' + (error ? 'failure' : 'success') + ' leaves new game candidates and summary intact');
    check(await modal.isVisible() && await noSuccess(), 'late selection cannot close the new search or announce queued');
  }
  cases.push('late-selection-success-and-failure-game-switch');

  await reset();
  await search();
  await hold('mediaState');
  await cards.first().click();
  await pending('mediaState');
  await switchGame();
  await search();
  await page.evaluate(() => { window.__newCandidate = document.querySelector('[data-video-candidate]'); window.__newMedia = JSON.stringify(selectedActionGame.gameMedia); window.__snapshotBeforeLate = snapshot; window.__noticeCountBeforeLate = noticeQueue.length; });
  await release('mediaState');
  await settled();
  check(await page.evaluate(() => __newCandidate === document.querySelector('[data-video-candidate]') && __newMedia === JSON.stringify(selectedActionGame.gameMedia) && __snapshotBeforeLate === snapshot && __noticeCountBeforeLate === noticeQueue.length), 'late state refresh cannot mutate the new game, snapshot, candidates or notices');
  cases.push('late-state-refresh-game-switch');

  for (const binding of ['entry', 'executable', 'revision']) {
    await reset();
    await hold('mediaSearchVideos');
    await page.evaluate(() => { window.__lateSearch = searchSharedWallpaperVideos(captureSharedWallpaperWrite()); });
    await pending('mediaSearchVideos');
    await page.evaluate(binding => {
      if (binding === 'entry') selectedActionGame.gameMedia = { ...selectedActionGame.gameMedia, key: 'rebound-entry' };
      else if (binding === 'executable') selectedActionGame.primaryExecutable = 'G:\\H5Fixture\\Rebound\\game.exe';
      else selectedActionGame.gameMedia = { ...selectedActionGame.gameMedia, bindingRevision: selectedActionGame.gameMedia.bindingRevision + 1 };
    }, binding);
    await release('mediaSearchVideos');
    await page.evaluate(() => __lateSearch);
    check(await cards.count() === 0 && await noSuccess(), 'late search cannot cross a changed owner ' + binding + ' binding');
  }
  cases.push('late-search-entry-executable-revision-binding');

  for (const error of [undefined, 'late-rebound-selection-failed']) {
    await reset();
    await search();
    await hold('mediaSelectVideo');
    await cards.first().click();
    await pending('mediaSelectVideo');
    await page.evaluate(() => {
      selectedActionGame.gameMedia = { ...selectedActionGame.gameMedia, key: 'rebound-entry' };
      window.__reboundMedia = selectedActionGame.gameMedia;
      window.__newSummary = document.querySelector('#artwork-search-summary').textContent;
    });
    await release('mediaSelectVideo', error);
    await settled();
    check(await page.evaluate(() => selectedActionGame.gameMedia === __reboundMedia && __newSummary === document.querySelector('#artwork-search-summary').textContent) && await noSuccess(), 'late selection ' + (error ? 'failure' : 'success') + ' cannot cross an entry rebind');
  }
  cases.push('late-selection-entry-binding');

  await reset();
  await search();
  await hold('mediaState');
  await cards.first().click();
  await pending('mediaState');
  await page.evaluate(() => {
    selectedActionGame.gameMedia = { ...selectedActionGame.gameMedia, bindingRevision: selectedActionGame.gameMedia.bindingRevision + 1 };
    window.__reboundMedia = selectedActionGame.gameMedia;
    window.__snapshotBeforeLate = snapshot;
  });
  await release('mediaState');
  await settled();
  check(await page.evaluate(() => selectedActionGame.gameMedia === __reboundMedia && snapshot === __snapshotBeforeLate), 'late mediaState refresh cannot overwrite a newer binding revision in the same game');
  cases.push('late-state-refresh-entry-revision-binding');

  await reset();
  await hold('mediaSearchVideos');
  await page.evaluate(() => { window.__lateSearch = searchSharedWallpaperVideos(captureSharedWallpaperWrite()); });
  await pending('mediaSearchVideos');
  await search();
  await page.evaluate(() => { window.__newCandidate = document.querySelector('[data-video-candidate]'); });
  await release('mediaSearchVideos', 'superseded-search-failed');
  await page.evaluate(() => __lateSearch);
  check(await page.evaluate(() => __newCandidate === document.querySelector('[data-video-candidate]')), 'superseded search failure cannot replace the newer search');
  cases.push('superseded-search-token');

  await reset();
  await search();
  await hold('mediaSelectVideo');
  await cards.first().click();
  await pending('mediaSelectVideo');
  await page.locator('#artwork-search-cancel').click();
  await release('mediaSelectVideo');
  await settled();
  check(await noSuccess() && await unchanged() && await modal.evaluate(n => n.classList.contains('hidden')), 'cancelled selection ignores the late reply and releases its busy state');
  cases.push('cancelled-selection-late-reply');
  return cases;
}

try{for(const viewport of[{width:1280,height:720},{width:1280,height:800}]){const page=await browser.newPage({viewport});const send=key=>viewport.height===800?page.evaluate(key=>routeWorkspaceAction(({Enter:'accept',Escape:'back',ArrowDown:'navigate-down'})[key]),key):page.keyboard.press(key);page.on('pageerror',e=>errors.push(String(e)));await page.route('http://app.localhost/**',route=>{const name=path.basename(new URL(route.request().url()).pathname)||'index.html';if(['index.html','app.js','styles.css'].includes(name)){let body=fs.readFileSync(ui+'/'+name);if(name==='index.html')body=Buffer.from(body.toString().replace('</head>','<script>'+fixture+'</script></head>'));return route.fulfill({contentType:name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html; charset=utf-8',body});}const allowed=['cover.jpg','header.jpg','wallpaper.jpg','sample.mp4'];if(allowed.includes(name))return route.fulfill({contentType:name.endsWith('.mp4')?'video/mp4':'image/jpeg',body:fs.readFileSync(samples+'/'+name)});return route.fulfill({status:404,body:''});});await page.goto('http://app.localhost/index.html');await page.locator('#edit-modal:not(.hidden)').waitFor();await page.evaluate(()=>searchSharedWallpaperVideos(captureSharedWallpaperWrite()));await page.locator('[data-video-candidate="0"]').waitFor();const trigger=page.locator('[data-select-id="artwork-video-quality"] .workspace-dropdown-trigger');check(await page.locator('#artwork-video-quality').inputValue()==='720','manual dialog starts at 720p');await trigger.focus();await send('Enter');await page.getByRole('option',{name:'1080p 优先',exact:true}).waitFor();await send('Escape');check(await trigger.evaluate(n=>n===document.activeElement),'cancel returns focus to quality trigger');check(await page.locator('#artwork-video-quality').inputValue()==='720','cancel leaves default preference');await trigger.focus();await send('Enter');await page.waitForFunction(()=>document.activeElement?.getAttribute('role')==='option');await send('ArrowDown');await page.waitForTimeout(180);await send('ArrowDown');await send('Enter');check(await page.locator('#artwork-video-quality').inputValue()==='1080','production dropdown keyboard/controller key path selects 1080p');check(await page.locator('[data-video-candidate="0"] .artwork-candidate-meta').textContent()==='1080p 优先','candidate label follows explicit quality');const layout=await page.locator('#artwork-search-modal .modal-card').evaluate(n=>{const box=n.getBoundingClientRect(),quality=document.querySelector('#artwork-video-quality-row').getBoundingClientRect();return{dialog:{x:box.x,y:box.y,right:box.right,bottom:box.bottom},quality:{x:quality.x,y:quality.y,right:quality.right,bottom:quality.bottom},viewport:[innerWidth,innerHeight]}});check(layout.quality.x>=layout.dialog.x&&layout.quality.right<=layout.dialog.right&&layout.quality.bottom<=layout.dialog.bottom,'manual quality remains inside real modal');await page.screenshot({path:lane+'/manual-quality-'+viewport.height+'.png'});await page.locator('[data-video-candidate="0"]').focus();await send('Enter');await page.waitForFunction(()=>__qualityCalls.some(c=>c.command==='mediaSelectVideo'));check(await page.evaluate(()=>__qualityCalls.filter(c=>c.command==='mediaSelectVideo').at(-1).args.preferredHeight===1080),'1080p preference reaches the real UI IPC request');await page.waitForFunction(()=>document.querySelector('#artwork-search-modal').classList.contains('hidden'));await page.evaluate(()=>searchSharedWallpaperVideos(captureSharedWallpaperWrite()));await page.locator('[data-video-candidate="0"]').waitFor();check(await page.locator('#artwork-video-quality').inputValue()==='1080','reopen reflects saved explicit game selection');await trigger.click();await page.getByRole('option',{name:'480p 优先',exact:true}).click();await page.locator('[data-video-candidate="1"]').click();await page.waitForFunction(()=>__qualityCalls.filter(c=>c.command==='mediaSelectVideo').length===2);check(await page.evaluate(()=>__qualityCalls.filter(c=>c.command==='mediaSelectVideo').at(-1).args.preferredHeight===480),'explicit 480p is also carried through');await page.evaluate(()=>searchArtwork('cover'));check(await page.locator('#artwork-video-quality-row').evaluate(n=>n.classList.contains('hidden')),'static artwork search does not show video quality');const requests=await page.evaluate(()=>__qualityCalls.filter(c=>c.command==='mediaSelectVideo'));const selectionCases=await verifySelectionBoundaries(page,viewport);const downloadFeedback=await verifyDownloadFeedback(page,viewport);rows.push({viewport,layout,requests,selectionCases,downloadFeedback});await page.close();}check(errors.length===0,'no exceptions across real shipping HTML/CSS/JS modal actions');const result={ok:true,checks,rows,assertions,errors,scope:'actual child HTML/CSS/app and actual dropdown/modal key handlers and workspace-action consumer; injected memory IPC, 1280x720/800, no physical controller or live native write'};fs.writeFileSync(lane+'/manual-quality-results.json',JSON.stringify(result,null,2));console.log(JSON.stringify({ok:true,checks}));}catch(error){fs.writeFileSync(lane+'/manual-quality-results.json',JSON.stringify({ok:false,checks,rows,assertions,errors,failure:String(error.stack||error)},null,2));throw error;}finally{await browser.close();}

