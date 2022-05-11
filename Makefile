CXXFLAGS := -std=c++11 -Wall -Wextra -pedantic-errors -O2 $(CXXFLAGS)
LDFLAGS := -Wl,-s $(LDFLAGS)
ifdef MINGW_PREFIX
  LDFLAGS := -municode -static $(LDFLAGS)
  TARGET ?= psisiarc.exe
else
  LDFLAGS := $(LDFLAGS)
  TARGET ?= psisiarc
endif

all: $(TARGET)
$(TARGET): psisiarc.cpp util.cpp util.hpp psiarchiver.cpp psiarchiver.hpp psiextractor.cpp psiextractor.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH) -o $@ psisiarc.cpp util.cpp psiarchiver.cpp psiextractor.cpp
clean:
	$(RM) $(TARGET)
