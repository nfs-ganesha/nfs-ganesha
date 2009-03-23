#!/usr/bin/env python2

# epinfo2sxw
#
# Written by Peter Åstrand <peter@cendio.se>
# Copyright (C) 2001 Cendio Systems AB (http://www.cendio.se)
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License. 
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

# Description: Extract equivalence partitioning and testcase
# information from docstrings (in nfs4st) and creates StarOffice
# tables from it. For more information about the SO XML file format,
# see http://xml.openoffice.org/. 

import nfs4st
import StringIO
import time

def parse_method(meth):
    name = meth.__name__
    doc = meth.__doc__
    if doc:
        lines = doc.split("\n")
    else:
        lines = []

    pos_test = 0
    neg_test = 0
    extra_test = 0
    valid_classes = ""
    invalid_classes = ""
    comments = ""
    # First line in docstring is the test case description.
    description = lines[0]
    while lines:
        line = lines[0]
        del lines[0]
        
        if line.find("Covered valid equivalence classes:") != -1:
            pos_test = 1
            data = line[line.find(":") + 1:]
            data = data.strip()
            valid_classes = data
            
        if line.find("Covered invalid equivalence classes:") != -1:
            neg_test = 1
            data = line[line.find(":") + 1:]
            data = data.strip()
            invalid_classes = data
            
        if line.find("Extra test") != -1:
            extra_test = 1
            valid_classes = "-"
            invalid_classes = "-"

        if line.find("Comments:") != -1:
            data = line[line.find(":") + 1:]
            data = data.strip()
            # Use everything after comment
            while lines:
                line = lines[0]
                data = data + " " + line.strip()
                del lines[0]
            comments += data

    if not (pos_test or neg_test or extra_test):
        print "ERROR: Unknown test type for method %s, skipping" % meth.__name__
        return

    if pos_test + neg_test + extra_test > 1:
        print "ERROR: Ambigious test type for method %s, skipping" % meth.__name__
        return

    # If we are still around, we know the test type for sure. 
    if pos_test:
        if not valid_classes:
            print "ERROR: Class coverage not specified for positive test %s" % meth.__name__
            return
        invalid_classes = "-"
    elif neg_test:
        if not invalid_classes:
            print "ERROR: Class coverage not specified for negative test %s" % meth.__name__
            return
        valid_classes = "-"
    elif extra_test:
        valid_classes = "-"
        invalid_classes = "-"
    else:
        # Should not happen
        raise("Internal error")
    
    if not comments:
        comments = "-"

    #
    # Write output
    #

    # One row per method
    outfile.write('<table:table-row>\n')
    outfile.write('<table:table-cell table:style-name="Table2.A2" table:value-type="string">\n')
    # Test name
    outfile.write('<text:p text:style-name="EP Table Contents">%s</text:p>\n' % name)
    # Test description
    outfile.write('<text:p text:style-name="EP Table Contents">%s</text:p>\n' % description)
    outfile.write('</table:table-cell>\n')

    # Valid equivalence classes. 
    xmlstr = """
    <table:table-cell table:style-name="Table2.B3" table:value-type="string">
    <text:p text:style-name="P1">%s</text:p>
    </table:table-cell>
    """
    outfile.write(xmlstr % valid_classes)

    # Invalid equivalence classes
    xmlstr = """
    <table:table-cell table:style-name="Table2.A2" table:value-type="string">
    <text:p text:style-name="P1">%s</text:p>
    </table:table-cell>
    """
    outfile.write(xmlstr % invalid_classes)

    # Comments
    xmlstr = """
    <table:table-cell table:style-name="Table2.D2" table:value-type="string">
    <text:p text:style-name="EP Table Contents">%s</text:p>
    </table:table-cell>
            """ 
    outfile.write(xmlstr % comments)
            
    outfile.write('</table:table-row>\n')


def get_cell_info(lines):
    cell = ""
    while lines:
        line = lines[0]
        if line.find("            ") == -1:
            break
        del lines[0]
        if not cell:
            cell += line.strip()
        else:
            cell += ", " + line.strip()

    return cell

