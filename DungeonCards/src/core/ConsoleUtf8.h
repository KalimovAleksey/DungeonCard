#pragma once

// Windows-консоль по умолчанию не в UTF-8 (cmd - CP866, Visual Studio - CP1251/866), поэтому русский
// текст из тестов и утилит превращается в «╨в╨╡╤Б╤В╤Л». Вызови enableUtf8Console() в начале main().
#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int codePage);
#endif

namespace dc {

inline void enableUtf8Console() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);  // CP_UTF8
#endif
}

}  // namespace dc
