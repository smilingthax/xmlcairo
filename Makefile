SOURCES=xmlcairo.c xmlcairo-apply.c parse-svg-cairo.c ftfont-cairo.c gposkern.c
EXEC=xmlcairo

CPPFLAGS=-O3 -Wall -Wextra
CPPFLAGS+=`pkg-config --cflags libxml-2.0 cairo`
LDFLAGS+=`pkg-config --libs libxml-2.0 cairo` -lm
LDFLAGS+=`pkg-config --libs freetype2`

OBJECTS=$(patsubst %.c,$(PREFIX)%$(SUFFIX).o,\
        $(patsubst %.cpp,$(PREFIX)%$(SUFFIX).o,\
$(SOURCES)))
DEPENDS=$(patsubst %.c,$(PREFIX)%$(SUFFIX).d,\
        $(patsubst %.cpp,$(PREFIX)%$(SUFFIX).d,\
        $(filter-out %.o,""\
$(SOURCES))))

all: $(EXEC)
ifneq "$(MAKECMDGOALS)" "clean"
  -include $(DEPENDS)
endif

clean:
	rm -f $(EXEC) $(OBJECTS) $(DEPENDS)

%.d: %.c
	@$(CC) $(CPPFLAGS) -MM -MT"$@" -MT"$*.o" -o $@ $<  2> /dev/null

%.d: %.cpp
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MM -MT"$@" -MT"$*.o" -o $@ $<  2> /dev/null

$(EXEC): $(OBJECTS) main.c
	$(CC) -o $@ $^ $(LDFLAGS)   $(CPPFLAGS)

