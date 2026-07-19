#pragma once

#include <string>
#include <vector>

struct Detection {
    std::string class_name;

    float confidence;

    int x;
    int y;
    int width;
    int height;
};