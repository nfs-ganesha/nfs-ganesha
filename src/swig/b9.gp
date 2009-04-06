set terminal postscript
set output 'graphe_b9.ps'
set title "Basic Test Number 9"
set xlabel "Number of statfs calls"
set ylabel "Time (in sec)"
set nokey
plot 'b9.txt' using 2:3
