set terminal postscript
set output 'graphe_b1.ps'
set title "Basic Test Number 1"
set xlabel "Number of Files"
set ylabel "Number of Directories"
set zlabel "Time (in sec)"
set nokey
splot 'b1.txt' using 2:3:5 with lines
set output 'graphe_b1_objects.ps'
set title "Basic Test Number 1"
set xlabel "Number of Files and Directories"
set ylabel "Time (in sec)"
set nokey
plot 'b1.txt' using 2+3:5
