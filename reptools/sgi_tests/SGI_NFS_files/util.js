/* Originally from nav_functions.js, the menu routines are obsolete now. */
window.onerror = null;
window.defaultStatus = '';

// Select box pull down redirection mechanism
function goSelect(daform) {
	var whereTo = daform.options[daform.selectedIndex].value;
	if (whereTo.indexOf("http://")==0 && whereTo.indexOf("sgi.com")==-1) {
		if (!Disclaimer()) return false;
	}
	top.location=whereTo;
}

// Ask the User if they're sure they want to leave
function Disclaimer () { return confirm(
 "You are now leaving SGI's website. SGI assumes no responsibility for\n"+
 "information or statements you may encounter on the Internet\n"+
 "outside of our website. Thank you for visiting.");
}
