cmake_minimum_required(VERSION 3.15)
project(memPool)

# set(CMAKE_CXX_FLAGS "$ {CMAKE_CXX_FLAGS} -fPIC")

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

include_directories(${PROJECT_SOURCE_DIR}/ngx_mem_pool)
include_directories(${PROJECT_SOURCE_DIR}/sgi_stl_mem_pool)
link_directories(${PROJECT_SOURCE_DIR}/lib)

add_subdirectory(ngx_mem_pool)
add_subdirectory(sgi_stl_mem_pool)
add_subdirectory(test_ngx_mem_pool)

