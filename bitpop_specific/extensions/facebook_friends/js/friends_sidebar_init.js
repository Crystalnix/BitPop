function setAntiscrollHeight() {
  $('#friend_list').height(
    $('body').height() - $('.antiscroll-wrap').offset().top
  );
  $('.antiscroll-inner').width($('body').width());
}

$(document).ready(function() {

  setAntiscrollHeight();
  $('.antiscroll-wrap').antiscroll();

  try {
    $('body select').msDropDown();
  } catch(e) {
    alert(e.message);
  }
});
$(window).resize(function() {
  setAntiscrollHeight();
  if ($('.antiscroll-wrap').data('antiscroll')) {
    $('.antiscroll-wrap').data('antiscroll').rebuild();
    }
});

