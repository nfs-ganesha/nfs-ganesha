set xtic 0,1

# writing an eps file
set terminal postscript eps monochrome "Helvetica" 12
set output "deniel/deniel-ganesha_preloaded_cache.eps"
set xlabel "Number of GANESHA's worker threads"
show xlabel

set ylabel "Throughput (entries listed/sec)"
show ylabel

set title "Thoughput when scanning a 100k entries filesystem\nfrom a preloaded server's cache"

plot [workers=1:17] "deniel/deniel-ganesha_preloaded_cache.dat" notitle with lines


# writing a postscript file
# set terminal postscript
# set output "deniel-ganesha_preloaded_cache.ps"
# replot

# replot


# writing a png file
# set terminal png 
# set output "deniel-ganesha_preloaded_cache.png"
# replot

