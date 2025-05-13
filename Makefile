# Компилятор и флаги
CXX := g++
CXXFLAGS := -Wall -std=c++17 -pthread

# Пути и файлы
SRC_DIR := src
BUILD_DIR := build
TARGET := $(BUILD_DIR)/p2p
SRC := $(SRC_DIR)/main.cpp

# Цель по умолчанию
all: $(TARGET)

# Создание директорий и компиляция
$(TARGET): $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

# Запуск программы
run: $(TARGET)
	./$(TARGET)

# Очистка
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean
