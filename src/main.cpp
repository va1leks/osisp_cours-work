#include <iostream>
#include <filesystem>
#include <thread>
#include <fstream>
#include <vector>
#include <string>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>

namespace fs = std::filesystem;
const int BUFFER_SIZE = 4096;

// ANSI цветовые коды
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_GRAY    "\033[90m"

int local_port;
std::vector<std::pair<std::string, int>> peers;
int current_peer = -1;

// ==== Вывод заголовка ====
void print_header(const std::string& title) {
    std::cout << COLOR_CYAN << "=== " << title << " ===" << COLOR_RESET << "\n";
}

// ==== Просмотр файлов ====
void list_files(const std::string& dir) {
    print_header("СОДЕРЖИМОЕ ПАПКИ " + dir);

    size_t total_files = 0;
    size_t total_dirs = 0;

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (fs::is_directory(entry)) {
            std::cout << COLOR_BLUE << " 📁 " << COLOR_RESET
            << fs::relative(entry.path(), dir).string() << "\n";
            total_dirs++;
        } else {
            std::cout << " 📄 " << fs::relative(entry.path(), dir).string()
            << COLOR_GRAY << " (" << fs::file_size(entry) << " bytes)" << COLOR_RESET << "\n";
            total_files++;
        }
    }

    std::cout << COLOR_GREEN << "\nИтого: " << total_dirs << " папок, "
    << total_files << " файлов" << COLOR_RESET << "\n";
}

// ==== Отправка файла или папки ====
void send_all(const std::string& ip, int port, const std::string& target) {
    std::string full_path = "shared/" + target;
    if (!fs::exists(full_path)) {
        std::cerr << COLOR_RED << "Ошибка: Файл или папка не найдены: " << full_path << COLOR_RESET << "\n";
        return;
    }

    std::vector<fs::path> paths;
    if (fs::is_regular_file(full_path)) {
        paths.push_back(fs::path(target));
    } else {
        for (const auto& p : fs::recursive_directory_iterator(full_path)) {
            paths.push_back(fs::relative(p.path(), "shared"));
        }
    }

    print_header("ОТПРАВКА НА " + ip + ":" + std::to_string(port));

    size_t success_count = 0;
    size_t fail_count = 0;

    for (const auto& rel_path : paths) {
        std::string full = "shared/" + rel_path.string();
        bool is_dir = fs::is_directory(full);

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::cerr << COLOR_RED << " ✘ Ошибка сокета для " << rel_path.string() << COLOR_RESET << "\n";
            fail_count++;
            continue;
        }

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);

        if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            std::cerr << COLOR_RED << " ✘ Ошибка подключения для " << rel_path.string() << COLOR_RESET << "\n";
            close(sock);
            fail_count++;
            continue;
        }

        char type = is_dir ? 'D' : 'F';
        send(sock, &type, 1, 0);
        send(sock, rel_path.string().c_str(), rel_path.string().size(), 0);
        send(sock, "\n", 1, 0);

        if (!is_dir) {
            std::ifstream in(full, std::ios::binary);
            char buffer[BUFFER_SIZE];
            while (in.read(buffer, sizeof(buffer)) || in.gcount()) {
                send(sock, buffer, in.gcount(), 0);
            }
        }

        close(sock);
        std::cout << COLOR_GREEN << " ✔ " << (is_dir ? "Папка " : "Файл  ")
        << std::left << std::setw(40) << rel_path.string()
        << COLOR_RESET << " отправлен\n";
        success_count++;
    }

    std::cout << COLOR_YELLOW << "\nРезультат: " << success_count << " успешно, "
    << fail_count << " с ошибками" << COLOR_RESET << "\n";
}

// ==== Обработка клиента ====
void handle_client(int client) {
    char type;
    if (recv(client, &type, 1, 0) <= 0) { close(client); return; }

    std::string rel_path;
    char ch;
    while (recv(client, &ch, 1, 0) > 0 && ch != '\n') {
        rel_path += ch;
    }

    std::string out_path = "received/" + rel_path;

    if (type == 'D') {
        fs::create_directories(out_path);
        std::cout << COLOR_BLUE << "[Сервер] 📁 Принята папка: " << rel_path << COLOR_RESET << "\n";
    } else if (type == 'F') {
        fs::create_directories(fs::path(out_path).parent_path());
        std::ofstream out(out_path, std::ios::binary);
        char buffer[BUFFER_SIZE];
        ssize_t n;
        while ((n = recv(client, buffer, sizeof(buffer), 0)) > 0) {
            out.write(buffer, n);
        }
        std::cout << COLOR_GREEN << "[Сервер] 📄 Принят файл: " << rel_path
        << COLOR_GRAY << " (" << fs::file_size(out_path) << " bytes)" << COLOR_RESET << "\n";
    }

    close(client);
}

// ==== Сервер ====
void server_thread(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(EXIT_FAILURE);
    }

    listen(server_fd, 10);
    std::cout << COLOR_GREEN << "\n[Сервер] Запущен и слушает порт " << port << COLOR_RESET << "\n";

    fs::create_directory("received");

    while (true) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) { perror("accept"); continue; }

        std::thread(handle_client, client).detach();
    }
}

