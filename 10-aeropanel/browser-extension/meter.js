
(() => {
  if (globalThis.__aeroDesktopVideoMeter) return;
  globalThis.__aeroDesktopVideoMeter = true;
  let video, callback, previous, windowStart, totalFrames=0, totalMs=0;
  let windowFrames=0, intervals=[], lastSend=0;
  function reset() {
    if (callback !== undefined) video.cancelVideoFrameCallback(callback);
    callback=previous=windowStart=undefined;
    totalFrames=totalMs=windowFrames=lastSend=0;
    intervals=[];
  }
  function sample(now, frame) {
    callback=undefined;
    if (document.hidden || video.paused || video.seeking) { reset(); return; }
    const time=frame.expectedDisplayTime;
    const delta=previous ? frame.presentedFrames-previous.presentedFrames : 0;
    const elapsed=previous ? time-previous.expectedDisplayTime : 0;
    if(windowStart===undefined) windowStart=time;
    if(delta>0 && elapsed>0 && elapsed<2000) {
      totalFrames+=delta;
      totalMs+=elapsed;
      windowFrames+=delta;
      if(delta===1) {
        intervals.push(elapsed);
        if(intervals.length>1200) intervals.shift();
      }
    } else if(previous && (delta<0 || elapsed>=2000 || elapsed<0)) {
      reset();
      windowStart=time;
    }
    previous=frame;
    if(time-windowStart>=750 && windowFrames>0 && now-lastSend>=750) {
      const ms=(time-windowStart)/windowFrames;
      const worst=intervals.length>=100 ? [...intervals].sort((a,b)=>b-a).slice(0,Math.ceil(intervals.length/100)) : [];
      const low=worst.length ? 1000*worst.length/worst.reduce((a,b)=>a+b,0) : 0;
      try {
        chrome.runtime.sendMessage({type:'aero-video',fps:1000/ms,ms,average:1000*totalFrames/totalMs,low}).catch(() => {
          if (!chrome.runtime?.id) stop();
        });
      } catch (error) {
        if (!chrome.runtime?.id || /context invalidated/i.test(String(error))) {
          stop();
          return;
        }
        console.warn('AeroPanel could not send a video sample', error);
      }
      windowStart=time;
      windowFrames=0;
      lastSend=now;
    }
    callback=video.requestVideoFrameCallback(sample);
  }
  function onSeeking(event) { if(event.target===video)reset(); }
  function stop() {
    reset();
    document.removeEventListener('seeking',onSeeking,true);
    document.removeEventListener('visibilitychange',update);
    for (const name of playbackEvents) document.removeEventListener(name,onPlayback,true);
    delete globalThis.__aeroDesktopVideoMeter;
  }
  function update() {
    if (!chrome.runtime?.id) { stop(); return; }
    const next=[...document.querySelectorAll('video')].filter(v=>!v.paused&&!v.seeking&&v.getBoundingClientRect().width>0)
      .sort((a,b)=>b.clientWidth*b.clientHeight-a.clientWidth*a.clientHeight)[0];
    if(next!==video) { reset(); video=next; }
    if(document.hidden || !video){reset();return;}
    if(callback===undefined && typeof video.requestVideoFrameCallback==='function') callback=video.requestVideoFrameCallback(sample);
  }
  const playbackEvents=['playing','pause','emptied'];
  document.addEventListener('seeking',onSeeking,true);
  document.addEventListener('visibilitychange',update);
  function onPlayback(event) {
    if (event.target instanceof HTMLVideoElement) update();
  }
  for (const name of playbackEvents) document.addEventListener(name,onPlayback,true);
  update();
})();
