cmake_minimum_required(VERSION 3.15)
project(es_vm C)

set(CMAKE_C_STANDARD 99)

add_executable(es_vm main.c vm.c vm.h vm_builtins.h vm_builtins.c)