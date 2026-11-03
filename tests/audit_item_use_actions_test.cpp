#include <list>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "activity_type.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "item.h"
#include "item_factory.h"
#include "item_location.h"
#include "itype.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "map.h"
#include "map_helpers.h"
#include "map_selector.h"
#include "mapdata.h"
#include "messages.h"
#include "morale_types.h"
#include "omdata.h"
#include "overmapbuffer.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const activity_id ACT_CHOP_PLANKS( "ACT_CHOP_PLANKS" );
static const activity_id ACT_FISH( "ACT_FISH" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_audit_use_b08_rune( "audit_use_b08_rune" );
static const itype_id itype_blindfold( "blindfold" );
static const itype_id itype_blindfold_up( "blindfold_up" );
static const itype_id itype_cotton_patchwork( "cotton_patchwork" );
static const itype_id itype_fishing_rod_basic( "fishing_rod_basic" );
static const itype_id itype_log( "log" );
static const itype_id itype_paper( "paper" );
static const itype_id itype_saw( "saw" );
static const itype_id itype_splinter( "splinter" );
static const itype_id itype_tshirt( "tshirt" );

static const morale_type morale_perm_constrained( "morale_perm_constrained" );

static const oter_str_id oter_field( "field" );

static const quality_id qual_HOTPLATE( "HOTPLATE" );

static const ter_str_id ter_t_water_sh( "t_water_sh" );

static const trait_id trait_VANITY( "VANITY" );

namespace
{

// Resets the avatar, and empties a map tile, when it goes out of scope.
struct avatar_and_tile_cleanup {
    tripoint_bub_ms pos;
    ~avatar_and_tile_cleanup() {
        get_map().i_clear( pos );
        clear_avatar();
    }
};

int count_on_tile( const tripoint_bub_ms &pos, const itype_id &type )
{
    int count = 0;
    for( const item &it : get_map().i_at( pos ) ) {
        if( it.typeId() == type ) {
            count += it.count();
        }
    }
    return count;
}

} // namespace

// USE-B06: LUMBER removed the log before queuing the 20 minute plank activity, which spawns its
// output only when it finishes and cannot be resumed: a cut that was cancelled or interrupted
// lost the log.
TEST_CASE( "use-b06_cutting_planks_keeps_the_log_until_the_cut_is_done", "[audit][iuse]" )
{
    clear_avatar();
    clear_map();
    avatar &they = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = they.pos_bub();
    const avatar_and_tile_cleanup cleanup{ pos };
    item saw( itype_saw );
    here.add_item( pos, item( itype_log ) );
    REQUIRE( count_on_tile( pos, itype_log ) == 1 );

    REQUIRE( iuse::lumber( &they, &saw, pos ) == 1 );
    REQUIRE( they.has_activity( ACT_CHOP_PLANKS ) );

    SECTION( "a cancelled cut leaves the log" ) {
        they.cancel_activity();
        CHECK( count_on_tile( pos, itype_log ) == 1 );
    }

    SECTION( "a finished cut turns the log into planks" ) {
        process_activity( they );
        CHECK_FALSE( they.has_activity( ACT_CHOP_PLANKS ) );
        CHECK( count_on_tile( pos, itype_log ) == 0 );
        CHECK( count_on_tile( pos, itype_2x4 ) + count_on_tile( pos, itype_splinter ) > 0 );
    }

    SECTION( "a log taken away during the cut gives nothing" ) {
        here.i_clear( pos );
        process_activity( they );
        CHECK( count_on_tile( pos, itype_2x4 ) == 0 );
        CHECK( count_on_tile( pos, itype_splinter ) == 0 );
    }
}

// USE-B07 (data part): the HOTPLATE action heats with the item itself only if it has the
// HOTPLATE quality.  The kerosene and soda can stoves had the action without the quality, so
// they asked for a fire or another hotplate instead.
// The code part (iuse::hotplate returned 1, so activation charged the heater on top of the
// heat activity) needs the item selection menu, and is checked by reading.
TEST_CASE( "use-b07_hotplate_action_items_have_the_hotplate_quality", "[audit][iuse]" )
{
    int checked = 0;
    for( const itype *type : item_controller->all() ) {
        if( type->get_use( "HOTPLATE" ) == nullptr ) {
            continue;
        }
        CAPTURE( type->get_id().str() );
        CHECK( item( type ).has_quality( qual_HOTPLATE ) );
        ++checked;
    }
    CHECK( checked > 0 );
}

// USE-B08: the effect_on_conditions use action always gave its EOCs a character-relative
// location for the item, so an item used where it lies (ALLOWS_REMOTE_USE) was removed or
// changed through the wrong location.
TEST_CASE( "use-b08_eoc_use_action_finds_the_item_where_it_is", "[audit][iuse_actor]" )
{
    clear_avatar();
    clear_map();
    avatar &they = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = they.pos_bub() + tripoint_rel_ms::east;
    const avatar_and_tile_cleanup cleanup{ pos };

    item &rune = here.add_item( pos, item( itype_audit_use_b08_rune ) );
    const use_function *use = rune.type->get_use( "effect_on_conditions" );
    REQUIRE( use != nullptr );
    const effect_on_conditions_actor *actor =
        dynamic_cast<const effect_on_conditions_actor *>( use->get_actor_ptr() );
    REQUIRE( actor != nullptr );

    // Its EOC removes the item it was used from.
    actor->use( &they, rune, &here, pos );
    CHECK( count_on_tile( pos, itype_audit_use_b08_rune ) == 0 );
}

// USE-B09: every in-place transformation passes dont_take_off, which also skipped the worn
// armor bookkeeping, so morale kept counting the old item's coverage.  A vain character who
// raised a blindfold kept the "constrained" penalty for the eyes until save and load.
TEST_CASE( "use-b09_transforming_worn_armor_updates_its_coverage_for_morale",
           "[audit][iuse_actor]" )
{
    clear_avatar();
    avatar &they = get_avatar();
    const avatar_and_tile_cleanup cleanup{ they.pos_bub() };
    they.set_mutation( trait_VANITY );
    REQUIRE( they.has_trait( trait_VANITY ) );
    REQUIRE( they.has_morale( morale_perm_constrained ) == 0 );

    std::optional<std::list<item>::iterator> worn = they.wear_item( item( itype_blindfold ), false );
    REQUIRE( worn.has_value() );
    item &blindfold = **worn;
    // covered eyes constrain a vain character
    REQUIRE( they.has_morale( morale_perm_constrained ) < 0 );

    const itype *lowered = blindfold.type;
    REQUIRE( lowered->invoke( &they, blindfold, they.pos_bub() ).has_value() );
    REQUIRE( blindfold.typeId() == itype_blindfold_up );
    REQUIRE( they.is_worn( blindfold ) );
    // raised, it covers the head only
    CHECK( they.has_morale( morale_perm_constrained ) == 0 );

    REQUIRE( they.takeoff( item_location( they, &blindfold ) ) );
    CHECK( they.has_morale( morale_perm_constrained ) == 0 );
}

namespace
{

// Puts a tile's terrain and its overmap tile's terrain back when it goes out of scope.
struct terrain_restore {
    tripoint_bub_ms pos;
    ter_id ter;
    tripoint_abs_omt omt;
    oter_id oter;
    ~terrain_restore() {
        get_map().ter_set( pos, ter );
        overmap_buffer.ter_set( omt, oter );
    }
};

} // namespace

// USE-B10: good_fishing_spot() rejected still water with no fish nearby only inside a river
// overmap tile, where water has a current, so the check almost never fired: a pond, swamp or
// cave pool with no fish was always a good spot.
TEST_CASE( "use-b10_still_water_with_no_fish_is_a_poor_fishing_spot", "[audit][iuse]" )
{
    clear_avatar();
    clear_map();
    avatar &they = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms water = they.pos_bub() + tripoint_rel_ms::east;
    const avatar_and_tile_cleanup cleanup{ water };

    // An overmap tile that is no river, lake or ocean
    const tripoint_abs_omt omt = project_to<coords::omt>( here.get_abs( water ) );
    const terrain_restore restore{ water, here.ter( water ), omt, overmap_buffer.ter( omt ) };
    overmap_buffer.ter_set( omt, oter_field.id() );
    here.ter_set( water, ter_t_water_sh );
    REQUIRE( here.has_flag( ter_furn_flag::TFLAG_FISHABLE, water ) );
    REQUIRE_FALSE( here.has_flag( ter_furn_flag::TFLAG_CURRENT, water ) );

    item rod( itype_fishing_rod_basic );
    Messages::clear_messages();
    const std::optional<int> used = iuse::fishing_rod( &they, &rod, they.pos_bub() );
    const bool fishing = they.has_activity( ACT_FISH );
    they.cancel_activity();

    CHECK_FALSE( used.has_value() );
    CHECK_FALSE( fishing );
    bool doubted = false;
    for( const std::pair<std::string, std::string> &msg :
         Messages::recent_messages( Messages::size() ) ) {
        doubted |= msg.second.find( "You doubt you will have much luck" ) != std::string::npos;
    }
    CHECK( doubted );
    Messages::clear_messages();
}

// USE-B11: salvage rounded each remembered basic component down on its own.  A crafted item
// keeps one component item per unit, so any loss of efficiency (a Fabrication or dexterity roll,
// damage) turned every one into 0: cutting up a crafted t-shirt gave nothing.  A component
// counted by charges gave at most one, whatever its charges.
TEST_CASE( "use-b11_salvage_rounds_once_per_material", "[audit][iuse_actor][salvage]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms pos( 60, 60, 0 );
    const avatar_and_tile_cleanup cleanup{ pos };

    item shirt( itype_tshirt );
    REQUIRE( shirt.is_salvageable() );
    REQUIRE( itype_cotton_patchwork->is_basic_component() );
    REQUIRE( itype_paper->is_basic_component() );
    for( int i = 0; i < 24; ++i ) {
        item patch( itype_cotton_patchwork );
        shirt.components.add( patch );
    }
    item paper( itype_paper );
    REQUIRE( paper.count_by_charges() );
    paper.charges = 10;
    shirt.components.add( paper );

    item &placed = here.add_item( pos, shirt );
    const item_location cut( map_cursor( pos ), &placed );

    SECTION( "with no losses everything comes back" ) {
        std::map<itype_id, int> got = salvage_actor::salvage_results( cut, 1.0 );
        CHECK( got[itype_cotton_patchwork] == 24 );
        CHECK( got[itype_paper] == 10 );
    }

    SECTION( "a loss of efficiency costs a share, not everything" ) {
        // the Fabrication roll's 0.9: 21.6 patches and 9 sheets of paper
        std::map<itype_id, int> got = salvage_actor::salvage_results( cut, 0.9 );
        CHECK( got[itype_cotton_patchwork] == 21 );
        CHECK( got[itype_paper] == 9 );
    }
}
