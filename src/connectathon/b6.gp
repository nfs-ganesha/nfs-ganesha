set terminal postscript
set output 'graphe_b6.ps'
set title "Basic Test Number 6"
set xlabel "Number of Files"
set ylabel "Number of entries read"
set zlabel "Time (in sec)"
set nokey
splot 'b6.txt' using 3:2:4 with lines
