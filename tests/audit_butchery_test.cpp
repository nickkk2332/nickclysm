#include <string>
#include <vector>

#include "activity_actor_definitions.h"
#include "butchery.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "flag.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_helpers.h"
#include "map_selector.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

static const itype_id itype_knife_combat( "knife_combat" );

static const mtype_id mon_chicken( "mon_chicken" );

static const skill_id skill_survival( "survival" );

static constexpr tripoint_bub_ms butcher_pos( 60, 60, 0 );
static constexpr tripoint_bub_ms corpse_a_pos( 61, 60, 0 );
static constexpr tripoint_bub_ms corpse_b_pos( 59, 60, 0 );

static Character &setup_butcher()
{
    clear_map();
    clear_avatar();
    Character &u = get_player_character();
    u.setpos( get_map(), butcher_pos );
    // High enough that the harvest roll can never be a fatal failure.
    u.set_skill_level( skill_survival, 10 );
    // CUT 2 + BUTCHER quality: enough for skinning a small corpse without a surface.
    item knife( itype_knife_combat );
    REQUIRE( u.wield( knife ) );
    return u;
}

static item &place_corpse( const tripoint_bub_ms &pos )
{
    item &corpse = get_map().add_item( pos, item::make_corpse( mon_chicken ) );
    REQUIRE( corpse.is_corpse() );
    return corpse;
}

// ACT-B01: when set_up_butchery() refused the corpse at the back of the queue, do_turn() popped
// it and then kept reading/writing the destroyed queue entry (use-after-free).  In practice that
// stamped the refused corpse with butchery progress.  The refused corpse must be left alone and
// the activity must move on to the next corpse.
TEST_CASE( "act-b01_butchery_queue_skips_refused_corpse", "[audit][butchery]" )
{
    Character &u = setup_butcher();

    // Corpse A is already skinned, so skinning it is refused.
    item &corpse_a = place_corpse( corpse_a_pos );
    corpse_a.set_flag( flag_SKINNED );
    item &corpse_b = place_corpse( corpse_b_pos );

    item_location loc_a( map_cursor( corpse_a_pos ), &corpse_a );
    item_location loc_b( map_cursor( corpse_b_pos ), &corpse_b );

    // The queue is processed from the back, so the refused corpse A is tried first.
    std::vector<butchery_data> queue;
    queue.emplace_back( loc_b, butcher_type::SKIN );
    queue.emplace_back( loc_a, butcher_type::SKIN );
    player_activity p_act{ butchery_activity_actor( queue ) };

    int turns = 0;
    while( p_act && turns < 100000 ) {
        p_act.do_turn( u );
        ++turns;
    }
    CAPTURE( turns );
    REQUIRE( !p_act );

    REQUIRE( loc_a );
    REQUIRE( loc_b );
    // Nothing may have been recorded on the refused corpse.
    CHECK( !loc_a->has_var( butcher_progress_time_var() ) );
    CHECK( !loc_a->has_var( butcher_progress_var( butcher_type::SKIN ) ) );
    // The next corpse in the queue was still processed.
    CHECK( loc_b->has_flag( flag_SKINNED ) );
}

// ACT-B01 (related): a queued corpse can disappear (e.g. revive) before its butchery starts.
// do_turn() used to initiate the butchery, dereferencing the missing corpse, before checking it.
TEST_CASE( "act-b01_butchery_of_vanished_corpse_stops", "[audit][butchery]" )
{
    Character &u = setup_butcher();

    item &corpse = place_corpse( corpse_a_pos );
    item_location loc( map_cursor( corpse_a_pos ), &corpse );
    butchery_data bd( loc, butcher_type::SKIN );
    get_map().i_clear( corpse_a_pos );
    REQUIRE( !loc );

    player_activity p_act{ butchery_activity_actor( bd ) };
    p_act.do_turn( u );
    CHECK( !p_act );
}

// ACT-B01 (related): canceled() dereferenced the corpse before checking it, and for a queue entry
// that was never initiated it saved 0/0 (NaN) over the corpse's earlier progress.
TEST_CASE( "act-b01_butchery_cancel_saves_only_started_progress", "[audit][butchery]" )
{
    Character &u = setup_butcher();

    item &corpse = place_corpse( corpse_a_pos );
    const std::string progress_var = butcher_progress_var( butcher_type::SKIN );
    corpse.set_var( progress_var, 0.25 );
    item_location loc( map_cursor( corpse_a_pos ), &corpse );
    butchery_data bd( loc, butcher_type::SKIN );
    player_activity dummy_act;

    SECTION( "entry that was never initiated keeps the earlier progress" ) {
        butchery_activity_actor actor( bd );
        actor.canceled( dummy_act, u );
        CHECK( loc->get_var( progress_var, 0.0 ) == Approx( 0.25 ) );
    }

    SECTION( "initiated entry saves its progress" ) {
        bd.time_to_butcher = 10_minutes;
        bd.progress = 5_minutes;
        butchery_activity_actor actor( bd );
        actor.canceled( dummy_act, u );
        CHECK( loc->get_var( progress_var, 0.0 ) == Approx( 0.5 ) );
    }

    SECTION( "vanished corpse is left alone" ) {
        bd.time_to_butcher = 10_minutes;
        bd.progress = 5_minutes;
        butchery_activity_actor actor( bd );
        get_map().i_clear( corpse_a_pos );
        REQUIRE( !loc );
        actor.canceled( dummy_act, u );
        CHECK( !loc );
    }
}
