"
" Vim syntax definition for ganesha NFS server's configuration file
" Author: Thomas LEIBOVICI (thomas.leibovici@cea.fr)
" Date: 2008/02/25
" Version: 1.0
"

" We are mostly using strncasecmp 
syn case ignore

" Comments :
" Anywhere, even at toplevel
syn match comment /\s*#.*$/

" Include directive:
syn match include /%include/ contained
syn region include_line start=/%include/ end=/\n/ contains=include,value_str2 

" Value
syn region value_str1 start=/'/ end=/'/ skip=/\\'/ contained
syn region value_str2 start=/"/ end=/"/ skip=/\\"/ contained
syn match  value_any  /[^;'"]+/ contained 

" This parse the second part of a 'key = value ;' expression
" key = value ;
"     ^^^^^^^^^
syn region affect matchgroup=Quote start=/=/ end=/;/ skip=/ / contains=value_.*

" This parse the first part of a 'key = value ;' expression
" key = value ;
" ^^^
syn match keyname "\I\i*" contained contains=known_keyname

" Known parameters for GANESHA
syn keyword known_keyname contained Alphabet_Length Attr_Expiration_Time Cache_Directory Core_Dump_Size DebugLevel Df_HighWater Df_LowWater DirData_Prealloc_PoolSize Directory_Expiration_Time Directory_Lifetime Drop_IO_Errors Drop_Inval_Errors Dump_Stats_Per_Client DupReq_Expiration Emergency_Grace_Delay Entry_Prealloc_PoolSize Entry_Prealloc_PoolSize Expiration_Time FH_Expire File_Lifetime Inactivity_Before_Flush Index_Size KeytabPath LRU_DupReq_Prealloc_PoolSize LRU_Nb_Call_Gc_invalid LRU_Nb_Call_Gc_invalid LRU_Pending_Job_Prealloc_PoolSize LRU_Prealloc_PoolSize LRU_Prealloc_PoolSize Lease_Lifetime Lifetime LogFile MNT_Port MNT_Program Map Map Max_Fd NFS_Port NFS_Program NbEntries_HighWater NbEntries_LowWater Nb_Before_GC Nb_Call_Before_GC Nb_Call_Before_GC Nb_Client_Id_Prealloc Nb_DupReq_Before_GC Nb_DupReq_Prealloc Nb_IP_Stats_Prealloc Nb_MaxConcurrentGC Nb_Worker OpenFile_Retention ParentData_Prealloc_PoolSize Pending_Job_Prealloc Prealloc_Node_Pool_Size Prealloc_Node_Pool_Size PrincipalName Refresh_FSAL_Force Returns_ERR_FH_EXPIRED Runtime_Interval State_v4_Prealloc_PoolSize Stats_File_Path Stats_Per_Client_Directory Stats_Update_Delay Symlink_Expiration_Time Use_Getattr_Directory_Invalidation Use_OpenClose_cache Use_Test_Access AuthMech BusyDelay BusyRetries CredentialLifetime DB_Host DB_Login DB_Name DB_Port DB_keytab DebugLevel DebugPath Enable_Extra_Alloc Enable_GC Enable_OnDemand_Alloc Export_FSAL_calls_detail Export_buddy_stats Export_cache_inode_calls_detail Export_cache_stats Export_maps_stats Export_nfs_calls_detail Export_requests_stats GC_Keep_Factor GC_Keep_Min KeytabPath LogFile MaxConnections Max_FS_calls NFS_Port NFS_Proto NFS_RecvSize NFS_SendSize NFS_Service NumRetries Open_by_FH_Working_Dir Page_Size PrincipalName Product_Id Retry_SleepTime ReturnInconsistentDirent Snmp_Agentx_Socket Snmp_adm_log Srv_Addr auth_phrase auth_proto auth_xdev_export cansettime client_name community dot_dot_root enable_descriptions enc_phrase enc_proto fs_root_group fs_root_mode fs_root_owner link_support maxread maxwrite microsec_timeout nb_retries predefined_dir snmp_getbulk_count snmp_server snmp_version symlink_support umask username Access Access_Type Anonymous_root_uid Cache_Data Export_id FS_Specific Filesystem_id MaxCacheSize MaxOffsetRead MaxOffsetWrite MaxRead MaxWrite NFS_Protocols NOSGID NOSUID Path PrefRead PrefReaddir PrefWrite PrivilegedPort Pseudo Root_Access SecType Tag Transport_Protocols

" Block
syn region block start=/{/ end=/}/ contains=affect,comment,keyname

" known block names for GANESHA 
syn keyword BlockName contained BUDDY_MALLOC CacheInode_Client CacheInode_GC_Policy CacheInode_Hash EXPORT FSAL FileContent_Client FileContent_GC_Policy FileContent_Param FileSystem GHOST_FS GidMapper_Cache Groups HPSS Hosts NFS_Core_Param NFS_DupReq_Hash NFS_IP_Name NFS_KRB5 NFS_Worker_Param NFSv4 NFSv4_ClientId_Cache NFSv4_Proxy NFSv4_StateId_Cache POSIX SNMP SNMP_ADM TEMPLATE UidMapper_Cache Users

" Block label
syn match blocklbl "\I\i*" contains=BlockName

" used when seeking in the file (to know if we are in a blocki or not) 
syn sync match RobSync groupthere block "}" 
syn sync minlines=100

hi link affect 	   Number
hi link value_str1 String
hi link value_str2 String 
hi link known_keyname Identifier
hi link BlockName Type 

" Other unknown identifiers
"hi link keyname Error 

" Other unknown block labels 
"hi link blocklbl Error

