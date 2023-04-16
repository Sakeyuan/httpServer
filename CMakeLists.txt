 #指定CMake版本
cmake_minimum_required(VERSION 3.5)

#设置项目名称
project(webServer) 

#设置C++11标准
set(CMAKE_CXX_STANDARD 11)

#向当前工程添加存放源文件的子目录，并可以指定中间二进制和目标二进制存放的位置bin
add_subdirectory(src bin)



