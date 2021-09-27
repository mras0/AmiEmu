#ifndef ASM_H
#define ASM_H

#include <stdint.h>
#include <vector>

std::vector<uint16_t> assemble(uint32_t start_pc, const char* code);

#endif
