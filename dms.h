#ifndef DMS_H_INCLUDED
#define DMS_H_INCLUDED

#include <stdint.h>
#include <vector>

bool dms_detect(const std::vector<uint8_t>& data);
std::vector<uint8_t> dms_unpack(const std::vector<uint8_t>& data);

#endif
