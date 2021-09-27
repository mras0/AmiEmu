#include <iostream>
#include <string>
#include <cassert>
#include <fstream>
#include "asm.h"
#include "ioutil.h"

enum class output_format {
    bin,
    header,
};

void usage(const std::string& err = "")
{
    if (!err.empty())
        std::cerr << "Error: " << err << "\n\n";
    std::cerr << "Usage: m68kasm [-ofmt bin/header] input [-o output] [-org address]\n";
    exit(1);
}

const char* default_ext(output_format fmt)
{
    switch (fmt) {
    case output_format::bin: return "bin";
    case output_format::header: return "h";
    }
    assert(false);
    return "bin";
}

int main(int argc, char* argv[])
{
    output_format fmt = output_format::bin;
    const char* input_file = nullptr;
    std::string output_file;
    uint32_t org = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-ofmt")) {
            ++i;
            if (i == argc)
                usage("Argument missing to -ofmt");
            if (!strcmp(argv[i], "bin")) {
                fmt = output_format::bin;
                continue;
            } else if (!strcmp(argv[i], "header")) {
                fmt = output_format::header;
                continue;
            }
            usage("Invalid output format");
        } else if (!strcmp(argv[i], "-o")) {
            ++i;
            if (i == argc)
                usage("Argument missing to -o");
            if (!output_file.empty())
                usage("Multiple output files specified");
            output_file = argv[i];
            continue;
        } else if (!strcmp(argv[i], "-org")) {
            ++i;
            if (i == argc)
                usage("Argument missing to -org");
            auto res = from_hex(argv[i]);
            if (!res.first)
                usage("Invalid argument to -org");
            org = res.second;
            continue;
        }
        if (!input_file)
            input_file = argv[i];
        else
            usage("Invalid argument: " + std::string{argv[i]});
    }

    if (!input_file)
        usage("Input missing");

    const char* input_file_end = input_file + strlen(input_file);
    const char* input_fname = input_file_end;
    while (input_fname > input_file && input_fname[-1] != '\\' && input_fname[-1] != '/')
        --input_fname;

    const char* basename = strchr(input_fname, '.');
    if (!basename)
        basename = input_file + strlen(input_file);
    if (output_file.empty()) {
        output_file = std::string(input_file, basename - input_file) + "." + default_ext(fmt);
    }

    try {
        auto input = read_file(input_file);
        input.push_back(0);
        auto code = assemble(org, reinterpret_cast<const char*>(&input[0]), {});
        std::ofstream out { output_file, std::ofstream::binary };
        if (!out || !out.is_open())
            throw std::runtime_error { "Could not create " + output_file };

        if (fmt == output_format::bin) {
            out.write(reinterpret_cast<const char*>(code.data()), code.size());
        } else {
            assert(fmt == output_format::header);

            out << "const unsigned char " << std::string(input_fname, basename - input_fname) << "[" << code.size() << "] = {\n";
            for (size_t i = 0; i < code.size(); ++i) {
                out << "0x" << hexfmt(code[i]) << ",";
                if (i % 16 == 15 || i == code.size() - 1)
                    out << '\n';
                else
                    out << ' ';
            }
            out << "};\n";
        }

    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
