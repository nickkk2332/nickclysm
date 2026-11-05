#pragma once
#ifndef CATA_SRC_EXECUTION_INSPECTOR_H
#define CATA_SRC_EXECUTION_INSPECTOR_H

#include <string>

struct execution_inspection;
struct execution_trigger_report;

namespace execution_inspector
{
std::string format_scene( const execution_inspection &scene );
std::string format_trigger( const execution_trigger_report &report );
void show();
} // namespace execution_inspector

#endif // CATA_SRC_EXECUTION_INSPECTOR_H
