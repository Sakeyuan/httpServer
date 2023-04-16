 #搜索所有cpp文件
 file(GLOB SOURCES *.cpp *.h)

 # 添加可执行文件
 add_executable(${PROJECT_NAME} ${SOURCES})
 
 #设置链接库
 target_link_libraries(${PROJECT_NAME} pthread)
 