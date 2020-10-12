add_subdirectory(NFS)
add_subdirectory(XDR)
if(USE_NLM)
	add_subdirectory(NLM)
endif(USE_NLM)
if(USE_RQUOTA)
	add_subdirectory(RQUOTA)
endif(USE_RQUOTA)
if(USE_NFSACL3)
	add_subdirectory(NFSACL)
endif(USE_NFSACL3)

if(USE_9P)
  add_subdirectory(9P)
endif(USE_9P)

########### install files ###############
