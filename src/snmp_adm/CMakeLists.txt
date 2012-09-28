
########### next target ###############

SET(snmp_adm_STAT_SRCS
   config_daemon.h
   get_set_proc.h
   instance_handler.c
   instance_handler.h
   libdaemon.c
   parse_type.c
   parse_type.h
   register.c
   register.h
)

add_library(snmp_adm STATIC ${snmp_adm_STAT_SRCS})


########### install files ###############
