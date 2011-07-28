PAPERS += deniel/deniel.dvi
CLEAN += $(DENIELEPS) $(DENIELPDF)

DENIELFIG := $(wildcard deniel/*.fig)
DENIELPLOT := $(wildcard deniel/*.gnu)
DENIELEPS := $(DENIELFIG:.fig=.eps) $(DENIELPLOT:.gnu=.eps)
DENIELPDF := $(DENIELEPS:.eps=.pdf)
DENIELDAT := $(wildcard deniel/*.dat)

## Add any additional .tex or .eps files below:
deniel/deniel.dvi deniel/deniel-proc.dvi: \
	deniel/deniel.tex \
	$(DENIELFIG) $(DENIELPLOT) $(DENIELEPS) $(DENIELPDF) \
	$(DENIELDAT) 

deniel/deniel-%.pdf: deniel/deniel-%.eps
	epstopdf $<

deniel/deniel-%.eps: deniel/deniel-%.fig
	fig2dev -L eps $< $@

deniel/deniel-%.eps: deniel/deniel-%.gnu
	gnuplot $<

## If you are generating eps from dia, uncomment:
# deniel/deniel-%.eps: deniel/deniel-%.dia
#	dia --export= -t eps-builtin $^