def handle_valid_ec(lines):
    cell = get_cell_info(lines)
    
    outfile.write('<table:table-cell table:style-name="Table1.A2" table:value-type="string">\n')
    outfile.write('<text:p text:style-name="EP Table Contents">%s</text:p>\n' % cell)
    outfile.write('</table:table-cell>\n')

def handle_invalid_ec(lines):
    cell = get_cell_info(lines)

    outfile.write('<table:table-cell table:style-name="Table1.C2" table:value-type="string">\n')
    outfile.write('<text:p text:style-name="EP Table Contents">%s</text:p>\n' % cell)
    outfile.write('</table:table-cell>\n')


def handle_ic(lines):
     while lines:
         line = lines[0]
         if line.find("        ") == -1:
             return
         
         del lines[0]
         if line.find("Valid equivalence classes:") != -1:
             #print "Handling valid equivalence classes"
             handle_valid_ec(lines)

         if line.find("Invalid equivalence classes:") != -1:
             #print "Handling invalid equivalence classes"
             handle_invalid_ec(lines)
         

def handle_ep(lines):

    while lines:
        line = lines[0]
        del lines[0]
        if line.find("Input Condition:") != -1:
            ic_name = line[line.find(":") + 2:]
            #print "Handling Input Condition:", ic_name
            outfile.write("<table:table-row>\n")

            outfile.write('<table:table-cell table:style-name="Table1.A2" table:value-type="string">\n')
            outfile.write('<text:p text:style-name="EP Table Contents">%s</text:p>\n' % ic_name)
            outfile.write('</table:table-cell>\n')

            handle_ic(lines)
            outfile.write("</table:table-row>\n")


def class_output(klass):
    doc = klass.__doc__
    if not doc or doc.find("Equivalence partitioning:") == -1:
        print "Warning: %s has no Equivalence partitioning information" \
              % klass.__name__
        return


    outfile.write(EQPART_HEAD % klass.__name__)
    
    lines = doc.split("\n")
    while lines:
        line = lines[0]
        del lines[0]
        if line.find("Equivalence partitioning:") != -1:
            handle_ep(lines)

    outfile.write('</table:table>\n')
    outfile.write('<text:p text:style-name="Standard"><text:s/></text:p>\n')
    outfile.write('<text:p text:style-name="Standard"/>\n')
            

def parse_testcase(klass):
    print "Parsing", klass.__name__
    
    class_output(klass)

    outfile.write(TCPART_HEAD % klass.__name__)
    
    for methodname in dir(klass):
        if methodname.startswith("test"):
            method = eval("klass." + methodname)
            parse_method(method)
            
    outfile.write('</table:table>\n')
    outfile.write('<text:p text:style-name="Standard"><text:s/></text:p>\n')
    outfile.write('<text:p text:style-name="Standard"/>\n')

def main():
    global outfile
    outfile = StringIO.StringIO()
    outfile.write(EPINFO_HEAD)
    for attr in dir(nfs4st):
        if attr == "NFSSuite":
            continue
        if attr.endswith("Suite"):
            parse_testcase(eval("nfs4st." + attr))

    ending = """
  <text:p text:style-name="Standard"/>
 </office:body>
</office:document-content>
"""
    outfile.write(ending)

    print "Wrote epinfo.sxw."
    create_zip("epinfo.sxw", outfile.getvalue())
    outfile.close()

def create_zip(filename, content):
    import zipfile
    z = zipfile.ZipFile(filename, "w")
    date_time = time.localtime()[0:6]

    # content.xml
    zinfo = zipfile.ZipInfo("content.xml", date_time)
    z.writestr(zinfo, content)

    # styles.xml
    zinfo = zipfile.ZipInfo("styles.xml", date_time)
    z.writestr(zinfo, STYLES_FILE)

    z.close()

