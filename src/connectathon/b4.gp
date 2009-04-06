set terminal postscript
set output 'graphe_b4.ps'
set title "Basic Test Number 4"
set xlabel "Number of Files"
set ylabel "Number of chmod and stat calls"
set zlabel "Time (in sec)"
set nokey
splot 'b4.txt' using 3:2:4 with lines
