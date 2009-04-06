// convert all characters to lowercase to simplify testing 
var agt=navigator.userAgent.toLowerCase(); 

// *** BROWSER VERSION *** 
var vers = parseInt(navigator.appVersion); 

// *** BROWSER TYPE ***
var opera = (agt.indexOf("opera") != -1);
var opera7 = (opera && (vers >= 7));
var moz = (agt.indexOf("gecko") != -1);
var ie = ((agt.indexOf("msie") != -1) && !moz && !opera);
var ie4 = (ie && (vers >= 4));
var nn = ((agt.indexOf("mozilla") != -1) && !ie && !moz && !opera);
var nav4 = (nn && (vers >= 4));

// *** PLATFORM ***
var is_win  = (agt.indexOf("win")!=-1);
var is_mac  = (agt.indexOf("mac")!=-1);
var is_unix = (agt.indexOf("x11")!=-1);

// Select the appropriate stylesheet
if (nav4 || ie4 || moz || opera7 || (!nn && !ie && !moz && !opera)) {
 var isFour = true; // used in: nav_functions.js and survey_widget.js
 var styles = '';
 if (is_win)	styles += (nn)?'win_ns':'win_ie';
 else
  if (is_mac)	styles += (nn)?'mac_ns':'mac_ie';
  else		styles += (nn)?'unix':'win_ie';	// UNIX
 if (moz)	styles = 'mozilla'; // trumps all
 document.write('<LINK REL="stylesheet" HREF="http://www.sgi.com/styles/'+
	styles+'.css" TYPE="text/css">');
}
//alert('agent: '+agt);
