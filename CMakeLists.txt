cmake_minimum_required(VERSION 3.16)
project(AchievementCase CXX)   # ชื่อ project/exe เปลี่ยนที่บรรทัดนี้ที่เดียว
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm CONFIG REQUIRED)

# stb: vcpkg มี FindStb ให้, ถ้าไม่มี (เช่น apt/brew) ให้หา stb_image.h เอง
find_package(Stb QUIET)
if(NOT Stb_INCLUDE_DIR)
  find_path(Stb_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb)
endif()
if(NOT Stb_INCLUDE_DIR)
  message(FATAL_ERROR "stb_image.h not found. Install stb (vcpkg: stb)")
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# สร้าง ProjectPaths.h ไว้ใน build/ (path อ้างอิงจากตำแหน่ง folder จริง ไม่ผูกกับชื่อ folder)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ProjectPaths.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ProjectPaths.h"
  @ONLY)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  "${Stb_INCLUDE_DIR}")

target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL glfw GLEW::GLEW glm::glm)

# ให้ Visual Studio รัน (F5) โดยใช้ root ของโปรเจกต์เป็น working directory
set_target_properties(${PROJECT_NAME} PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
