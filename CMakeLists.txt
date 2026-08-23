cmake_minimum_required(VERSION 3.15)

project(imgui)

# Dear ImGui
set(IMGUI_DIR .)
file(GLOB IMGUI_SRC 
	"${IMGUI_DIR}/*.cpp" 
	"${IMGUI_DIR}/*.h"
	"${IMGUI_DIR}/misc/cpp/*.cpp"
	)

add_library(imgui STATIC ${IMGUI_SRC})

target_include_directories(imgui PUBLIC ${IMGUI_DIR})