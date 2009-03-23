set terminal postscript
set output 'graphe_b2.ps'
set title "Basic Test Number 2"
set xlabel "Number of Files"
set ylabel "Number of Directories"
set zlabel "Time (in sec)"
set nokey
splot 'b2.txt' using 2:3:5 with lines
set output 'graphe_b2_objects.ps'
set title "Basic Test Number 2"
set xlabel "Number of Files and Directories"
set ylabel "Time (in sec)"
set nokey
plot 'b2.txt' using 2+3:5
