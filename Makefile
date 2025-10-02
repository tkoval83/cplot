# Includes the project configurations
include project.conf

#
# Validating project variables defined in project.conf
#
ifndef PROJECT_NAME
$(error Missing PROJECT_NAME. Put variables at project.conf file)
endif
ifndef BINARY
$(error Missing BINARY. Put variables at project.conf file)
endif
ifndef PROJECT_PATH
$(error Missing PROJECT_PATH. Put variables at project.conf file)
endif


# Gets the Operating system name
OS := $(shell uname -s)
ARCH := $(shell uname -m)
OS_LOWER := $(shell echo $(OS) | tr '[:upper:]' '[:lower:]')

# Default shell
SHELL := bash

# Color prefix for Linux distributions
COLOR_PREFIX := e

ifeq ($(OS),Darwin)
	COLOR_PREFIX := 033
endif

# Color definition for print purpose
BROWN=\$(COLOR_PREFIX)[0;33m
BLUE=\$(COLOR_PREFIX)[1;34m
END_COLOR=\$(COLOR_PREFIX)[0m



# Source code directory structure
BINDIR := bin
SRCDIR := src
LOGDIR := log
LIBDIR := lib
TESTDIR := test

# Install directories
PREFIX ?= /usr/local
DESTDIR ?=
BINDIR_INSTALL ?= $(PREFIX)/bin


# Source code file extension
SRCEXT := c


# Defines the C Compiler (allow environment override)
CC ?= cc


# Defines the language standards for GCC
STD := -std=gnu99 # See man gcc for more options

# Protection for stack-smashing attack
STACK := -fstack-protector-all -Wstack-protector

# Specifies to GCC the required warnings
WARNS := -Wall -Wextra -pedantic -Wformat=2 -Wvla # -pedantic попереджає про відхилення від стандарту

# Build type: debug (default) or release
BUILD ?= debug

# Flags for compiling
CPPFLAGS := -I$(SRCDIR)
ifeq ($(BUILD),release)
	CFLAGS := -O3 $(STD) $(STACK) $(WARNS)
  CPPFLAGS += -DNDEBUG
  DEBUG :=
else
	CFLAGS := -O0 $(STD) $(STACK) $(WARNS)
  CPPFLAGS += -DDEBUG
  DEBUG := -g3
endif

LDFLAGS :=

# Dependency libraries
LIBS := -lm

# Tests are disabled on macOS-only build (no third-party deps)
TEST_LIBS :=
TEST_CFLAGS := -I $(SRCDIR) -include stdarg.h -include stddef.h -include setjmp.h

# Minimal OS-specific tweaks without extra dependencies
ifeq ($(OS),Darwin)
  MACOS_MIN_VER ?= 11.0
  CFLAGS += -mmacosx-version-min=$(MACOS_MIN_VER) -fvisibility=hidden -fstrict-aliasing -pipe
  LDFLAGS += -mmacosx-version-min=$(MACOS_MIN_VER) -Wl,-dead_strip
endif
ifeq ($(OS),Linux)
	ifeq ($(BUILD),release)
		CFLAGS += -D_FORTIFY_SOURCE=2
	endif
endif

# Optional portability toggle: keep binaries broadly compatible by default
# Set PORTABLE=0 to enable CPU-specific tuning and LTO in release builds.
PORTABLE ?= 1
ifeq ($(BUILD),release)
  ifeq ($(PORTABLE),0)
    CFLAGS += -flto -march=native
    LDFLAGS += -flto
  endif
endif



# Tests binary file
TEST_BINARY := $(BINARY)_test_runner



# %.o file names
SOURCES := $(shell find $(SRCDIR) -name '*.$(SRCEXT)')
OBJECTS := $(patsubst $(SRCDIR)/%.$(SRCEXT),$(LIBDIR)/%.o,$(SOURCES))
OBJECTS_NO_MAIN := $(filter-out $(LIBDIR)/main.o,$(OBJECTS))


#
# COMPILATION RULES
#

.PHONY: default all help start valgrind tests clean install uninstall release dist
.PHONY: fmt format
.PHONY: docs doxygen

default: all

# Help message
help:
	@echo "Шаблон C-проєкту"
	@echo
	@echo "Доступні цілі:"
	@echo "    all      - Компілює та створює виконуваний файл"
	@echo "    tests    - Запускає базові smoke-тести"
	@echo "    start    - Створює новий проєкт на основі шаблону"
	@echo "    valgrind - Запускає бінарник під valgrind"
	@echo "    clean    - Прибирає артефакти збірки"
	@echo "    help     - Друкує цю довідку"

# Starts a new project using C project template
start:
	@echo "Створення проєкту: $(PROJECT_NAME)"
	@mkdir -pv $(PROJECT_PATH)
	@echo "Копіювання файлів із шаблону до нової директорії:"
	@cp -rvf ./* $(PROJECT_PATH)/
	@echo
	@echo "Перейдіть до $(PROJECT_PATH) та зберіть проєкт: make"
	@echo "Далі запустіть: bin/$(BINARY) --help"
	@echo "Вдалого хакінгу o/"


# Rule for link and generate the binary file
all: $(OBJECTS)
	@echo -en "$(BROWN)LD $(END_COLOR)";
	@mkdir -p $(BINDIR)
	$(CC) -o $(BINDIR)/$(BINARY) $+ $(DEBUG) $(CFLAGS) $(LDFLAGS) $(LIBS)
	@echo -en "\n--\nBinary file placed at" \
			  "$(BROWN)$(BINDIR)/$(BINARY)$(END_COLOR)\n";


# Rule for object binaries compilation

$(LIBDIR)/%.o: $(SRCDIR)/%.$(SRCEXT)
	@echo -en "$(BROWN)CC $(END_COLOR)";
	@mkdir -p $(dir $@)
	$(CC) $(DEBUG) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF $(patsubst %.o,%.d,$@) -c $< -o $@

# Автоматична підстановка залежностей заголовків
DEPS := $(OBJECTS:.o=.d)
-include $(DEPS)


# Rule for run valgrind tool
valgrind:
	valgrind \
		--track-origins=yes \
		--leak-check=full \
		--leak-resolution=high \
		--log-file=$(LOGDIR)/$@.log \
		$(BINDIR)/$(BINARY)
	@echo -en "\nCheck the log file: $(LOGDIR)/$@.log\n"


# Basic smoke tests without external dependencies
tests: all
	@echo "Запуск базових smoke-тестів..."
	@echo -n "  Тест --help: "
	@if ./$(BINDIR)/$(BINARY) --help >/dev/null 2>&1; then echo "✓ ПРОЙШОВ"; else echo "✗ ПРОВАЛЕНИЙ"; exit 1; fi
	@echo -n "  Тест --version: "
	@if ./$(BINDIR)/$(BINARY) --version >/dev/null 2>&1; then echo "✓ ПРОЙШОВ"; else echo "✗ ПРОВАЛЕНИЙ"; exit 1; fi
	@echo -n "  Тест fonts --list: "
	@if ./$(BINDIR)/$(BINARY) fonts --list >/dev/null 2>&1; then echo "✓ ПРОЙШОВ"; else echo "✗ ПРОВАЛЕНИЙ"; exit 1; fi
	@echo -n "  Тест config --show: "
	@if ./$(BINDIR)/$(BINARY) config --show >/dev/null 2>&1; then echo "✓ ПРОЙШОВ"; else echo "✗ ПРОВАЛЕНИЙ"; exit 1; fi
	@echo -n "  Тест device list: "
	@if ./$(BINDIR)/$(BINARY) device list >/dev/null 2>&1; then echo "✓ ПРОЙШОВ"; else echo "✗ ПРОВАЛЕНИЙ"; exit 1; fi
	@echo -n "  Тест SVG превʼю: "
	@if echo "Test" | ./$(BINDIR)/$(BINARY) print --preview >/dev/null 2>&1; then echo "✓ ПРОЙШОВ"; else echo "✗ ПРОВАЛЕНИЙ"; exit 1; fi
	@echo -n "  Тест PNG превʼю: "
	@if echo "Test" | ./$(BINDIR)/$(BINARY) print --preview --png --output /tmp/cplot-test.png >/dev/null 2>&1; then echo "✓ ПРОЙШОВ"; else echo "✗ ПРОВАЛЕНИЙ"; exit 1; fi
	@rm -f /tmp/cplot-test.png
	@echo "Всі базові тести пройшли успішно! 🎉"


# Rule for cleaning the project
clean:
	@rm -rf $(BINDIR) $(LIBDIR) $(LOGDIR) dist || true

# Install/uninstall
install: all
	install -d $(DESTDIR)$(BINDIR_INSTALL)
	install -m 0755 $(BINDIR)/$(BINARY) $(DESTDIR)$(BINDIR_INSTALL)/$(BINARY)
	@echo "Installed to $(DESTDIR)$(BINDIR_INSTALL)/$(BINARY)"

uninstall:
	@rm -vf $(DESTDIR)$(BINDIR_INSTALL)/$(BINARY)
	@echo "Uninstalled $(DESTDIR)$(BINDIR_INSTALL)/$(BINARY)"

# Release and distribution
release:
	$(MAKE) clean
	$(MAKE) BUILD=release all
	@strip $(BINDIR)/$(BINARY) 2>/dev/null || true

TARNAME := $(BINARY)-$(shell grep -E '^VERSION\s*:=' project.conf | awk '{print $$3}')-$(OS_LOWER)-$(ARCH).tar.gz

dist: release
	@mkdir -p dist
	@tar -czf dist/$(TARNAME) LICENSE README.md hershey -C $(BINDIR) $(BINARY)
	@# Generate SHA256 checksum (portable between macOS and Linux)
	@(command -v shasum >/dev/null && shasum -a 256 dist/$(TARNAME) > dist/$(TARNAME).sha256 || \
	 command -v sha256sum >/dev/null && sha256sum dist/$(TARNAME) > dist/$(TARNAME).sha256 || true)
	@echo "Created dist/$(TARNAME)"

# Code formatting (requires clang-format in PATH)
fmt format:
	@command -v clang-format >/dev/null 2>&1 || { \
	  echo "clang-format not found. Install on macOS: brew install clang-format"; \
	  exit 2; \
	}
	@echo "Formatting C sources with clang-format..."
	clang-format -i -style=file $(SRCDIR)/*.$(SRCEXT) $(SRCDIR)/*.h
	@echo "Done."

# Generate API documentation with Doxygen (if installed)
docs doxygen:
	@command -v doxygen >/dev/null 2>&1 || { \
	  echo "doxygen not found. Install: macOS 'brew install doxygen' or Linux 'sudo apt-get install doxygen'"; \
	  exit 2; \
	}
	@echo "Generating API documentation (Doxygen)..."
	doxygen Doxyfile
	@echo "Documentation generated in docs/api/html" 
