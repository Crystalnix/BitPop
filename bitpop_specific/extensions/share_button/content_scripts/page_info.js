chrome.extension.sendRequest({
    type: 'pageInfo',
    info: {
        link: window.location.href,
        title: document.title,
        selection: window.getSelection().toString(),
        ref: document.referrer
    }
});