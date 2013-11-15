
########### next target ###############

SET(cidr_STAT_SRCS
   cidr_addr.c
   cidr_compare.c
   cidr_from_str.c
   cidr_get.c
   cidr_inaddr.c
   cidr_mem.c
   cidr_misc.c
   cidr_net.c
   cidr_num.c
   cidr_to_str.c
   cidr_pow2_p.h
)

add_library(cidr STATIC ${cidr_STAT_SRCS})

########### install files ###############
