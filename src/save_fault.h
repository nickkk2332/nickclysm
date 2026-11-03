#pragma once
#ifndef CATA_SRC_SAVE_FAULT_H
#define CATA_SRC_SAVE_FAULT_H

#include <set>
#include <string>
#include <vector>

// Lets tests make one step of game::save() or of dimension travel fail, so the failure paths can
// be checked without breaking the disk. The game itself never sets any of this.
namespace save_fault
{
// The steps that fail on purpose: "master", "dimension", "maps", "player", "achievements" and
// "options_record"
std::set<std::string> &failing_steps();
// The steps started since the test last cleared this, in order
std::vector<std::string> &started_steps();
// Called at the start of each step: records the step and says whether it must fail
bool step_fails( const std::string &step );
} // namespace save_fault

#endif // CATA_SRC_SAVE_FAULT_H
