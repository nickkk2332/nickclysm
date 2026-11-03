#pragma once
#ifndef CATA_SRC_SAVE_FAULT_H
#define CATA_SRC_SAVE_FAULT_H

#include <optional>
#include <set>
#include <string>
#include <vector>

// Lets tests make a step of game::save() or of dimension travel fail, or pass without writing
// anything, so the failure paths can be checked without breaking the disk or saving the test
// world. The game itself never sets any of this.
namespace save_fault
{
// The steps: "master", "player" (the player file, written before the world), "dimension",
// "maps", "achievements", "options_record", and "travel_save" (dimension travel's save of the
// dimension it leaves, once the travellers are out of it)
// Steps that fail without writing anything
std::set<std::string> &failing_steps();
// Steps that report success without writing anything
std::set<std::string> &skipped_steps();
// The steps started since the test last cleared this, in order
std::vector<std::string> &started_steps();
// Called at the start of each step: records the step, and gives the result the step must
// return at once, or nothing when it should run
std::optional<bool> forced_result( const std::string &step );
} // namespace save_fault

#endif // CATA_SRC_SAVE_FAULT_H
