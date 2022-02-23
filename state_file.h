#ifndef STATE_FILE_H_INCLUDED
#define STATE_FILE_H_INCLUDED

#include <memory>
#include <vector>
#include <string>

class state_file {
public:
    enum class dir { load, save };
    explicit state_file(dir d, const std::string& filename);
    ~state_file();

    bool loading() const;

    class scope {
    public:
        explicit scope(state_file& sf, const char* id, uint32_t version)
            : sf_ { sf }
            , pos_ { sf.open_scope(id, version) }
        {
        }

        ~scope()
        {
            sf_.close_scope(pos_);
        }

        scope(const scope&) = delete;
        scope& operator=(const scope&) = delete;
    private:
        state_file& sf_;
        const uint32_t pos_;
    };

    void handle(uint8_t& num);
    void handle(uint16_t& num);
    void handle(uint32_t& num);
    void handle(std::string& s);
    void handle(std::vector<std::string>& vec);
    void handle(std::vector<uint8_t>& vec);
    void handle_blob(void* blob, uint32_t size);

private:
    class impl;
    std::unique_ptr<impl> impl_;

    uint32_t open_scope(const char* id, uint32_t version);
    void close_scope(uint32_t pos);
};

#endif
