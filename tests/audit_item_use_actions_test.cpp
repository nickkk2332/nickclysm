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

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_audit_use_b08_rune( "audit_use_b08_rune" );
static const itype_id itype_log( "log" );
static const itype_id itype_saw( "saw" );
static const itype_id itype_splinter( "splinter" );

static const quality_id qual_HOTPLATE( "HOTPLATE" );

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
