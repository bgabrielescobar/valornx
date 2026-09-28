# Compilación para PC (Linux/WSL): pruebas del núcleo y simulador de la interfaz.
#   make test      -> compila y ejecuta las pruebas
#   make sim       -> compila el simulador (capturas PNG de la app y del overlay)
CXX      ?= g++
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
BUILD    := build

CORE    := $(wildcard ../source/core/*.cpp)
UI      := $(wildcard ../source/ui/*.cpp)
OVERLAY := ../source/overlay/panel.cpp ../source/overlay/painter.cpp ../source/overlay/ui_kit.cpp \
           ../source/overlay/search_pages.cpp

# FreeType: cabeceras de devkitPro (son las mismas en PC) + la biblioteca del sistema.
FT_INC ?= /opt/devkitpro/portlibs/switch/include/freetype2
FT_LIB ?= $(firstword $(wildcard /usr/lib/x86_64-linux-gnu/libfreetype.so /usr/lib/x86_64-linux-gnu/libfreetype.so.6))
# stb_truetype (el mismo que usa libtesla) para el emulador del overlay.
STB_INC := ../overlay/libtesla

.PHONY: test sim clean

test: $(BUILD)/test_core
	./$(BUILD)/test_core

$(BUILD)/test_core: $(CORE) test_core.cpp mock_game.cpp cheat_vm.cpp pc_time.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^

sim: $(BUILD)/sim

$(BUILD)/sim: $(CORE) $(UI) $(OVERLAY) sim_main.cpp mock_game.cpp cheat_vm.cpp $(BUILD)/tesla_emu.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -DVNX_PC -I$(FT_INC) -o $@ $^ $(FT_LIB) -lz

# stb_truetype da avisos que no son nuestros: se compila aparte y sin -Wextra.
$(BUILD)/tesla_emu.o: tesla_emu.cpp tesla_emu.hpp | $(BUILD)
	$(CXX) -std=c++17 -O2 -g -I$(STB_INC) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
