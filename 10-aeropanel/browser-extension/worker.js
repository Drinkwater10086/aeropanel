
chrome.action.onClicked.addListener(async ({id}) => {
  if (id === undefined) return;
  try {
    await chrome.scripting.executeScript({target:{tabId:id},files:['meter.js']});
    await chrome.action.setBadgeText({tabId:id,text:'...'});
    await chrome.action.setTitle({tabId:id,title:'脚本已启动，等待视频样本'});
  } catch (error) {
    await chrome.action.setTitle({tabId:id,title:String(error.message)});
    await chrome.action.setBadgeText({tabId:id,text:'!'});
  }
});
chrome.runtime.onMessage.addListener((sample, sender, respond) => {
  if (sample.type !== 'aero-video' || !sender.tab?.active || sender.frameId !== 0) return;
  const values = [sample.fps, sample.ms, sample.average, sample.low];
  if (!values.every(Number.isFinite)) return;
  const report = ok => {
    chrome.action.setBadgeText({tabId:sender.tab.id,text:ok?'FPS':'ERR'}).catch(()=>{});
    chrome.action.setTitle({tabId:sender.tab.id,title:ok?'已采样，桌面浮窗已接收':'已采到视频帧，桌面浮窗未确认接收'}).catch(()=>{});
    respond(ok);
  };
  fetch('http://127.0.0.1:47653/video', {
    method:'POST', headers:{'X-AeroPanel-Video':'1'},
    body:'AEROVIDEO '+values.join(' ')+'\n', signal:AbortSignal.timeout(1700)
  }).then(response => report(response.ok), () => report(false));
  return true;
});
