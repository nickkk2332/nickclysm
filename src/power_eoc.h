#pragma once
#ifndef CATA_SRC_POWER_EOC_H
#define CATA_SRC_POWER_EOC_H

#include <functional>
#include <string_view>

class JsonObject;
struct dialogue;
namespace power_eoc
{
std::function<void( dialogue & )> apply( const JsonObject &jo, std::string_view member,
        std::string_view source );
std::function<void( dialogue & )> end( const JsonObject &jo, std::string_view member,
                                       std::string_view source );
std::function<void( dialogue & )> dispel( const JsonObject &jo, std::string_view member,
        std::string_view source );
std::function<void( dialogue & )> resource( const JsonObject &jo, std::string_view member,
        std::string_view source );
std::function<void( dialogue & )> charge( const JsonObject &jo, std::string_view member,
        std::string_view source );
std::function<void( dialogue & )> fling( const JsonObject &jo, std::string_view member,
        std::string_view source );
std::function<void( dialogue & )> summon( const JsonObject &jo, std::string_view member,
        std::string_view source );
std::function<void( dialogue & )> summon_command( const JsonObject &jo, std::string_view member,
        std::string_view source );
std::function<void( dialogue & )> summon_dismiss( const JsonObject &jo, std::string_view member,
        std::string_view source );
std::function<void( dialogue & )> choose_ability( const JsonObject &jo, std::string_view member,
        std::string_view source );
} // namespace power_eoc

#endif // CATA_SRC_POWER_EOC_H
