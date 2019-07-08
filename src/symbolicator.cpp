//
// Created by lexxish on 7/8/2019.
//

#ifndef DRACONITY_SYMBOLICATOR_H
#define DRACONITY_SYMBOLICATOR_H
#include "symbolicator.h"

class Symbolicator {
public:
    void loadSymbols(std::string libraryName, std::list<SymbolLoad> symbols) {
        HINSTANCE hDLL = LoadLibrary(libraryName);
        if (hDLL == nullptr) {
            throw new std::runtime_error("Could not load DLL " + libraryName)
        }
        for (auto sym : symbols) {
            sym->setAddr(GetProcAddress(hDLL, sym->name))
        }
    }
};

#endif //DRACONITY_SYMBOLICATOR_H
