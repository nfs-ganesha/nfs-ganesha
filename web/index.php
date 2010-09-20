<?php

	# retrieves the page to be displayed
	$page = $_GET['page'];

	if ( $page == '' )
		$page = 'overview';
	
?>


<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1" />

<meta name="Author" 	CONTENT="Thomas LEIBOVICI, Philippe DENIEL">
<meta name="Expires" 	CONTENT="never">
<meta name="Generator" 	CONTENT="vim">
<meta name="Copyright" 	CONTENT="CEA/DAM 2008">

<meta name="Subject" 	CONTENT="NFS-GANESHA, an NFS server running in user-space">
<meta name="Keywords" 	CONTENT="NFS, Network File System, GANESHA, LGPL, CEA, POSIX, SNMP, Proxy, FUSE, NFSv4">


<title>NFS-GANESHA: Project Web Site</title>
<link href="nfs-ganesha.css" rel="stylesheet" type="text/css" />
</head>

<body>
<table width="1000" border="0" align="center" cellpadding="0" cellspacing="0">
  <tr>
    <td>
	<table width="100%" border="0" cellspacing="0" cellpadding="0">
        <tr>
	<td width="50">
		<a href="http://www.cea.fr" class="logo"><img src="images/logo_cea.gif"></a> 
	</td>
	<td width="30">&nbsp;</td>
        <td width="50%" height="120">
	<table border="0" cellspacing="0" cellpadding="0">

        <tr>
        <td height="42" valign="bottom"><a href="http://nfs-ganesha.sourceforge.net" class="logo">NFS-Ganesha</a></td>
          </tr>
          <tr>
    <td><a href="http://nfs-ganesha.sourceforge.net" class="tag">NFS server running in user-space</a></td>
          </tr>
        </table>
	</td>
        <td valign="bottom" style="padding-bottom:8px;">

	 <table width="60%" border="0" align="right" cellpadding="0" cellspacing="0">
	  <tr height="90"><td colspan="4"><a href="http://sourceforge.net"><img src="http://sflogo.sourceforge.net/sflogo.php?group_id=221673&amp;type=2"
 width="125" height="37" border="0" alt="SourceForge.net Logo" /></a></td></tr>
          <tr>
            <td width="15"><img src="images/img04.gif" alt="Home" width="11" height="10" /></td>
            <td><a href="http://nfs-ganesha.sourceforge.net" class="topmenu">Home</a></td>
            <td width="15"><img src="images/img06.gif" alt="Contact us" width="11" height="8" /></td>
            <td><a href="index.php?page=contacts" class="topmenu">Contacts</a></td>
          </tr>
        </table></td>
      </tr>
    </table></td>
  </tr>
  <tr>
    <td height="10"></td>
  </tr>
  <tr>
    <td height="40"><table width="70%" border="0" cellspacing="0" cellpadding="0">
      <tr>
        <td><a href="index.php?page=overview" class="toplinks">Overview</a></td>
        <td><a href="index.php?page=contacts" class="toplinks">Contacts</a></td>
        <td><a href="index.php?page=links" class="toplinks">Links</a></td>
      </tr>
    </table></td>
  </tr>
  <tr>
    <td>&nbsp;</td>
  </tr>
  <tr>
    <td><table width="100%" border="0" cellspacing="0" cellpadding="0">
      <tr>
        <td width="220" valign="top"><table width="100%" border="0" cellspacing="0" cellpadding="0">
	  <tr>

            <td class="boxed" valign="top"><table width="100%" border="0" align="center" cellpadding="0" cellspacing="0">
              <tr>
                <td class="boxtitle">News &amp; Updates</td>
              </tr>
              <tr>
                <td>&nbsp;</td>
              </tr>
              <tr>
                <td>
		<table width="80%" border="0" align="center" cellpadding="0" cellspacing="0">
		  <tr>
                    <td class="date">Sep 17, 2010</td>
              	  </tr>
                  <tr>
                    <td height="50"><a href="http://sourceforge.net/projects/nfs-ganesha/files/" class="smallred">v 1.0.1 is available</a></td>
                  </tr>
		  <tr>
                    <td class="date">January 2006</td>
              	  </tr>
                  <tr>
                    <td height="50" class="newstext">First time in production on site</td>
                  </tr>
		  <tr>
                    <td class="date">Octobre 2004</td>
              	  </tr>
                  <tr>
                    <td height="50" class="newstext">Project started</td>
                  </tr>

                </table>
                </td>
              </tr>
            </table></td>
          </tr>
          <tr>
            <td><a href="#" class="smallred"></a></td>
          </tr>
          <tr>
            <td class="boxed"><table width="100%" border="0" align="center" cellpadding="0" cellspacing="0">
              <tr>
                <td class="boxtitle">Quick Links </td>
              </tr>
              <tr>
                <td>&nbsp;</td>
              </tr>
              <tr>
                <td><table width="80%" border="0" align="center" cellpadding="5" cellspacing="5">
                  <tr> <td><a href="http://sourceforge.net/projects/nfs-ganesha" target="_blank" class="quicklinks">Sourceforge project's page</a></td> </tr>
                  <tr> <td><a href="http://www.linux.com/feature/153789" target="_blank" class="quicklinks">Article about NFS-GANESHA on linux.com</a></td> </tr>
                  <tr> <td><a href="http://nfs-ganesha.git.sourceforge.net/" target="_blank" class="quicklinks">NFS-GANESHA GitWeb (hosted by SourceForge)</a></td> </tr>
                  <tr> <td><a href="https://lists.sourceforge.net/lists/listinfo/nfs-ganesha-announce" target="_blank" class="quicklinks">NFS-GANESHA's announcement mailing-list</a></td> </tr>
                  <tr> <td><a href="https://lists.sourceforge.net/lists/listinfo/nfs-ganesha-devel" target="_blank" class="quicklinks">NFS-GANESHA's development mailing-list</a></td> </tr>
                  <tr> <td><a href="https://lists.sourceforge.net/lists/listinfo/nfs-ganesha-support" target="_blank" class="quicklinks">NFS-GANESHA's support mailing-list</a></td> </tr>
                  <tr> <td><a href="http://www.cea.fr" target="_blank" class="quicklinks">CEA web site</a></td> </tr>
		  <tr> <td><a href="http://www.usenix.org/fast10/going">  <img src="http://www.usenix.org/events/fast10/art/fast10_going.jpg" border="0" width="169" height="77" alt="I'm going to FAST '10">  </a> </td></tr>
		  <tr> <td><a href="http://www.gnu.org/licenses/lgpl.html">  <img src="http://www.gnu.org/graphics/lgplv3-147x51.png" border="0" width="169" height="77" alt="LGPLv3 Logo">  </a> </td></tr>
                </table></td>
              </tr>
            </table></td>
          </tr>
        </table></td>
        <td valign="top" style="padding-left:40px; text-align:justify; line-height:190%; padding-top:10px;">


<?php
	include 'pages/'.$page.'.html';
?>


       <!--   <table width="100%" border="0" cellspacing="0" cellpadding="0">
            <tr>
              <td height="1" bgcolor="#CCCCCC"></td>
            </tr>
          </table> //-->

</td>
      </tr>
    </table></td>
  </tr>
  <tr>
<td height="1" bgcolor="#CCCCCC"></td>
  </tr>
  <tr>
    <td align="center">
    <p id="links"><a href="http://www.cea.fr/" class="quicklinks">CEA</a> | <a href="http://www-ccrt.cea.fr/" class="quicklinks">CCRT</a> </p>

</td>
  </tr>
</table>

<div style="font-size: 0.8em; text-align: center; margin-top: 1.0em; margin-bottom: 1.0em;">
Designed by <a href="http://www.cmgtechnologies.com/" title="Designed by CMG Technologies" class="quicklinks">CMG Technologies</a>,
adapted by <a class="quicklinks" href="mailto: thomas.leibovici@cea.fr">Thomas LEIBOVICI</a>
<br>
Design downloaded from <a class="quicklinks" href="http://www.freewebtemplates.com/">Free Templates</a>
</div>
</body>
</html>