#
# XML clips
#
EQPART_HEAD = """
  <table:table table:name="Table1" table:style-name="Table1">
   <table:table-column table:style-name="Table1.A" table:number-columns-repeated="3"/>
   <table:table-header-rows>
    <table:table-row>
     <table:table-cell table:style-name="Table1.A1" table:number-columns-spanned="3" table:value-type="string">
      <text:p text:style-name="EP Table Heading">Equivalence partitioning for %s</text:p>
     </table:table-cell>
     <table:covered-table-cell/>
     <table:covered-table-cell/>
    </table:table-row>
   </table:table-header-rows>
   <table:table-row>
    <table:table-cell table:style-name="Table1.A2" table:value-type="string">
     <text:p text:style-name="EP Table Heading">Input Conditions</text:p>
    </table:table-cell>
    <table:table-cell table:style-name="Table1.A2" table:value-type="string">
     <text:p text:style-name="EP Table Heading">Valid Equivalence Classes</text:p>
    </table:table-cell>
    <table:table-cell table:style-name="Table1.C2" table:value-type="string">
     <text:p text:style-name="EP Table Heading">Invalid Equivalence Classes</text:p>
    </table:table-cell>
   </table:table-row>
"""

TCPART_HEAD = """
  <table:table table:name="Table2" table:style-name="Table2">
   <table:table-column table:style-name="Table2.A"/>
   <table:table-column table:style-name="Table2.B"/>
   <table:table-column table:style-name="Table2.C"/>
   <table:table-column table:style-name="Table2.D"/>
   <table:table-header-rows>
    <table:table-row>
     <table:table-cell table:style-name="Table2.A1" table:number-columns-spanned="4" table:value-type="string">
      <text:p text:style-name="EP Table Heading">Test Cases for %s</text:p>
     </table:table-cell>
     <table:covered-table-cell/>
     <table:covered-table-cell/>
     <table:covered-table-cell/>
    </table:table-row>
   </table:table-header-rows>
   <table:table-row>
    <table:table-cell table:style-name="Table2.A2" table:value-type="string">
     <text:p text:style-name="EP Table Heading">Name and description</text:p>
    </table:table-cell>
    <table:table-cell table:style-name="Table2.A2" table:value-type="string">
     <text:p text:style-name="EP Table Heading">Covered valid equivalence classes</text:p>
    </table:table-cell>
    <table:table-cell table:style-name="Table2.A2" table:value-type="string">
     <text:p text:style-name="EP Table Heading">Covered invalid equivalence classes</text:p>
    </table:table-cell>
    <table:table-cell table:style-name="Table2.D2" table:value-type="string">
     <text:p text:style-name="EP Table Heading">Comments</text:p>
    </table:table-cell>
   </table:table-row>
"""

