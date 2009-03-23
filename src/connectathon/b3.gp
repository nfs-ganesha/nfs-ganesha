set terminal postscript
set output 'graphe_b3.ps'
set title "Basic Test Number 3"
set xlabel "Number of getcwd ans stat calls"
set ylabel "Time (in sec)"
set nokey
plot 'b3.txt' using 2:3
