function saveToLocalStorage(jid, msg, timestamp, me) {
  var maxHistorySize = 100;

  if (!(jid in localStorage)) {
    var newVal = JSON.stringify([{ msg: msg, time: timestamp, me: me }]);
    localStorage[jid] = newVal;
  } else {
    var vals = JSON.parse(localStorage[jid]);
    if (vals.length >= maxHistorySize)
      vals.shift();
    vals.push({ msg: msg, time: timestamp, me: me });
    localStorage[jid] = JSON.stringify(vals);
  }
}

function preprocessMessageText(msgText) {
  return msgText.replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
                .autoLink({ 'onclick':
                               'chrome.tabs.create({ \'url\': this.href })'});
}
