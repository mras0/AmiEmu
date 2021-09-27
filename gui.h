#ifndef GUI_H
#define GUI_H

#include <memory>

class gui {
public:
    explicit gui(unsigned width, unsigned height);
    ~gui();

    bool update();
    void update_image(const uint32_t* data);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};

#endif
