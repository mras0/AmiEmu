#ifndef ASM_H
#define ASM_H

#include <stdint.h>
#include <vector>
#include <utility>

std::vector<uint8_t> assemble(uint32_t start_pc, const char* code, const std::vector<std::pair<const char*, uint32_t>>& predefines = {});

#endif
