/*
 * @require:
 *   autolink
 *
 */

var bitpop;
if (!bitpop) bitpop = {};

// constants ==================================================================
bitpop.CONTROLLER_EXTENSION_ID = "igddmhdmkpkonlbfabbkkdoploafopcn";
// end of constants ===

// date formatting extension
Date.prototype.bitpopFormat = function (timeOnly) {
  var months = [ 'Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug',
      'Sep', 'Oct', 'Nov', 'Dec'];

  var curr_date = this.getDate();
  var curr_month = this.getMonth();
  curr_month++;
  var curr_year = this.getFullYear();

  var curr_hour = this.getHours();

  var a_p;
  if (curr_hour < 12)
     {
     a_p = "AM";
     }
  else
     {
     a_p = "PM";
     }
  if (curr_hour == 0)
     {
     curr_hour = 12;
     }
  if (curr_hour > 12)
     {
     curr_hour = curr_hour - 12;
     }

  var curr_min = this.getMinutes();
  if (curr_min.toString().length == 1)
    curr_min = '0' + curr_min.toString();

  return timeOnly ? curr_hour + ':' + curr_min + a_p :
      months[curr_month - 1] + ' ' + curr_date + ', ' + curr_year + ' at ' +
      curr_hour + ':' + curr_min + a_p;
};

Date.prototype.isTodayDate = function() {
  var now = new Date();
  return (this.getDate() == now.getDate()) &&
    (this.getMonth() == now.getMonth()) &&
    (this.getFullYear() == now.getFullYear());
};

bitpop.saveToLocalStorage = function(jidSelf, jidOther, msg, timestamp, me) {
  var maxHistorySize = 100;

  var jid = jidSelf + ':' + jidOther;
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

bitpop.preprocessMessageText = function(msgText) {
  return msgText.replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
                .autoLink({ 'target': '_blank' });
};