EPINFO_HEAD = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE office:document-content PUBLIC "-//OpenOffice.org//DTD OfficeDocument 1.0//EN" "office.dtd">
<office:document-content xmlns:office="http://openoffice.org/2000/office" xmlns:style="http://openoffice.org/2000/style" xmlns:text="http://openoffice.org/2000/text" xmlns:table="http://openoffice.org/2000/table" xmlns:draw="http://openoffice.org/2000/drawing" xmlns:fo="http://www.w3.org/1999/XSL/Format" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:number="http://openoffice.org/2000/datastyle" xmlns:svg="http://www.w3.org/2000/svg" xmlns:chart="http://openoffice.org/2000/chart" xmlns:dr3d="http://openoffice.org/2000/dr3d" xmlns:math="http://www.w3.org/1998/Math/MathML" xmlns:form="http://openoffice.org/2000/form" xmlns:script="http://openoffice.org/2000/script" office:class="text" office:version="1.0">
 <office:script/>
 <office:font-decls>
  <style:font-decl style:name="Arial Unicode MS" fo:font-family="&apos;Arial Unicode MS&apos;" style:font-pitch="variable"/>
  <style:font-decl style:name="HG Mincho Light J" fo:font-family="&apos;HG Mincho Light J&apos;" style:font-pitch="variable"/>
  <style:font-decl style:name="Thorndale" fo:font-family="Thorndale" style:font-family-generic="roman" style:font-pitch="variable"/>
 </office:font-decls>
 <office:automatic-styles>
  <style:style style:name="Table1" style:family="table">
   <style:properties style:width="16.999cm" table:align="margins"/>
  </style:style>
  <style:style style:name="Table1.A" style:family="table-column">
   <style:properties style:column-width="5.666cm" style:rel-column-width="21845*"/>
  </style:style>
  <style:style style:name="Table1.A1" style:family="table-cell">
   <style:properties fo:padding="0.097cm" fo:border="0.002cm solid #000000"/>
  </style:style>
  <style:style style:name="Table1.A2" style:family="table-cell">
   <style:properties fo:padding="0.097cm" fo:border-left="0.002cm solid #000000" fo:border-right="none" fo:border-top="none" fo:border-bottom="0.002cm solid #000000"/>
  </style:style>
  <style:style style:name="Table1.C2" style:family="table-cell">
   <style:properties fo:padding="0.097cm" fo:border-left="0.002cm solid #000000" fo:border-right="0.002cm solid #000000" fo:border-top="none" fo:border-bottom="0.002cm solid #000000"/>
  </style:style>
  
  <style:style style:name="Table2" style:family="table">
   <style:properties style:width="16.002cm" table:align="margins"/>
  </style:style>
  <style:style style:name="Table2.A" style:family="table-column">
   <style:properties style:column-width="4.399cm" style:rel-column-width="2494*"/>
  </style:style>
  <style:style style:name="Table2.B" style:family="table-column">
   <style:properties style:column-width="3cm" style:rel-column-width="1701*"/>
  </style:style>
  <style:style style:name="Table2.C" style:family="table-column">
   <style:properties style:column-width="3cm" style:rel-column-width="1701*"/>
  </style:style>
  <style:style style:name="Table2.D" style:family="table-column">
   <style:properties style:column-width="5.602cm" style:rel-column-width="3176*"/>
  </style:style>
  <style:style style:name="Table2.A1" style:family="table-cell">
   <style:properties fo:padding="0.097cm" fo:border="0.002cm solid #000000"/>
  </style:style>
  <style:style style:name="Table2.A2" style:family="table-cell">
   <style:properties fo:padding="0.097cm" fo:border-left="0.002cm solid #000000" fo:border-right="none" fo:border-top="none" fo:border-bottom="0.002cm solid #000000"/>
  </style:style>
  <style:style style:name="Table2.D2" style:family="table-cell">
   <style:properties fo:padding="0.097cm" fo:border-left="0.002cm solid #000000" fo:border-right="0.002cm solid #000000" fo:border-top="none" fo:border-bottom="0.002cm solid #000000"/>
  </style:style>
  <style:style style:name="Table2.B3" style:family="table-cell" style:data-style-name="N0">
   <style:properties fo:padding="0.097cm" fo:border-left="0.002cm solid #000000" fo:border-right="none" fo:border-top="none" fo:border-bottom="0.002cm solid #000000"/>
  </style:style>
  
  <style:style style:name="P1" style:family="paragraph" style:parent-style-name="EP Table Contents">
   <style:properties fo:text-align="end" style:justify-single-word="false"/>
  </style:style>
  <number:number-style style:name="N0" style:family="data-style">
   <number:number number:min-integer-digits="1"/>
  </number:number-style>
 </office:automatic-styles>
 <office:body>
  <text:sequence-decls>
   <text:sequence-decl text:display-outline-level="0" text:name="Illustration"/>
   <text:sequence-decl text:display-outline-level="0" text:name="Table"/>
   <text:sequence-decl text:display-outline-level="0" text:name="Text"/>
   <text:sequence-decl text:display-outline-level="0" text:name="Drawing"/>
  </text:sequence-decls>
  <text:p text:style-name="Standard"/>
