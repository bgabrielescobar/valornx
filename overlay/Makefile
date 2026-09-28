#---------------------------------------------------------------------------------
# ValorNX - overlay para Tesla Menu / Ultrahand (ValorNX.ovl)
# Compilar: make   (necesita devkitPro: devkitA64 + libnx). Usa libtesla (carpeta libtesla/).
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

APP_TITLE	:=	ValorNX
APP_AUTHOR	:=	ValorNX
APP_VERSION	:=	1.3.0

TARGET		:=	ValorNX
BUILD		:=	build
# El overlay usa el mismo núcleo que la app y solo lo necesario de la parte de la Switch.
SOURCES		:=	../source/overlay ../source/core
EXTRA_CPP	:=	../source/switch/switch_game.cpp ../source/switch/nsinfo.cpp ../source/ui/iconmask.cpp
EXTRA_C		:=	../source/switch/dmntcht.c
INCLUDES	:=	../source libtesla

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections -fdata-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__

# libtesla necesita C++20 y RTTI (usa dynamic_cast); sus avisos no son nuestros (-isystem).
CXXFLAGS	:= $(CFLAGS) -std=gnu++20 -fno-exceptions -isystem $(CURDIR)/libtesla

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(notdir $*.map)

LIBS	:= -lnx -lm

LIBDIRS	:= $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(sort $(foreach f,$(EXTRA_CPP) $(EXTRA_C),$(CURDIR)/$(dir $(f))))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))) $(notdir $(EXTRA_C))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp))) $(notdir $(EXTRA_CPP))

export LD	:=	$(CXX)

export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES 	:=	$(OFILES_SRC)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).ovl $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).ovl

$(OUTPUT).ovl	:	$(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS)
	@echo built ... $(notdir $@)

$(OUTPUT).elf	:	$(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
