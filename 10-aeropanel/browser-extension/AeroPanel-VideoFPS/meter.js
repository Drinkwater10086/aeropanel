
(() => {
  const key = '__aeroVideoMeter';
  if (globalThis[key]) { globalThis[key](); return; }
  const badge = document.createElement('div');
  badge.style.cssText = 'position:fixed;top:12px;right:12px;z-index:2147483647;pointer-events:none';
  const label = document.createElement('span');
  label.style.cssText = 'display:block;padding:6px 10px;background:#16181de8;color:#fff;border-radius:6px;font:600 14px monospace';
  badge.attachShadow({mode:'closed'}).append(label);
  let video, callback, baseline, lastSample = 0;
  const waiting = () => { label.textContent = '等待视频帧…'; };
  function sample(now, frame) {
    lastSample = now;
    if (!baseline || frame.presentedFrames <= baseline.presentedFrames ||
        frame.expectedDisplayTime < baseline.expectedDisplayTime) baseline = frame;
    const elapsed = frame.expectedDisplayTime - baseline.expectedDisplayTime;
    const count = frame.presentedFrames - baseline.presentedFrames;
    if (elapsed >= 500 && count > 0) {
      const ms = elapsed / count;
      label.textContent = `视频 ${(1000 / ms).toFixed(1)} FPS · ${ms.toFixed(1)} ms`;
      baseline = frame;
    }
    callback = video.requestVideoFrameCallback(sample);
  }
  function reset() {
    if (callback !== undefined) video.cancelVideoFrameCallback(callback);
    callback = undefined;
    baseline = undefined;
    lastSample = 0;
  }
  function update() {
    const root = document.fullscreenElement || document.documentElement;
    if (badge.parentNode !== root) root.append(badge);
    const candidates = root.matches('video') ? [root] : [...root.querySelectorAll('video')];
    const visible = candidates.map(item => {
      const rect = item.getBoundingClientRect();
      const width = Math.max(0, Math.min(innerWidth, rect.right) - Math.max(0, rect.left));
      const height = Math.max(0, Math.min(innerHeight, rect.bottom) - Math.max(0, rect.top));
      return {item, area: width * height};
    }).filter(({area}) => area > 0);
    visible.sort((a,b) => Number(a.item.paused) - Number(b.item.paused) || b.area - a.area);
    const next = visible[0]?.item;
    if (next !== video) { reset(); video = next; waiting(); }
    if (!video) { label.textContent = '未找到可见视频'; return; }
    if (!video.requestVideoFrameCallback) { label.textContent = '浏览器不支持视频帧采样'; return; }
    if (video.paused || video.seeking) { reset(); label.textContent = video.seeking ? '正在跳转…' : '视频已暂停'; return; }
    if (callback === undefined) { waiting(); callback = video.requestVideoFrameCallback(sample); }
    if (lastSample && performance.now() - lastSample > 1500) { baseline = undefined; waiting(); }
  }
  const timer = setInterval(update, 500);
  document.addEventListener('fullscreenchange', update);
  globalThis[key] = () => {
    clearInterval(timer);
    document.removeEventListener('fullscreenchange', update);
    reset();
    badge.remove();
    delete globalThis[key];
  };
  update();
})();