"""

STYLES_FILE = """\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE office:document-styles PUBLIC "-//OpenOffice.org//DTD OfficeDocument 1.0//EN" "office.dtd">
<office:document-styles xmlns:office="http://openoffice.org/2000/office" xmlns:style="http://openoffice.org/2000/style" xmlns:text="http://openoffice.org/2000/text" xmlns:table="http://openoffice.org/2000/table" xmlns:draw="http://openoffice.org/2000/drawing" xmlns:fo="http://www.w3.org/1999/XSL/Format" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:number="http://openoffice.org/2000/datastyle" xmlns:svg="http://www.w3.org/2000/svg" xmlns:chart="http://openoffice.org/2000/chart" xmlns:dr3d="http://openoffice.org/2000/dr3d" xmlns:math="http://www.w3.org/1998/Math/MathML" xmlns:form="http://openoffice.org/2000/form" xmlns:script="http://openoffice.org/2000/script" office:version="1.0">
 <office:font-decls>
  <style:font-decl style:name="Arial Unicode MS" fo:font-family="&apos;Arial Unicode MS&apos;" style:font-pitch="variable"/>
  <style:font-decl style:name="HG Mincho Light J" fo:font-family="&apos;HG Mincho Light J&apos;" style:font-pitch="variable"/>
  <style:font-decl style:name="Thorndale" fo:font-family="Thorndale" style:font-family-generic="roman" style:font-pitch="variable"/>
 </office:font-decls>
 <office:styles>
  <style:default-style style:family="graphics"/>
  <style:default-style style:family="paragraph">
   <style:properties fo:color="#000000" style:font-name="Thorndale" fo:font-size="12pt" fo:language="none" fo:country="none" style:font-name-asian="HG Mincho Light J" style:font-size-asian="12pt" style:language-asian="none" style:country-asian="none" style:font-name-complex="Arial Unicode MS" style:font-size-complex="12pt" style:language-complex="none" style:country-complex="none" style:text-autospace="ideograph-alpha" style:punctuation-wrap="hanging" style:line-break="strict"/>
  </style:default-style>
  <style:style style:name="Standard" style:family="paragraph" style:class="text"/>
  <style:style style:name="Text body" style:family="paragraph" style:parent-style-name="Standard" style:class="text">
   <style:properties fo:margin-top="0mm" fo:margin-bottom="2.12mm"/>
  </style:style>
  <style:style style:name="Table Contents" style:family="paragraph" style:parent-style-name="Text body" style:class="extra">
   <style:properties text:number-lines="false" text:line-number="0"/>
  </style:style>
  <style:style style:name="Table Heading" style:family="paragraph" style:parent-style-name="Table Contents" style:class="extra">
   <style:properties fo:font-style="italic" fo:font-weight="bold" style:font-style-asian="italic" style:font-weight-asian="bold" style:font-style-complex="italic" style:font-weight-complex="bold" fo:text-align="center" style:justify-single-word="false" text:number-lines="false" text:line-number="0"/>
  </style:style>
 <style:style style:name="EP Table Heading" style:family="paragraph" style:parent-style-name="Table Heading">
   <style:properties fo:language="none" fo:country="none"/>
  </style:style>
  <style:style style:name="EP Table Contents" style:family="paragraph" style:parent-style-name="Table Contents">
   <style:properties fo:language="none" fo:country="none"/>
  </style:style>
  <text:outline-style>
   <text:outline-level-style text:level="1" style:num-format=""/>
   <text:outline-level-style text:level="2" style:num-format=""/>
   <text:outline-level-style text:level="3" style:num-format=""/>
   <text:outline-level-style text:level="4" style:num-format=""/>
   <text:outline-level-style text:level="5" style:num-format=""/>
   <text:outline-level-style text:level="6" style:num-format=""/>
   <text:outline-level-style text:level="7" style:num-format=""/>
   <text:outline-level-style text:level="8" style:num-format=""/>
   <text:outline-level-style text:level="9" style:num-format=""/>
   <text:outline-level-style text:level="10" style:num-format=""/>
  </text:outline-style>
  <text:footnotes-configuration style:num-format="1" text:start-value="0" text:footnotes-position="page" text:start-numbering-at="document"/>
  <text:endnotes-configuration style:num-format="i" text:start-value="0"/>
  <text:linenumbering-configuration text:number-lines="false" text:offset="4.99mm" style:num-format="1" text:number-position="left" text:increment="5"/>
 </office:styles>
 <office:automatic-styles>
  <style:page-master style:name="pm1">
   <style:properties fo:page-width="209.99mm" fo:page-height="296.99mm" style:num-format="1" style:print-orientation="portrait" fo:margin-top="20mm" fo:margin-bottom="20mm" fo:margin-left="20mm" fo:margin-right="20mm" style:footnote-max-height="0mm">
    <style:footnote-sep style:width="0.18mm" style:distance-before-sep="1.01mm" style:distance-after-sep="1.01mm" style:adjustment="left" style:rel-width="25%" style:color="#000000"/>
   </style:properties>
   <style:header-style/>
   <style:footer-style/>
  </style:page-master>
 </office:automatic-styles>
 <office:master-styles>
  <style:master-page style:name="Standard" style:page-master-name="pm1"/>
 </office:master-styles>
</office:document-styles>
"""


if __name__ == '__main__':
    main()
