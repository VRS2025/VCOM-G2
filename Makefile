# VCOM_G — гитарный мультиэффект на Daisy Seed (пресеты под треки + лупер + тюнер).
# Железо и DSP-блоки переиспользуем из соседнего GuitarPedal (libDaisy/DaisySP + модули).

TARGET = VCOM_G

# RTNeural + eigen дают крупный бинарь -> грузим в SRAM через Daisy bootloader.
APP_TYPE = BOOT_SRAM

GP_DIR        = GuitarPedal
# Тяжёлые библиотеки (libDaisy/DaisySP/RTNeural/eigen, уже собранные) — общие с VCOM-G1.
LIB_DIR       = ../VCOM-G1/GuitarPedal
LIBDAISY_DIR  = $(LIB_DIR)/libDaisy
DAISYSP_DIR   = $(LIB_DIR)/DaisySP

# Наши исходники + переиспользуемые модули из GuitarPedal.
CPP_SOURCES = my_pedal.cpp \
              $(GP_DIR)/Hardware-Modules/base_hardware_module.cpp \
              $(GP_DIR)/Hardware-Modules/guitar_pedal_125b.cpp \
              $(GP_DIR)/Effect-Modules/base_effect_module.cpp \
              $(GP_DIR)/Effect-Modules/amp_module.cpp \
              $(GP_DIR)/Effect-Modules/ImpulseResponse/ImpulseResponse.cpp \
              $(GP_DIR)/Effect-Modules/ImpulseResponse/dsp.cpp \
              $(GP_DIR)/Util/audio_utilities.cpp

C_INCLUDES += -I$(GP_DIR)/Hardware-Modules
C_INCLUDES += -I$(GP_DIR)/Effect-Modules
C_INCLUDES += -I$(GP_DIR)/Util

# -O3 (скорость), а не -Os (размер): резко ускоряет нейроинференс RTNeural+eigen.
OPT = -O3

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# RTNeural (нейросетевой инференс усилителя) + eigen — общие с VCOM-G1.
C_INCLUDES += -I$(LIB_DIR)/eigen
C_INCLUDES += -I$(LIB_DIR)/RTNeural
CPPFLAGS += -DRTNEURAL_DEFAULT_ALIGNMENT=8 -DRTNEURAL_NO_DEBUG=1 -DRTNEURAL_USE_EIGEN=1
# Быстрая FP-математика (как в оригинальном bkshepherd Makefile).
CPPFLAGS += -ffast-math -ffinite-math-only
