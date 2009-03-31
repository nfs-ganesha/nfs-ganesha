# Makefile for proceedings using the OLS/GCCSummit schema.

# Tools - some of the dvi programs are from dviutils.rpm
#         pdftk is from pdftk.rpm (fedora-extras among other places)
#         dviconcat is from dviutils.rpm (try rpmfind)
LATEX	  = latex
BIBTEX	  = bibtex
DVIPS	  = dvips
DVIPDF	  = echo dvipdfm
DVICONCAT = dviconcat
PDFCONCAT = pdftk
CLEAN     = Frontmatter.pdf
# EXTRATEX  = /usr/share/texmf:
# Note: dvipdfm can be used to generate PDF from DVI in a pinch,
# but pdflatex seems to do slightly better.  We'll invoke both
# latex and pdflatex in the latex2dvi script, and skip dvipdfm
# for now.

# Note that "make WHAT=V1" will do Volume1, if set up; likewise with V2.

all: papers

# Get the set of all papers.  DIRS can be set on the command line to
# suppress some of the papers; as a side effect this disables
# generation of the proceedings.

# Papers which were dropped for one reason or another
DROPPED := hohndel/ thompson/ welte/ charles/ corbet/ gross/ marinas/ shureih/ wilson/ wong/

# MANUAL SPLIT of papers into two volumes.  Best done after doing the single volume...
V1DIRS := ben-yehuda bergmann bligh branco briglia brown_1 brown_2 brown_z cagney chen \
	crouse deniel forbes french gorman gough goyal harper hiramatsu holtmann \
	ke keniston kivity komkoff kroah-hartman lahey lameter linkov litke lunev lutterkort martin

V2DIRS := mason mathur melo menage mirtchovski muralidhar nagar nahari nakajima opdenacker padioleau \
	pallipadi perez-gonzalez phillips poettering potzl rostedt russell sato schlesinger siddha \
	singh sipek szeideman ueda vangend whitehouse wilder wu yoshioka zhang zhu zijlstra

FRONTMATTER := Frontmatter
PROCEEDINGS := Article_GANESHA_ols2007


# To rebuild the Example paper, delete the EXAMPLE/ from the DIRS line below
#DIRS := $(subst /,,$(filter-out  Texmf/ TEMPLATES/, $(wildcard */)))
ifeq ($(DIRS),)
DIRS := $(subst /,,$(filter-out BROKEN/ CVS/ $(DROPPED) EXAMPLE/ Init/ OLD/ STATUS/ Texmf/ TEMPLATES/ Topdir Utils/ local/ scratch/ .svn/, $(wildcard */)))

ifeq ($(WHAT),V1)
DIRS := $(V1DIRS)
FRONTMATTER := FrontmatterV1
PROCEEDINGS := Proceedings-V1
FRONTCOUNT  := FrontCount=9
endif

ifeq ($(WHAT),V2)
DIRS := $(V2DIRS)
FRONTMATTER := FrontmatterV2
PROCEEDINGS := Proceedings-V2
FRONTCOUNT  := FrontCount=9
endif

all: proceedings ProcSeq.mk

