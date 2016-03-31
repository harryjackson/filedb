cmake_minimum_required(VERSION 3.3)
project(fdb C)
set(cool_srcs 
  ${CMAKE_CURRENT_SOURCE_DIR}/fdb.c
  ${CMAKE_CURRENT_SOURCE_DIR}/fdb.h
  ${CMAKE_CURRENT_SOURCE_DIR}/fdb_impl.c
  #${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c
  #${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.h
)
add_executable(fdb
  ${CMAKE_CURRENT_SOURCE_DIR}/fdb.c
  ${CMAKE_CURRENT_SOURCE_DIR}/fdb.h
  ${CMAKE_CURRENT_SOURCE_DIR}/fdb_impl.c
  #${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c
  #${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.h
)
add_test(test_fdb  fdb)
target_link_libraries(fdb "-lapr-1 -laprutil-1 -lsqlite3") 
