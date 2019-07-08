//
// Created by lexxish on 7/8/2019.
//

#ifndef DRACONITY_SYMBOLICATOR_H
#define DRACONITY_SYMBOLICATOR_H

#include "symbolicator.h"

class Symbolicator {
public:
    void loadSymbols(std::string libraryName, std::list<SymbolLoad> symbols) {
        HINSTANCE hDLL = LoadLibrary(libraryName.c_str());
        if (hDLL == nullptr) {
            throw std::runtime_error("Could not load DLL " + libraryName + ". " + getLastErrorAsString());
        }
        for (auto sym : symbols) {
            FARPROC procAddress = GetProcAddress(hDLL, sym->getName());
            if (procAddress == nullptr) {
                throw std::runtime_error(
                        "Could not find method " + sym->getName() + " in library " + libraryName + ". " +
                        getLastErrorAsString()
                );
            }
            sym->setAddr(procAddress);
        }
    }

private:
    std::string getLastErrorAsString() {
        DWORD errorMessageID = ::GetLastError();
        if (errorMessageID == 0) {
            // No error message
            return std::string();
        }

        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR) &messageBuffer, 0, NULL
        );

        std::string message(messageBuffer, size);

        LocalFree(messageBuffer);
        return message;
    }
};

#endif //DRACONITY_SYMBOLICATOR_H
