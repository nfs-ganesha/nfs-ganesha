find_package(BISON)
find_package(FLEX)

add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

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
   conf_url.c
   analyse.h
)

if(RADOS_URLS)
  set(config_parsing_STAT_SRCS
    ${config_parsing_STAT_SRCS}
    conf_url_rados.c
    )

  include_directories(${RADOS_INCLUDE_DIR})
endif(RADOS_URLS)

add_library(config_parsing STATIC
  ${config_parsing_STAT_SRCS}
  ${BISON_ConfigParser_OUTPUTS}
  ${FLEX_ConfigScanner_OUTPUTS}
)

if(RADOS_URLS)
  target_link_libraries(config_parsing ${RADOS_LIBRARIES})
endif(RADOS_URLS)

add_sanitizers(config_parsing)

########### next target ###############

SET(verif_syntax_SRCS
   verif_syntax.c
)

add_executable(verif_syntax EXCLUDE_FROM_ALL ${verif_syntax_SRCS})
add_sanitizers(verif_syntax)

target_link_libraries(verif_syntax config_parsing log ${CMAKE_THREAD_LIBS_INIT})

if(RADOS_URLS)
  target_link_libraries(config_parsing ${RADOS_LIBRARIES})
endif(RADOS_URLS)

########### next target ###############

SET(test_parsing_SRCS
   test_parse.c
)

add_executable(test_parsing EXCLUDE_FROM_ALL ${test_parsing_SRCS})
add_sanitizers(test_parsing)

target_link_libraries(test_parsing config_parsing log ${CMAKE_THREAD_LIBS_INIT})


########### install files ###############
