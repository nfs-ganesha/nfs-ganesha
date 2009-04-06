set terminal postscript
set output 'graphe_b8.ps'
set title "Basic Test Number 8"
set xlabel "Number of Files"
set ylabel "Number of symlinks and readlinks"
set zlabel "Time (in sec)"
set nokey
splot 'b8.txt' using 3:2:5 with lines
set output 'graphe_b8_symlen.ps'
set title "Basic Test Number 8"
set xlabel "Size of symkink (number of char)"
set ylabel "Time (in sec)"
set nokey
plot 'b8.txt' using 4:5 with lines
