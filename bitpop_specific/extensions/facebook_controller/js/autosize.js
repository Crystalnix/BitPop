(function() {
window.resizeBy(document.width - document.documentElement.offsetWidth,
								document.height - document.documentElement.offsetHeight);
var style = document.createElement('link');
style.rel = 'stylesheet';
style.type = 'text/css';
style.href = chrome.extension.getURL('autosize.css');
document.head.appendChild(style);
})();
