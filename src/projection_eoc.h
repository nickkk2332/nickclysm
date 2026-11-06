#pragma once
#ifndef CATA_SRC_PROJECTION_EOC_H
#define CATA_SRC_PROJECTION_EOC_H
#include <functional>
#include <string_view>
class JsonObject;
struct dialogue;
namespace projection_eoc
{
std::function<void( dialogue & )> create( const JsonObject &, std::string_view, std::string_view );
std::function<void( dialogue & )> end( const JsonObject &, std::string_view, std::string_view );
std::function<void( dialogue & )> dispel( const JsonObject &, std::string_view, std::string_view );
}
#endif // CATA_SRC_PROJECTION_EOC_H
