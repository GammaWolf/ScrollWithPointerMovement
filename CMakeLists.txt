cmake_minimum_required(VERSION 2.8)

project(MouseMoveToScroll)

find_package(X11 REQUIRED)
link_libraries(${X11_LIBRARIES})
include_directories(${X11_INCLUDE_DIR})

link_libraries(libXi.so)
link_libraries(Xtst.so)
link_libraries(Xfixes)

add_executable(${PROJECT_NAME} "main.c")
