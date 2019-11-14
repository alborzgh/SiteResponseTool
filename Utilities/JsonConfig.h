#include <map>
#include <string>
#include <fstream>
#include "ConfigIO.h"
#include "json.hpp"

#ifndef JSONCONFIG_H
#define JSONCONFIG_H

class JSONConfig: public ConfigIO
{
public:
    JSONConfig(const char* json_file);
    ~JSONConfig();


    virtual std::string getStringProperty(std::string key);
    virtual int getIntProperty(std::string key);
    virtual double getFloatProperty(std::string key);
    virtual bool getBooleanProperty(std::string key);

private:
    nlohmann::json config_json;
    std::map <std::string, std::string> config_data;
    std::string getValueFromKey(std::string key);
};

#endif