#include <core.h>
#include <clocale>
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    std::setlocale(LC_ALL, ".UTF-8");
#endif
    return sdrpp_main(argc, argv);
}