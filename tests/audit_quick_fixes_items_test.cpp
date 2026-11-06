#include <map>
#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "global_vars.h"
#include "item.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "math_parser_diag_value.h"
#include "monster.h"
#include "mtype.h"
#include "type_id.h"

static const clothing_mod_id clothing_mod_kevlar_padded( "kevlar_padded" );
static const clothing_mod_id clothing_mod_leather_padded( "leather_padded" );

static const itype_id itype_milk( "milk" );
static const itype_id itype_milk_raw( "milk_raw" );
static const itype_id itype_test_qf_npc_summoner( "test_qf_npc_summoner" );
static const itype_id itype_test_qf_tailors_kit( "test_qf_tailors_kit" );
static const itype_id itype_water_clean( "water_clean" );

static const material_id material_cotton( "cotton" );
static const material_id material_leather( "leather" );

static const mtype_id mon_cow( "mon_cow" );
static const mtype_id mon_test_qf_honeydew_aphid( "mon_test_qf_honeydew_aphid" );

// USE-J03: sew_advanced read its "clothing_mods" array into its materials, so no garment
// matched the material filter and the menu of clothing mods was always empty.
TEST_CASE( "use-j03_sew_advanced_loads_its_materials_and_clothing_mods", "[audit][item]" )
{
    const item kit( itype_test_qf_tailors_kit );
    const use_function *use = kit.type->get_use( "sew_advanced" );
    REQUIRE( use != nullptr );
    const sew_advanced_actor *actor = dynamic_cast<const sew_advanced_actor *>
                                      ( use->get_actor_ptr() );
    REQUIRE( actor != nullptr );

    CHECK( actor->materials == std::set<material_id> { material_cotton, material_leather } );
    CHECK( actor->clothing_mods == std::vector<clothing_mod_id> {
        clothing_mod_leather_padded, clothing_mod_kevlar_padded
    } );
}

// USE-J04: place_npc never read the "radius" ITEM.md documents for random placement, so the
// field failed to load and the radius was always 1.
TEST_CASE( "use-j04_place_npc_reads_its_radius", "[audit][item]" )
{
    const item summoner( itype_test_qf_npc_summoner );
    const use_function *use = summoner.type->get_use( "place_npc" );
    REQUIRE( use != nullptr );
    const place_npc_iuse *actor = dynamic_cast<const place_npc_iuse *>( use->get_actor_ptr() );
    REQUIRE( actor != nullptr );

    CHECK( actor->place_randomly );
    CHECK( actor->radius == 3 );
}

// MONDT-J03: refilling udders only looked for milk_raw and milk.  A monster milked for any
// other liquid (innawood's honeydew aphids) read through ammo.end(), and the legacy milk
// migration read the entry it had just erased.
TEST_CASE( "mondt-j03_udders_of_a_non_milk_liquid_refill", "[audit][monster]" )
{
    restore_on_out_of_scope restore_turn( calendar::turn );

    SECTION( "a monster milked for clean water refills once a day" ) {
        monster aphid( mon_test_qf_honeydew_aphid );
        REQUIRE( aphid.type->starting_ammo == std::map<itype_id, int> { { itype_water_clean, 3 } } );
        aphid.ammo = aphid.type->starting_ammo;
        // Milked dry
        aphid.ammo[itype_water_clean] = 0;

        aphid.refill_udders();
        CHECK( aphid.ammo == std::map<itype_id, int> { { itype_water_clean, 0 } } );

        calendar::turn += 2_days;
        aphid.refill_udders();
        CHECK( aphid.ammo == std::map<itype_id, int> { { itype_water_clean, 3 } } );
    }

    SECTION( "a monster saved without any ammo starts full" ) {
        monster aphid( mon_test_qf_honeydew_aphid );
        aphid.ammo.clear();
        aphid.refill_udders();
        CHECK( aphid.ammo == std::map<itype_id, int> { { itype_water_clean, 3 } } );
    }

    SECTION( "raw milk saved under the old milk id moves to milk_raw" ) {
        monster cow( mon_cow );
        REQUIRE( cow.type->starting_ammo.begin()->first == itype_milk_raw );
        cow.ammo = { { itype_milk, 7 } };
        cow.refill_udders();
        CHECK( cow.ammo == std::map<itype_id, int> { { itype_milk_raw, 7 } } );
    }
}

// MONDT-J05: a monster's death EOC got no context, so it couldn't tell where the monster
// died or how hard it was hit; it now gets "loc" and "overkill".
TEST_CASE( "mondt-j05_death_eoc_gets_its_location_and_overkill", "[audit][monster][eoc]" )
{
    clear_map_without_vision();
    clear_creatures();
    global_variables &globals = get_globals();
    on_out_of_scope cleanup( [&globals]() {
        globals.remove_global_value( "test_qf_death_loc" );
        globals.remove_global_value( "test_qf_death_overkill" );
        clear_creatures();
    } );
    globals.remove_global_value( "test_qf_death_loc" );
    globals.remove_global_value( "test_qf_death_overkill" );

    map &here = get_map();
    monster &mon = spawn_test_monster( "mon_test_qf_death_context",
                                       get_avatar().pos_bub() + tripoint_rel_ms( 3, 0, 0 ) );
    const tripoint_abs_ms death_loc = mon.pos_abs();
    // 7 more damage than it has HP, from nothing (no killer, so no alpha talker)
    mon.apply_damage( nullptr, bodypart_id( "torso" ), mon.get_hp() + 7 );
    mon.die( &here, nullptr );

    const diag_value *loc = globals.maybe_get_global_value( "test_qf_death_loc" );
    REQUIRE( loc != nullptr );
    CHECK( loc->tripoint() == death_loc );
    const diag_value *overkill = globals.maybe_get_global_value( "test_qf_death_overkill" );
    REQUIRE( overkill != nullptr );
    CHECK( overkill->dbl() == 7 );
}
