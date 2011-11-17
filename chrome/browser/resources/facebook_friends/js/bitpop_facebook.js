/**
 * Override to hide app secret.
 */
Strophe.Connection.prototype._sasl_challenge1_fb = function (elem)
    {
        var challenge = Base64.decode(Strophe.getText(elem));
        var nonce = "";
        var method = "";
        var version = "";

        // remove unneeded handlers
        this.deleteHandler(this._sasl_failure_handler);

        var challenges = explode("&", challenge);
        for(i=0; i<challenges.length; i++)
        {
        	map = explode("=", challenges[i]);
        	switch (map[0])
        	{
        		case "nonce":
        			nonce = map[1];
        			break;
        		case "method":
        			method = map[1];
        			break;
        		case "version":
        			version = map[1];
        			break;
          }
        }

        var responseText = "";

        responseText += 'api_key=' + this.apiKey;
        responseText += '&call_id=' + (Math.floor(new Date().getTime()/1000));
        responseText += '&method=' + method;
        responseText += '&nonce=' + nonce;
        responseText += '&session_key=' + this.sessionKey;
        responseText += '&v=' + '1.0';

        var this_ = this;
		$.get(boshServerURL + '/sasl_challenge1_fb', {'data':responseText.replace(/&/g,"")}, function(data) {
	        responseText += '&sig=' + data;

	        this_._sasl_challenge_handler = this_._addSysHandler(
	            this_._sasl_challenge2_cb.bind(this_), null,
	            "challenge", null, null);
	        this_._sasl_success_handler = this_._addSysHandler(
	            this_._sasl_success_cb.bind(this_), null,
	            "success", null, null);
	        this_._sasl_failure_handler = this_._addSysHandler(
	            this_._sasl_failure_cb.bind(this_), null,
	            "failure", null, null);

	        this_.send($build('response', {
	            xmlns: Strophe.NS.SASL
	        }).t(Base64.encode(responseText)).tree());
		});
        return false;
};

