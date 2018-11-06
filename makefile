CC = gcc
CC_INC_Release = ../gsl-x86_64-Release
CC_INC_Debug = ../gsl-x86_64-Debug
CC_OPT_Release = -std=c11 -mavx -flto -O3 -Wall
CC_OPT_Debug = -D_DEBUG -mavx -flto -std=c11 -O0 -ggdb -Wall

CXX = g++
CXX_INC_Release = 
CXX_INC_Debug = $(CXX_INC_Release)
CXX_OPT_Release = -std=c++0x -mavx -flto -O3 -Wall
CXX_OPT_Debug = -D_DEBUG -mavx -flto -std=c++0x -O0 -ggdb -Wall

ASM = yasm
ASM_OPT_Release =
ASM_OPT_Debug = -D_DEBUG

LD = $(CC)
LD_INC_Release = ../gsl-x86_64-Release
LD_INC_Debug = ../gsl-x86_64-Debug
LD_LIB_Release = m pthread gsl gslcblas
LD_LIB_Debug = $(LD_LIB_Release)
LD_OPT_Release = -flto
LD_OPT_Debug = -flto

OS = $(shell uname)

ifeq ($(OS),$(addsuffix $(subst CYGWIN,,$(OS)),CYGWIN))
    ASM_OPT_Release += -fwin64
    ASM_OPT_Debug += -fwin64
else ifeq ($(OS),Linux)
    ASM_OPT_Release += -felf64
    ASM_OPT_Debug += -felf64 -gdwarf2
else ifeq ($(OS),Darwin)
    ASM_OPT_Release += -fmacho64
    ASM_OPT_Debug += -fmacho64
endif

TARGET_Release = RegionsMT-Release
TARGET_Debug = RegionsMT-Debug

OBJ_DIR_Release = ./obj-release
OBJ_DIR_Debug = ./obj-debug
SRC_DIR = ./src

CONFIG = Release Debug

rwildcard = $(wildcard $(addsuffix $2,$1)) $(foreach d,$(wildcard $(addsuffix *,$1)),$(call rwildcard,$d/,$2))
struct = $(sort $(patsubst $(SRC_DIR)/%,$($(addprefix OBJ_DIR_,$1))/%,$(dir $(call rwildcard,$(SRC_DIR)/,))))
obj = $(sort $(patsubst $(addprefix $(SRC_DIR)/%,$2),$($(addprefix OBJ_DIR_,$1))/%.o,$(call rwildcard,$(SRC_DIR)/,$(addprefix *,$2))))

.PHONY: all release debug
all: release debug;
release: Release;
debug: Debug;

.PHONY: clean
clean:; $(foreach cfg, $(CONFIG), $(shell rm -rf $($(addprefix OBJ_DIR_,$(cfg)))) $(shell rm -f $($(addprefix TARGET_,$(cfg)))))

define build =
.PHONY: $1
$1: | $(call struct,$1) $($(addprefix TARGET_,$1));
$(call struct,$1):; $$(shell mkdir -p $$@)
$($(addprefix TARGET_,$1)): $(call obj,$1,.c) $(call obj,$1,.cpp) $(call obj,$1,.asm); $(LD) $($(addprefix LD_OPT_, $1)) -o $$@ $$^ $(addprefix -L,$($(addprefix LD_INC_,$1))) $(addprefix -l,$($(addprefix LD_LIB_,$1)))
$(call obj,$1,.c): $($(addprefix OBJ_DIR_,$1))/%.o: $(SRC_DIR)/%.c; $(CC) $($(addprefix CC_OPT_,$1)) $(addprefix -I,$($(addprefix CC_INC_,$1))) -o $$@ -c $$^
$(call obj,$1,.cpp): $($(addprefix OBJ_DIR_,$1))/%.o: $(SRC_DIR)/%.cpp; $(CXX) $($(addprefix CXX_OPT_,$1)) $(addprefix -I,$($(addprefix CXX_INC,$1))) -o $$@ -c $$^
$(call obj,$1,.asm): $($(addprefix OBJ_DIR_,$1))/%.o: $(SRC_DIR)/%.asm; $(ASM) $($(addprefix ASM_OPT_,$1)) -o $$@ $$^
endef

$(foreach cfg, $(CONFIG), $(eval $(call build, $(cfg))))

.PHONY: print-%
print-% :; @echo $* = $($*)