# Writing a dependency list for this file is impossible.  We want it
# to be regenerated only when the set of subdirectories changes, but
# there is no way to know that.  Fortunately, generating it is cheap.
ifneq ($(MAKECMDGOALS), clean)
include ProcSeq.mk
ProcSeq.mk:
	@set fnord $(DIRS); shift; \
	while [ $$# -gt 0 ]; do \
	    dir=$${1%/}; shift; \
	    printf '%s/%s-proc.tex: %s/%s-proc.stmp; @:\n' \
                   $$dir $$dir $$dir $$dir; \
	    printf '%s/%s-proc.stmp: ProcDefs.tex %s\n' $$dir $$dir $$prev; \
	    printf '\t$(FRONTCOUNT) ./Texmf/make-wrapper $$^ $$@\n'; \
	    prev=$$dir/$$dir-proc.aux; \
	done > $@T
	@mv -f $@T $@
endif
endif

SUBMAKES := $(DIRS:=/Makefile.inc)
include $(SUBMAKES)

# $(space) expands to a single space.
space := $(empty) $(empty)
SEARCHPATH := $(subst $(space),:,$(DIRS)):  # intentional trailing colon

# Render PDF and PostScript for each individual paper.
PS  = $(PAPERS:.dvi=.ps)
PDF = $(PAPERS:.dvi=.pdf)

update-templates:
	svn update TEMPLATES Texmf EXAMPLE Makefile $(FRONTMATTER).tex


papers: $(PS) $(PDF)
proceedings: $(PROCEEDINGS).ps $(PROCEEDINGS).pdf

# Render PDF and PostScript for the entire proceedings.  This works as
# follows: For each paper we generate a wrapper file, $(paper)-proc.tex, 
# which reads $(paper).tex in proceedings mode.  The content of this file
# depends on a small number of settings extracted from the top-level
# Proceedings.tex, and the .aux file of the alphabetically previous paper.
# A DVI file generated (in the usual manner) from that wrapper file will
# be suitable for concatenation with all the other DVIs to make the full
# proceedings DVI, from which we then generate PostScript and PDF as usual.

# Stamp-file pattern to prevent trivial changes in Proceedings.tex from
# triggering a complete rebuild.
ProcDefs.tex: ProcDefs.stmp; @:
ProcDefs.stmp: $(FRONTMATTER).tex
	{ echo '\PassOptionsToClass{proceedings}{ols}'; \
	  fgrep '\def\Proc' $<; } > ProcDefs.texT
	./Texmf/move-if-change ProcDefs.texT ProcDefs.tex
	echo timestamp > $@

ProcToc.tex: ProcToc.stmp; @:
ProcToc.stmp: $(PAPERS:.dvi=-proc.aux)
	./Texmf/make-toc $(@:.stmp=.tex) $^
	echo timestamp > $@

# These must have explicit rules; the implicit rules below are geared to
# subdirectories, and will not work.
$(FRONTMATTER).dvi: $(FRONTMATTER).tex ProcToc.tex
	./Texmf/latex2dvi $(*F)

$(PROCEEDINGS).dvi: $(FRONTMATTER).dvi $(PAPERS:.dvi=-proc.dvi)
	cp $(PAPERS:.dvi=-proc.dvi)  $(PROCEEDINGS).dvi

$(PROCEEDINGS).pdf: $(FRONTMATTER).pdf $(PAPERS:.dvi=-proc.pdf)
	cp $(PAPERS:.dvi=-proc.pdf) $(PROCEEDINGS).pdf

$(PROCEEDINGS).ps: $(PROCEEDINGS).dvi
	TEXINPUTS=$(SEARCHPATH) $(DVIPS) -q -o $@ $<

# Proceedings.pdf: Proceedings.dvi
#	TEXINPUTS=$(SEARCHPATH) $(DVIPDF) -q -o $@ $<

EXAMPLE/EXAMPLE.tex:
	cd EXAMPLE && ln -s myPaper.tex EXAMPLE.tex

# Utility.
dirclean:
	-rm -f $(PAPERS:.dvi=-proc.tex) $(PAPERS:.dvi=-proc.stmp)
	-rm -f $(PAPERS:.dvi=-proc.dvi) $(PAPERS:.dvi=-proc.log)
	-rm -f $(PAPERS:.dvi=-proc.pdf) 
	-rm -f $(PAPERS:.dvi=-proc.aux) $(PAPERS:.dvi=-proc.oaux)
	-rm -f $(PAPERS:.dvi=-proc.bbl) $(PAPERS:.dvi=-proc.blg)
	-rm -f $(PAPERS) $(PS) $(PDF)
	-rm -f $(PAPERS:.dvi=.aux) $(PAPERS:.dvi=.oaux) $(PAPERS:.dvi=.log)
	-rm -f $(PAPERS:.dvi=.bbl) $(PAPERS:.dvi=.blg) $(CLEAN)

volclean:
	-rm -f $(PAPERS:.dvi=-proc.tex) $(PAPERS:.dvi=-proc.stmp)
	-rm -f $(PAPERS:.dvi=-proc.dvi) $(PAPERS:.dvi=-proc.log)
	-rm -f $(PAPERS:.dvi=-proc.pdf) 
	-rm -f $(PAPERS:.dvi=-proc.aux) $(PAPERS:.dvi=-proc.oaux)
	-rm -f $(PAPERS:.dvi=-proc.bbl) $(PAPERS:.dvi=-proc.blg)
	-rm -f ProcSeq.mk ProcDefs.tex ProcDefs.stmp ProcToc.stmp ProcToc.tex
	-rm -f $(FRONTMATTER).dvi $(FRONTMATTER).log $(FRONTMATTER).aux $(FRONTMATTER).oaux
	-rm -f $(PROCEEDINGS).dvi $(PROCEEDINGS).ps 

mostlyclean: volclean
	-rm -f $(PAPERS) $(PS) $(PDF)
	-rm -f $(PAPERS:.dvi=.aux) $(PAPERS:.dvi=.oaux) $(PAPERS:.dvi=.log)
	-rm -f $(PAPERS:.dvi=.bbl) $(PAPERS:.dvi=.blg)

# CLEAN is set in the Makefile.inc files and usually consists of generated diagrams.
clean: mostlyclean volclean
	-rm -f Proceedings.pdf Proceedings.dvi Proceedings.ps
	-rm -f Proceedings-V1.pdf Proceedings-V1.dvi Proceedings-V1.ps
	-rm -f Proceedings-V2.pdf Proceedings-V2.dvi Proceedings-V2.ps
	-rm -f Frontmatter.dvi Frontmatter.log Frontmatter.aux Frontmatter.oaux
	-rm -f FrontmatterV1.dvi FrontmatterV1.log FrontmatterV1.aux FrontmatterV1.oaux
	-rm -f FrontmatterV2.dvi FrontmatterV2.log FrontmatterV2.aux FrontmatterV2.oaux
	-rm -f $(CLEAN)

# Pattern rules.  Generation of PDF/PS from DVI is straightforward.
%.pdf: %.dvi
	cd $(@D) && $(DVIPDF) -o $(@F) $(<F)

%.ps: %.dvi
	cd $(@D) && $(DVIPS) -q -o $(@F) $(<F)

.fig.eps:
	fig2dev -L eps $< >$@

.fig.pdf:
	fig2dev -L pdf $< >$@


# Properly regenerating a .dvi file from the corresponding .tex file
# requires running LaTeX (and possibly BibTeX) in a loop.  GNU make
# cannot be coded to do this, so we have a helper script to do it.
# We read the .aux files for nefarious purposes, so make needs to be
# aware that this operation generates them too.
%.dvi %.aux: %.tex
	cd $(@D) && TEXINPUTS=../Texmf:.:$$TEXINPUTS ../Texmf/latex2dvi $(*F)

.PHONY: all papers proceedings clean
.SECONDARY:  # Never delete intermediate files.

