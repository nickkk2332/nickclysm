#include <map>
#include <set>
#include <utility>

#include "cata_catch.h"
#include "global_vars.h"
#include "mapdata.h"
#include "mapgen_functions.h"
#include "mapgendata.h"
#include "type_id.h"

static const ter_str_id ter_t_floor( "t_floor" );
static const ter_str_id ter_t_linoleum_gray( "t_linoleum_gray" );
static const ter_str_id ter_t_linoleum_white( "t_linoleum_white" );

static const update_mapgen_id
update_mapgen_test_audit_place_variable_weight_nest( "test_audit_place_variable_weight_nest" );
static const update_mapgen_id update_mapgen_test_audit_update_variants(
    "test_audit_update_variants" );

// Terrains an update mapgen changes, over several runs
static std::set<ter_id> terrains_changed_by( const update_mapgen_id &id, int runs )
{
    std::set<ter_id> seen;
    for( int i = 0; i < runs; ++i ) {
        for( const std::pair<const ter_id, int> &changed :
             get_changed_ids_from_update( id, {} ).first ) {
            seen.insert( changed.first );
        }
    }
    return seen;
}

// Every definition of an update_mapgen_id was added to a list, but only the first one ever ran,
// so the other prison bus and portal dungeon layouts could never appear. One definition is now
// picked per run, by weight.
TEST_CASE( "mgen-b02_update_mapgen_picks_one_of_its_definitions", "[audit][mapgen]" )
{
    // Two definitions of weight 1000: both show up in 40 runs unless 1 in 2^39 bad luck
    const std::set<ter_id> seen = terrains_changed_by( update_mapgen_test_audit_update_variants,
                                  40 );
    CHECK( seen.count( ter_t_floor.id() ) == 1 );
    CHECK( seen.count( ter_t_linoleum_white.id() ) == 1 );
    // The weight 0 definition never runs
    CHECK( seen.count( ter_t_linoleum_gray.id() ) == 0 );
}

// A nested chunk's own "weight" was read with get_int, so a variable or math weight (accepted
// everywhere else mapgen takes a weight) failed to load.
TEST_CASE( "mgen-b02_nested_mapgen_weight_can_be_math", "[audit][mapgen]" )
{
    global_variables &globals = get_globals();
    const update_mapgen_id &place_nest = update_mapgen_test_audit_place_variable_weight_nest;
    // The math weighted chunk places white linoleum, the weight 1000 one gray linoleum
    globals.set_global_value( "audit_nest_weight", 0 );
    const std::set<ter_id> disabled = terrains_changed_by( place_nest, 20 );
    globals.set_global_value( "audit_nest_weight", 1000000 );
    const std::set<ter_id> favoured = terrains_changed_by( place_nest, 20 );
    globals.remove_global_value( "audit_nest_weight" );

    CHECK( disabled.count( ter_t_linoleum_white.id() ) == 0 );
    CHECK( disabled.count( ter_t_linoleum_gray.id() ) == 1 );
    CHECK( favoured.count( ter_t_linoleum_white.id() ) == 1 );
}
