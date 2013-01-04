function setAntiscrollHeight() {
  $('.box, .box .antiscroll-inner').height(
    $('body').height() - $('.box-wrap').offset().top
  );
  $('.antiscroll-inner').width($('body').width());
}

$(document).ready(function() {
  setAntiscrollHeight();
  $('.box-wrap').antiscroll();

  try {
    $('body select').msDropDown();
  } catch(e) {
    alert(e.message);
  }
});
$(window).resize(function() {
  setAntiscrollHeight();
  if ($('.box-wrap').data('antiscroll')) {
    $('.box-wrap').data('antiscroll').rebuild();
    }
});