aux_source_directory(. SRC)

add_executable(test_ngx_mem_pool ${SRC})
target_link_libraries(test_ngx_mem_pool ngx_mem_pool)
