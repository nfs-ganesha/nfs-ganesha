add_subdirectory(NFS)
add_subdirectory(XDR)
if(USE_NLM)
	add_subdirectory(NLM)
endif(USE_NLM)
add_subdirectory(RQUOTA)

if(USE_9P)
  add_subdirectory(9P)
endif(USE_9P)

########### install files ###############
