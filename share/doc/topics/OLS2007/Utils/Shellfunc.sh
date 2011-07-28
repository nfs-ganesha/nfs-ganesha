# These are convenience functions, useful when working in the papers tree.
# They're meant to be sourced into a bash shell.

# Does a "make clean" on however many papers
function mkclean { 
    for i in $@; do
        make DIRS=$i dirclean;
    done
}
# Does a "make clean ; make" on however many papers
function remake { 
    for i in $@; do
        make DIRS=$i dirclean;
        make DIRS=$i;
    done
}
# Does a "make paper; xpdf paper/paper.pdf" on a given paper
# Args: paper, starting page.  If PDFVIEW is set, will use that
# instead of xpdf, in which case the starting page arg may or
# may not be useful, depending on the other viewer.
function mview  { 
    local i=$1;
    local d=${2:-"1"};
    PDFVIEW=${PDFVIEW:-xpdf}
    make DIRS=$i && $PDFVIEW $i/$i.pdf $d
}

