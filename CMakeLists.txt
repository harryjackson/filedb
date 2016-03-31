PROJECT(fdb)
cmake_minimum_required(VERSION 3.3)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -Wall -pedantic-errors -std=c11 -Weverything -Wno-float-equal -Wno-padded -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -Wno-missing-noreturn -Wno-unneeded-internal-declaration -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-unused -Wno-unused-macros -Wno-switch-enum -Wno-switch -Wno-reserved-id-macro") 
add_subdirectory(./src)
