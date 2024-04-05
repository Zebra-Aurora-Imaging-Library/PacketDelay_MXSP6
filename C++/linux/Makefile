TARGET	= PacketDelay
TARGET_OBJECTS= PacketDelay.o
TARGET_INCLUDES = 

CFLAGS   = -I$(MILDIR)/include -g -Werror $(USER_CFLAGS)
CXXFLAGS = $(CFLAGS) -std=c++11
LDFLAGS  = -L$(MILDIR)/lib -lmil -lmilim

.PHONY   = all clean


%.o: %.cpp $(TARGET_INCLUDES)
	$(CXX) -c -o $@ $< $(CXXFLAGS)

$(TARGET): $(TARGET_OBJECTS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

all: $(TARGET)

clean:
	-rm -f $(TARGET) $(TARGET_OBJECTS)

