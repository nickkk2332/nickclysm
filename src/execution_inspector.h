#pragma once
#ifndef CATA_SRC_EXECUTION_INSPECTOR_H
#define CATA_SRC_EXECUTION_INSPECTOR_H

#include <string>

struct execution_inspection;
struct execution_trigger_report;
struct ability_view;
struct damage_transaction_view;
struct power_application_view;
class power_manager;
struct power_projection_view;
struct power_grant_view;

namespace execution_inspector
{
std::string format_scene( const execution_inspection &scene );
std::string format_trigger( const execution_trigger_report &report );
std::string format_ability( const ability_view &ability );
std::string format_damage( const damage_transaction_view &damage );
std::string format_application( const power_application_view &application );
std::string format_power_accounts( const power_manager &powers );
std::string format_projection( const power_projection_view &projection );
std::string format_grant( const power_grant_view &grant );
void show();
} // namespace execution_inspector

#endif // CATA_SRC_EXECUTION_INSPECTOR_H
