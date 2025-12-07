#pragma once
#include <string>

struct Note {
    int id = -1;
    std::string title;
    std::string content;
    int color_id = 0;
    bool is_archived = false;
    bool is_pinned = false;
    std::string created_at;
    std::string modified_at;
};
