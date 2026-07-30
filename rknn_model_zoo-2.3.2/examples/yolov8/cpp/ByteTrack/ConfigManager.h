#pragma once

#include <json/json.h>
#include <string>


class ConfigManager
{

public:

    bool load(const std::string& file);


    Json::Value get();


private:

    Json::Value root;

};