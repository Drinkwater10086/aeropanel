
chrome.action.onClicked.addListener(async ({id}) => {
  if (id === undefined) return;
  try {
    await chrome.scripting.executeScript({target: {tabId: id}, files: ['meter.js']});
    await chrome.action.setBadgeText({tabId: id, text: ''});
  } catch {
    await chrome.action.setBadgeText({tabId: id, text: '!'});
  }
});
