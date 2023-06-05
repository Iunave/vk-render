SHELL=/bin/bash
BUILD_MODE ?= debug
export BUILD_MODE

PROFILE ?= true

SRC_DIR := source
BUILD_DIR := build
EXEC := $(BUILD_DIR)/exec
HEADERS := $(wildcard $(SRC_DIR)/*.hpp)
SOURCES_CPP := $(wildcard $(SRC_DIR)/*.cpp)
SOURCES_ASM := $(wildcard $(SRC_DIR)/*.asm)
OBJECTS := $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SOURCES_CPP))
OBJECTS += $(patsubst $(SRC_DIR)/%.asm, $(BUILD_DIR)/%.asm.o, $(SOURCES_ASM))
PIPELINE_CACHE := $(BUILD_DIR)/pipeline_cache_data

SHADER_DIR := shader
SHADER_GLOB := $(SHADER_DIR)/bin/shader_glob.o
SHADER_INCLUDE := $(SHADER_DIR)/shader_include.hpp

THIRD_PARTY_DIR := third_party

GLFW_DIR := $(THIRD_PARTY_DIR)/glfw

IMGUI_DIR := $(THIRD_PARTY_DIR)/imgui
IMGUI_BACKEND_DIR := $(IMGUI_DIR)/backends
IMGUI_SOURCES := $(wildcard $(IMGUI_DIR)/*.cpp)
IMGUI_OBJECTS := $(patsubst $(IMGUI_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(IMGUI_SOURCES))
IMGUI_BACKEND_SOURCES := $(IMGUI_BACKEND_DIR)/imgui_impl_glfw.cpp $(IMGUI_BACKEND_DIR)/imgui_impl_vulkan.cpp
IMGUI_BACKEND_OBJECTS := $(BUILD_DIR)/imgui_impl_glfw.o $(BUILD_DIR)/imgui_impl_vulkan.o
IMGUI_SOURCES += $(IMGUI_BACKEND_SOURCES)
IMGUI_OBJECTS += $(IMGUI_BACKEND_OBJECTS)

IMPLOT_DIR := $(THIRD_PARTY_DIR)/implot
IMPLOT_SOURCES := $(wildcard $(IMPLOT_DIR)/*.cpp)
IMPLOT_OBJECTS := $(patsubst $(IMPLOT_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(IMPLOT_SOURCES))

VMA_DIR := $(THIRD_PARTY_DIR)/VulkanMemoryAllocator
VMA_HPP_DIR := $(THIRD_PARTY_DIR)/VulkanMemoryAllocator-Hpp

OBJ_LOADER_DIR := $(THIRD_PARTY_DIR)/tinyobjloader
OBJ_LOADER_HEADERS := $(THIRD_PARTY_DIR)/tinyobjloader/tiny_obj_loader.h
OBJ_LOADER_SOURCES := $(THIRD_PARTY_DIR)/tinyobjloader/tiny_obj_loader.cc
OBJ_LOADER_OBJECTS := $(patsubst $(OBJ_LOADER_DIR)/%.cc, $(BUILD_DIR)/%.o, $(OBJ_LOADER_SOURCES))

ASM := nasm
ASMFLAGS := -F dwarf -f elf64

CXX := clang++
CPPFLAGS := -std=c++20 -O3 -march=native -Wno-nullability-completeness -c -I . -I $(SRC_DIR) -I $(THIRD_PARTY_DIR) -I $(IMGUI_DIR)
LDFLAGS := -lpthread -lfmt -lglfw -lassimp -lX11 -lxcb -lxcb-util -lxcb-icccm -lvulkan

IMGUI_CPPFLAGS := -std=c++20 -O3 -march=native -c -I $(IMGUI_DIR) -I $(IMGUI_BACKEND_DIR)

OBJ_LOADER_CPPFLAGS := -std=c++20 -O3 -march=native -c -I $(OBJ_LOADER_DIR)

ifeq ($(BUILD_MODE), debug)
CPPFLAGS += -g -D DEBUG=1
IMGUI_CPPFLAGS += -g -D DEBUG=1
ASMFLAGS += -g -D DEBUG=1
else
CPPFLAGS += -D NDEBUG=1
IMGUI_CPPFLAGS += -D NDEBUG=1
ASMFLAGS += -D NDEBUG=1
endif

#ifeq ($(PROFILE), true)
#CPPFLAGS += -pg
#IMGUI_CPPFLAGS += -pg
#endif

all: $(EXEC) $(OBJECTS) $(IMGUI_OBJECTS) $(IMPLOT_OBJECTS) $(SHADER_GLOB) $(SHADER_INCLUDE)
.PHONY: all

cheemsit-gui: $(EXEC)
.PHONY: cheemsit-gui

$(EXEC): $(OBJECTS) $(SHADER_GLOB) $(IMGUI_OBJECTS) $(IMPLOT_OBJECTS)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(HEADERS) $(SOURCES_CPP) $(SHADER_INCLUDE)
	$(CXX) $(CPPFLAGS) $< -o $@

$(BUILD_DIR)/%.asm.o: $(SRC_DIR)/%.asm
	$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(IMGUI_DIR)/%.cpp
	$(CXX) $(IMGUI_CPPFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(IMGUI_BACKEND_DIR)/%.cpp
	$(CXX) $(IMGUI_CPPFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(IMPLOT_DIR)/%.cpp
	$(CXX) $(IMGUI_CPPFLAGS) $< -o $@

$(BUILD_DIR)/%.o: $(OBJ_LOADER_DIR)/%.cc
	$(CXX) $(OBJ_LOADER_CPPFLAGS) $< -o $@

$(SHADER_GLOB): FORCE
	$(MAKE) -j -C $(SHADER_DIR) bin/shader_glob.o
FORCE:

$(SHADER_INCLUDE): FORCE
	$(MAKE) -j -C $(SHADER_DIR) shader_include.hpp
FORCE:

clean:
	rm -f $(EXEC) $(OBJECTS) $(IMGUI_OBJECTS) $(PIPELINE_CACHE)
	$(MAKE) -C $(SHADER_DIR) clean
.PHONY: clean
	
