if (USE_RADOS_RECOV)
  add_executable(ganesha-rados-grace ganesha-rados-grace.c)
  include_directories(${RADOS_INCLUDE_DIR})
  target_link_libraries(ganesha-rados-grace rados_grace ${RADOS_LIBRARIES})
  install(TARGETS ganesha-rados-grace COMPONENT tools DESTINATION bin)
endif(USE_RADOS_RECOV)

########### install files ###############

if(USE_TOOL_MULTILOCK)
  add_subdirectory(multilock)
endif(USE_TOOL_MULTILOCK)
