#pragma once
#include <string>
#include <vector>

struct ChecklistItem {
    int id = -1;
    int note_id = -1;
    std::string item_text;
    bool is_checked = false;
    int item_order = 0;
};

struct Note {
    int id = -1;
    std::string title;
    std::string content;
    int color_id = 0;
    bool is_archived = false;
    bool is_pinned = false;
    bool is_checklist = false;
    std::string created_at;
    std::string modified_at;
    std::vector<ChecklistItem> checklist_items;
};
