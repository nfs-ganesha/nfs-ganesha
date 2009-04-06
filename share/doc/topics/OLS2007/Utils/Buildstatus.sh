#!/bin/bash

# define functions to be anything that needs doing across the papers...
function do_prep {
  pageCount=0
  pageCount1=0
  pageCount2=0
  if [ -r Proceedings.pdf ] ; then
     pageCount=$(pdfinfo Proceedings.pdf | grep '^Pages:' | awk '{ print $2 }')
  fi
  if [ -r Proceedings-V1.pdf ] ; then
     pageCount1=$(pdfinfo Proceedings-V1.pdf | grep '^Pages:' | awk '{ print $2 }')
  fi
  if [ -r Proceedings-V2.pdf ] ; then
     pageCount2=$(pdfinfo Proceedings-V2.pdf | grep '^Pages:' | awk '{ print $2 }')
  fi

  echo '<html>'
  echo '<title>OLS07: Proceedings of the 2007 Linux Symposium</title>'
  echo '<body>'
  echo '<h2>Proceedings of the 2007 <a href="http://www.linuxsymposium.org">Linux Symposium</a></h2>'
  echo '<br><hr><br>'
  echo '<h2>Draft: DO NOT REDISTRIBUTE UNTIL AFTER THE CONFERENCE</h2>'
  echo '<table width="100%" border="1">'
  if [ $pageCount -gt 0 ] ; then
    echo '<tr><td><b>Proceedings</b> ('${pageCount}' pages)</td><td><a href="Proceedings.dvi">DVI</a> // <a href="Proceedings.pdf">PDF</a></td></tr>'
  fi
  if [ $pageCount -gt 0 ] ; then
    echo '<tr><td><b>Proceedings V1</b> ('${pageCount1}' pages)</td><td><a href="Proceedings-V1.pdf">PDF</a></td></tr>'
  fi
  if [ $pageCount -gt 0 ] ; then
    echo '<tr><td><b>Proceedings V2</b> ('${pageCount2}' pages)</td><td><a href="Proceedings-V2.pdf">PDF</a></td></tr>'
  fi
  echo '<tr><td>Templates</td><td><a href="OLS-Package-2007-V2.tar.gz">OLS-Package-2007-V2.tar.gz</a></td></tr>'
  echo '</table>'
  echo '<br><br>'
  echo "<h2>Individual Papers Received (${recCnt} of ${totCnt})</h2>"
  echo '<table width="100%" border="1">'
  echo '<tr><td><b>Author</b></td><td><b>Title / Link to Draft PDF</b></td></tr>'
  rm -f NotReceived.txt NotReceived.html PDFList.txt
  touch NotReceived.txt 
  touch NotReceived.html
  touch PDFList.txt
}
function do_work {
   if [ $RECEIVED -eq 0 ] ; then
       ## http://www.linuxsymposium.org/2007/view_abstract.php?content_key=86
       if [ $CONTENT_KEY -ne 0 ] ; then
         echo '<tr><td>' "$AUTHOR" '</td><td><a href="http://www.linuxsymposium.org/2007/view_abstract.php?content_key='${CONTENT_KEY}'">'"$TITLE"'</a></td></tr>' >> NotReceived.html
       else
         echo '<tr><td>' "$AUTHOR" '</td><td>'"$TITLE"'</td></tr>' >> NotReceived.html
       fi
       echo "$AUTHOR" >> NotReceived.txt
      return;
   fi
   dirName=$(echo "$AUTHOR" | awk '{ print $NF }' | tr '[:upper:]' '[:lower:]')
   echo '<tr><td>' "$AUTHOR" '</td><td><a href="'${dirName}'-proc.pdf">'"$TITLE"'</a></td></tr>'
   echo "${dirName}/${dirName}-proc.pdf" >> PDFList.txt
}
function do_wrapup {
  echo '</table>'
  if [ ! -z NotReceived.html ] ; then
      echo '<br><hr><br>'
      echo "<h2>Individual Papers Not Yet Received (${notrecCnt} of ${totCnt})</h2>"
      echo '<table width="100%" border="1">'
      echo '<tr><td><b>AUTHOR</b></td><td><b>Title / Link to Abstract on Conference Site</b></td></tr>'
      cat NotReceived.html
      echo '</table>'
  fi
  echo '<br><hr><br>'
  echo "Page last updated: " $(date) '<br>'
  echo '<br><hr><br>'
  echo '</body>'
  echo '</html>'
  if [ -r Proceedings.ps ] ; then
     rm -f Proceedings.ps.gz
     gzip -9 Proceedings.ps
  fi
  for i in Proceedings.pdf Proceedings.dvi Proceedings-V1.pdf Proceedings-V2.pdf ; do
      if [ -r $i ] ; then
	  echo $i >> PDFList.txt
      fi
  done
}

myDataFile=$(dirname $0)/../Data.sh
export notrecCnt=$(grep -c RECEIVED=0 $myDataFile)
export recCnt=$(grep -c RECEIVED=1 $myDataFile)
export totCnt=$(grep -c RECEIVED= $myDataFile)

source $myDataFile


