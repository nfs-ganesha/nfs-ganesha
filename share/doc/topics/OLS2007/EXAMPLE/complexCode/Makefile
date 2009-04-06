
.SUFFIXES: .tex .dvi .aux .eps .fig .dia .ps .pdf .bib .bbl

TOP=complexFigure
TEXFILES=$(TOP).tex
FIGFILES:=$(wildcard *.fig)
EPSFILES:=$(wildcard *.eps)
EPSFILES+=$(FIGFILES:.fig=.eps)
PDFFILES=$(EPSFILES:.eps=.pdf)

.fig.eps:
	fig2dev -L eps $< >$@

.fig.pdf:
	fig2dev -L pdf $< >$@

.eps.pdf:
	epstopdf $<

all: $(TOP).ps $(TOP).pdf

$(TOP).ps: $(TOP).dvi
	dvips -o $(TOP).ps $(TOP)

$(TOP).dvi: $(TEXFILES) $(EPSFILES)
	latex $(TOP) || true
	bibtex $(TOP) || true
	latex $(TOP) || true
	latex $(TOP)

$(TOP).pdf: $(TEXFILES) $(PDFFILES)
	pdflatex $(TOP) || true
	bibtex $(TOP) || true
	pdflatex $(TOP) || true
	pdflatex $(TOP)

clean:
	rm -f *.aux *.dvi *.log 
	rm -f $(TOP).ps $(TOP).pdf $(TOP).bbl $(TOP).blg


