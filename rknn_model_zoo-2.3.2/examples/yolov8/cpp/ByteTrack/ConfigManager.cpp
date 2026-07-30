#include "ConfigManager.h"

#include <fstream>


bool ConfigManager::load(
        const std::string& file)
{

    std::ifstream in(file);


    if(!in.is_open())
        return false;


    Json::CharReaderBuilder builder;

    std::string errs;


    return Json::parseFromStream(
        builder,
        in,
        &root,
        &errs
    );

}



Json::Value ConfigManager::get()
{
    return root;
}