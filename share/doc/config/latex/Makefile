FILE_PREFIX=nfs-ganesha-adminguide

all: dvi ps

dvi: $(FILE_PREFIX).dvi

ps : $(FILE_PREFIX).ps

pdf : $(FILE_PREFIX).pdf

clean:
	rm -f $(FILE_PREFIX).aux
	rm -f $(FILE_PREFIX).dvi
	rm -f $(FILE_PREFIX).idx
	rm -f $(FILE_PREFIX).log
	rm -f $(FILE_PREFIX).ps
	rm -f $(FILE_PREFIX).toc

new: clean all


$(FILE_PREFIX).ps: $(FILE_PREFIX).dvi
	dvips -o $(FILE_PREFIX).ps $(FILE_PREFIX).dvi

$(FILE_PREFIX).dvi: $(FILE_PREFIX).tex
	# run it twince for index table
	latex $(FILE_PREFIX).tex
	latex $(FILE_PREFIX).tex
	pdflatex $(FILE_PREFIX).tex
	cp $(FILE_PREFIX).pdf ../../../../src/