// ==== Добавить нового пира ====
void add_peer() {
    print_header("ДОБАВЛЕНИЕ НОВОГО ПИРА");

    std::string ip;
    int port;
    std::cout << "IP адрес узла: ";
    std::cin >> ip;
    std::cout << "Порт узла: ";
    std::cin >> port;

    peers.emplace_back(ip, port);
    current_peer = peers.size() - 1;

    std::cout << COLOR_GREEN << "\nПир успешно добавлен!\n"
    << "Индекс: " << current_peer << "\n"
    << "Адрес: " << ip << ":" << port << COLOR_RESET << "\n";
}

// ==== Выбрать пира ====
void select_peer() {
    if (peers.empty()) {
        std::cout << COLOR_YELLOW << "Список пиров пуст. Используйте команду 'add' для добавления." << COLOR_RESET << "\n";
        return;
    }

    print_header("ВЫБОР ПИРА");
    std::cout << "Текущий выбранный пир: ";
    if (current_peer == -1) {
        std::cout << COLOR_RED << "не выбран" << COLOR_RESET << "\n\n";
    } else {
        std::cout << COLOR_GREEN << current_peer << ". " << peers[current_peer].first
        << ":" << peers[current_peer].second << COLOR_RESET << "\n\n";
    }

    std::cout << "Доступные пиры:\n";
    for (size_t i = 0; i < peers.size(); ++i) {
        std::cout << " " << (i == current_peer ? COLOR_GREEN ">" : " ") << COLOR_RESET
        << i << ". " << peers[i].first << ":" << peers[i].second << "\n";
    }

    std::cout << "\nВведите индекс пира (" << COLOR_YELLOW << "или -1 чтобы сбросить выбор" << COLOR_RESET << "): ";
    std::cin >> current_peer;

    if (current_peer < -1 || current_peer >= (int)peers.size()) {
        std::cout << COLOR_RED << "Неверный индекс!" << COLOR_RESET << "\n";
        current_peer = -1;
    } else if (current_peer == -1) {
        std::cout << COLOR_YELLOW << "Выбор пира сброшен" << COLOR_RESET << "\n";
    } else {
        std::cout << COLOR_GREEN << "Выбран пир " << current_peer << ": "
        << peers[current_peer].first << ":" << peers[current_peer].second << COLOR_RESET << "\n";
    }
}

// ==== Главное меню ====
void print_menu() {
    print_header("ГЛАВНОЕ МЕНЮ");
    std::cout << " 1. " << COLOR_CYAN << "add" << COLOR_RESET << "    - Добавить нового пира\n";
    std::cout << " 2. " << COLOR_CYAN << "select" << COLOR_RESET << " - Выбрать пира\n";
    std::cout << " 3. " << COLOR_CYAN << "list" << COLOR_RESET << "   - Просмотр файлов\n";
    std::cout << " 4. " << COLOR_CYAN << "send" << COLOR_RESET << "   - Отправить файл/папку\n";
    std::cout << " 5. " << COLOR_CYAN << "exit" << COLOR_RESET << "   - Выход\n";

    std::cout << "\nТекущий пир: ";
    if (current_peer == -1) {
        std::cout << COLOR_RED << "не выбран" << COLOR_RESET;
    } else {
        std::cout << COLOR_GREEN << current_peer << ". " << peers[current_peer].first
        << ":" << peers[current_peer].second << COLOR_RESET;
    }
    std::cout << "\n";
}

// ==== Главная функция ====
int main() {
    std::cout << COLOR_CYAN << "\n=== P2P Файлообменник ===" << COLOR_RESET << "\n";
    std::cout << "Введите порт для приема файлов: ";
    std::cin >> local_port;

    std::thread(server_thread, local_port).detach();
    fs::create_directory("shared");

    while (true) {
        print_menu();

        std::string cmd;
        std::cout << "\nВведите команду: ";
        std::cin >> cmd;

        if (cmd == "exit"||cmd=="5") break;
        else if (cmd == "add"||cmd=="1") add_peer();
        else if (cmd == "select"||cmd=="2") select_peer();
        else if (cmd == "list"||cmd=="3") list_files("shared");
        else if (cmd == "send"||cmd=="4") {
            if (current_peer == -1) {
                std::cout << COLOR_RED << "Ошибка: не выбран пир!" << COLOR_RESET << "\n";
                continue;
            }
            std::string name;
            std::cout << "Введите имя файла/папки для отправки: ";
            std::cin >> name;
            send_all(peers[current_peer].first, peers[current_peer].second, name);
        }
        else {
            std::cout << COLOR_RED << "Неизвестная команда. Попробуйте снова." << COLOR_RESET << "\n";
        }
    }

    std::cout << COLOR_CYAN << "\nЗавершение работы..." << COLOR_RESET << "\n";
    return 0;
}
