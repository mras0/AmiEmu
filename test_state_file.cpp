#include <iostream>
#include "state_file.h"
#include "ioutil.h"

struct test_state1 {
    std::string str;
    std::vector<uint8_t> data;
    struct {
        uint32_t x;
        uint32_t y;
    } blob;
    uint32_t num;


    void handle_state(state_file& sf)
    {
        const state_file::scope s { sf, "My scope", 1 };
        sf.handle(str);
        sf.handle(data);
        sf.handle_blob(&blob, sizeof(blob));
        sf.handle(num);
    }
};

bool test_state_file()
{
    test_state1 src_state1 { "test string", { 1, 2, 4, 5, 6, 7 }, { 0x12345678, 0x9abcdef }, 0x42424141 };
    {
        state_file sf { state_file::dir::save, "test.state" };
        src_state1.handle_state(sf);
    }
    {
        state_file sf { state_file::dir::load, "test.state" };
        test_state1 dst;
        dst.handle_state(sf);

        if (dst.str != src_state1.str || dst.data != src_state1.data || memcmp(&src_state1.blob, &dst.blob, sizeof(dst.blob)) || dst.num != src_state1.num)
            throw std::runtime_error { "Test state 1 failed" };
    }

    auto v = read_file("test.state");
    hexdump(std::cout, v.data(), v.size());


    return true;
}
