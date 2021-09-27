#ifndef DISASM_H
#define DISASM_H

#include <iosfwd>
#include <string>
#include <stdint.h>

std::string ea_string(uint8_t ea);

void disasm(std::ostream& os, uint32_t pc, const uint16_t* iwords, size_t num_iwords);

#endif
