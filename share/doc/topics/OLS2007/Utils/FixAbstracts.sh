#!/bin/sh

for f in $* ; do
  echo working on "$f"
  mv -i "$f" "${f}.bak"
  chopPt=$(grep -n '^References' ${f}.bak | awk -F: '{ print $1 }')
  chopPt=$(( $chopPt - 1 ))
  cat ${f}.bak | cut -b 23- | head -${chopPt} | grep -v '30th, 2007, Ottawa, Canada' > ${f}
done
