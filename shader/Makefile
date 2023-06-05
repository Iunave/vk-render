SHELL=/bin/bash
BUILD_MODE ?= debug

shaders := $(wildcard src/*)
function_shaders = $(wildcard src/*.glsl)
graphics_shaders := $(filter %.vert %.geom %.frag %.comp, $(shaders))
spirv_shaders := $(patsubst src/%, bin/%, $(graphics_shaders))
spirv_shaders := $(addsuffix .spv, $(spirv_shaders))

all: bin/shader_glob.o shader_include.hpp
.PHONY: all

compile_shaders: $(spirv_shaders)
.PHONY: compile_shaders

bin/shader_glob.o: shader_glob.asm
ifeq ($(BUILD_MODE), debug)
	nasm -g -F dwarf -f elf64 -i bin/ -o bin/shader_glob.o -Lm -l bin/shader_glob.lst shader_glob.asm
else
	nasm -F dwarf -f elf64 -i bin/ -o bin/shader_glob.o shader_glob.asm
endif
	
shader_glob.asm: generate_glob.sh $(spirv_shaders)
	./generate_glob.sh $(patsubst bin/%, %, $(spirv_shaders))

shader_include.hpp: generate_header.sh $(graphics_shaders)
	./generate_header.sh $(patsubst src/%, %, $(graphics_shaders))

.SECONDEXPANSION:
.DELETE_ON_ERROR:
$(spirv_shaders): $$(patsubst bin/%.spv, src/%, $$@) $(function_shaders)
ifeq ($(BUILD_MODE), debug)
	glslc -g -O $< -o $@
else
	glslc -O $< -o $@
endif

clean:
	rm -f shader_glob.asm shader_include.hpp bin/shader_glob.o bin/shader_glob.lst $(spirv_shaders)
.PHONY: clean
