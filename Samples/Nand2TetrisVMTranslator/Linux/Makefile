
LINKER_FLAGS = 

CFLAGS = -I../../../Src/Includes/ -I../../../Src/NOMoneStdLib/Includes/ -DDESKTOP -DNVERBOSE=1 -DNPROFILE_MEMORY=3 -g -MMD# g=>generate debug info. MMD=>generate dependency files.

# Clang is more picky. Using it can help us pin-point more issues.
CC = clang

# Optimization flags,
#CFLAGS += -s -O3 -fdata-sections -ffunction-sections 
#LINKER_FLAGS += -Wl,--gc-sections -Wl,--strip-all

SOURCES = \
	$(wildcard ../../../Src/*.c) \
	$(wildcard ../../../Src/NOMoneStdLib/*.c) \
	$(wildcard ../../../Src/NOMoneStdLib/Backends/Linux/*.c) \
	$(wildcard ../Src/*.c) \

OBJECTS = $(SOURCES:.c=.o)

TARGET = VMTranslate.o

# Targets start here.
all: $(TARGET)

DEPENDENCIES = $(OBJECTS:.o=.d)
-include $(DEPENDENCIES)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $(OBJECTS) $(LINKER_FLAGS)

clean:
	$(RM) $(TARGET) $(OBJECTS) $(DEPENDENCIES)

depend:
	@$(CC) $(CFLAGS) -MM $(SOURCES)
        
# list targets that do not create files (but not all makes understand .PHONY)
.PHONY:        all clean depend

# Dependences (call 'make depend' to generate); do not delete:
# Build for these is implicit, no need to specify compiler command lines.
