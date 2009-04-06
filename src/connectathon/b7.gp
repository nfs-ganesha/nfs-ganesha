set terminal postscript
set output 'graphe_b7.ps'
set title "Basic Test Number 7"
set xlabel "Number of Files"
set ylabel "Number of renames and links"
set zlabel "Time (in sec)"
set nokey
splot 'b7.txt' using 3:2:4 with lines
