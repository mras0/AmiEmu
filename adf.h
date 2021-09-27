#ifndef ADF_H_INCLUDED
#define ADF_H_INCLUDED

#include <memory>
#include <vector>
#include <string>
#include <stdint.h>

class adf {
public:
    explicit adf(const std::vector<uint8_t>& data);
    ~adf();

    const std::vector<uint8_t>& get() const;

    std::string volume_label() const;
    std::vector<std::string> filelist() const;
    std::vector<uint8_t> read_file(const std::string& path) const;
    void make_dir(const std::string& path);
    void write_file(const std::string& path, const std::vector<uint8_t>& data);

    static adf new_disk(const std::string& label);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif