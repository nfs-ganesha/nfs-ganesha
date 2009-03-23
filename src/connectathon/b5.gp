set terminal postscript
set output 'graphe_b5a_time.ps'
set title "Basic Test Number 5a"
set xlabel "Size of the file (in bytes)"
set ylabel "Count"
set zlabel "Time (in sec)"
set nokey
splot 'b5.txt' using 2:3:4 with lines
set output 'graphe_b5a_bsec.ps'
set title "Basic Test Number 5a"
set xlabel "Size of the file (in bytes)"
set ylabel "Count"
set zlabel "Bytes / Sec"
set nokey
splot 'b5.txt' using 2:3:6 with lines
set output 'graphe_b5b_time.ps'
set title "Basic Test Number 5b"
set xlabel "Size of the file (in bytes)"
set ylabel "Count"
set zlabel "Time (in sec)"
set nokey
splot 'b5.txt' using 2:3:5 with lines
set output 'graphe_b5b_bsec.ps'
set title "Basic Test Number 5b"
set xlabel "Size of the file (in bytes)"
set ylabel "Count"
set zlabel "Bytes / Sec"
set nokey
splot 'b5.txt' using 2:3:7 with lines
