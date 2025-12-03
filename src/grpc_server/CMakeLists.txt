#SPDX - License - Identifier : LGPL - 3.0 - or -later
#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
#
#Copyright IBM, 2025
#Contributor : Avani Rateria < arateria @redhat>
#
#This program is free software; you can redistribute it and / or
#modify it under the terms of the GNU Lesser General Public
#License as published by the Free Software Foundation; either
#version 3 of the License, or (at your option) any later version.
#
#This program is distributed in the hope that it will be useful,
#but WITHOUT ANY WARRANTY; without even the implied warranty of
#MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the GNU
#Lesser General Public License for more details.
#
#You should have received a copy of the GNU Lesser General Public
#License along with this library; if not, write to the Free Software
#Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110 - 1301 USA
#
#-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
#gRPC server Cmake

add_subdirectory(proto)
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(${CMAKE_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROTO_OUT_DIR})

set(SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/GrpcServer.cc
    ${PROTO_SRCS}
    ${PROTO_HDRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/nfsServiceUtil.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/nfsServiceClient.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/nfsServiceServer.cc)

add_library(gRPCLibrary OBJECT ${SOURCE_FILES})

target_link_libraries(gRPCLibrary
                      PRIVATE
                      gRPC::grpc++
                      protobuf::libprotobuf
              grpc++_reflection
                  ${SYSTEM_LIBRARIES}
                sal
            proto_lib)

            target_include_directories(gRPCLibrary PRIVATE
                      ${PROTOBUF_INCLUDE_DIR}
                       ${CMAKE_SOURCE_DIR}/grpc_server/proto)

set_target_properties(gRPCLibrary PROPERTIES COMPILE_FLAGS "-fPIC")

install(TARGETS gRPCLibrary LIBRARY DESTINATION ${LIB_INSTALL_DIR})

