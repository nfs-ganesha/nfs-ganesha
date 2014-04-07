find_package(BISON)
find_package(FLEX)

BISON_TARGET(
  ConfigParser
  ${CMAKE_CURRENT_SOURCE_DIR}/conf_yacc.y
  ${CMAKE_CURRENT_BINARY_DIR}/conf_yacc.c
  COMPILE_FLAGS "--defines -pganesha_yy"
)

FLEX_TARGET(
  ConfigScanner
  ${CMAKE_CURRENT_SOURCE_DIR}/conf_lex.l
  ${CMAKE_CURRENT_BINARY_DIR}/conf_lex.c
  COMPILE_FLAGS "-Pganeshun_yy -olex.yy.c"
)

ADD_FLEX_BISON_DEPENDENCY(ConfigScanner ConfigParser)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

########### next target ###############

SET(config_parsing_STAT_SRCS
   analyse.c
   config_parsing.c
   analyse.h
)

add_library(config_parsing STATIC
  ${config_parsing_STAT_SRCS}
  ${BISON_ConfigParser_OUTPUTS}
  ${FLEX_ConfigScanner_OUTPUTS}
)


########### next target ###############

SET(verif_syntax_SRCS
   verif_syntax.c
)

add_executable(verif_syntax EXCLUDE_FROM_ALL ${verif_syntax_SRCS})

target_link_libraries(verif_syntax config_parsing log ${CMAKE_THREAD_LIBS_INIT})


########### next target ###############

SET(test_parsing_SRCS
   test_parse.c
)

add_executable(test_parsing EXCLUDE_FROM_ALL ${test_parsing_SRCS})

target_link_libraries(test_parsing config_parsing log ${CMAKE_THREAD_LIBS_INIT})


########### install files ###############
