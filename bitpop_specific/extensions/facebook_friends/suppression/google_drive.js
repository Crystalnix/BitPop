setTimeout(function() {

	var removeBar = function() {
		var shownBars = document.getElementsByClassName('jfk-butterBar jfk-butterBar-shown jfk-butterBar-info');
		for (var i = 0; i < shownBars.length; i++) {
			if (shownBars[i].innerText && shownBars[i].innerText.indexOf("Google Chrome") != -1) {
				shownBars[i].parentNode.removeChild(shownBars[i]);
				KX_resize();
			}
		}
	};

	removeBar();
	setIinterval(removeBar, 2000);
}, 500);
