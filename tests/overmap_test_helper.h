#pragma once
#ifndef CATA_TESTS_OVERMAP_TEST_HELPER_H
#define CATA_TESTS_OVERMAP_TEST_HELPER_H

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "coordinates.h"
#include "mapgen_post_process.h"
#include "mapgendata.h"
#include "mongroup.h"
#include "overmap.h"

// Reaches into overmap for tests; overmap names it as a friend.
class overmap_test_helper
{
    public:
        static void set_highway_connections(
            overmap &om,
            const std::array<tripoint_om_omt, 4> &conns ) {
            om.highway_connections = conns;
        }
        static const std::array<tripoint_om_omt, 4> &get_highway_connections(
            const overmap &om ) {
            return om.highway_connections;
        }
        // the overmap's monster groups, by where they are
        static std::multimap<tripoint_om_sm, mongroup> &zone_groups( overmap &om ) {
            return om.zg;
        }
        static void add_mon_group( overmap &om, const mongroup &group ) {
            om.add_mon_group( group );
        }
        // how many zone groups the signal looked at
        static int signal_hordes( overmap &om, const tripoint_abs_ms &p, int sig_power ) {
            return om.signal_hordes( p, sig_power );
        }
        // Forgets that a special was placed at @p p: its placement, its mapgen arguments and its
        // post-process decisions, with their storage once no other tile uses it.
        static void forget_special_at( overmap &om, const tripoint_om_omt &p ) {
            om.overmap_special_placements.erase( p );
            forget_shared( om.mapgen_args_index, om.mapgen_arg_storage, p );
            forget_shared( om.pp_decisions_index, om.pp_decision_storage, p );
        }
    private:
        template<typename Index, typename Storage>
        static void forget_shared( Index &index, Storage &storage, const tripoint_om_omt &p ) {
            const auto found = index.find( p );
            if( found == index.end() ) {
                return;
            }
            const auto shared = found->second;
            index.erase( found );
            if( std::none_of( index.begin(), index.end(), [shared]( const auto & entry ) {
            return entry.second == shared;
        } ) ) {
                storage.erase( storage.get_iterator( shared ) );
            }
        }
};

#endif // CATA_TESTS_OVERMAP_TEST_HELPER_H
