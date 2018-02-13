if (USE_RADOS_RECOV)
  add_executable(rados_grace_tool rados_grace_tool.c)
  target_link_libraries(rados_grace_tool rados_grace ${RADOS_LIBRARIES})
endif(USE_RADOS_RECOV)

########### install files ###############

if(USE_TOOL_MULTILOCK)
  add_subdirectory(multilock)
endif(USE_TOOL_MULTILOCK)
