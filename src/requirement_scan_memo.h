#pragma once
#ifndef CATA_SRC_REQUIREMENT_SCAN_MEMO_H
#define CATA_SRC_REQUIREMENT_SCAN_MEMO_H

// While one of these is alive, the multi-activity requirement checks (activity_item_handling.h)
// remember their answers and reuse them for later work spots:
// - remove_met_requirements: what is still needed once the character's own items are counted;
// - are_requirements_nearby for loot zone spots only (jobs that fetch tools from the loot
//   zones): whether the items are to be had there. The answer differs between work spots only
//   by the welding rigs in reach of the spot, which are part of what is remembered.
// Both depend on the character's items, the zones, the items in them and the item
// reservations. So a scope must not span a change to any of those: a multi-activity scan
// over the work spots it checks and skips is the intended scope, and the scan calls forget()
// before it acts on a spot.
class scoped_requirement_scan_memo
{
    public:
        scoped_requirement_scan_memo();
        ~scoped_requirement_scan_memo();
        scoped_requirement_scan_memo( const scoped_requirement_scan_memo & ) = delete;
        scoped_requirement_scan_memo &operator=( const scoped_requirement_scan_memo & ) = delete;

        // drops everything remembered so far
        void forget();
};

#endif // CATA_SRC_REQUIREMENT_SCAN_MEMO_H
