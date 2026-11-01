#include <string>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "coordinates.h"
#include "item.h"
#include "iuse.h"
#include "iuse_actor.h"
#include "map_helpers.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"

static const efftype_id effect_bleed( "bleed" );

static const itype_id itype_bandages( "bandages" );
static const itype_id itype_basic_papr_belt_off( "basic_papr_belt_off" );
static const itype_id itype_basic_papr_belt_on( "basic_papr_belt_on" );
static const itype_id itype_gasfilter_med( "gasfilter_med" );

static const proficiency_id proficiency_prof_wound_care( "prof_wound_care" );
static const proficiency_id proficiency_prof_wound_care_expert( "prof_wound_care_expert" );

static const skill_id skill_firstaid( "firstaid" );

// USE-B02: a PAPR blower ticked on the map has no carrier (p == nullptr).  The tick
// function used to dereference it unconditionally once the filter ran low or empty.
TEST_CASE( "use-b02_papr_blower_tick_without_carrier", "[audit]" )
{
    clear_map();
    const tripoint_bub_ms pos( 60, 60, 0 );

    SECTION( "empty filter shuts the blower off" ) {
        item blower( itype_basic_papr_belt_on );
        REQUIRE( blower.active );
        REQUIRE( blower.ammo_remaining_in_pocket( "papr_blower_filter" ) == 0 );

        // Same call map item processing makes for a blower lying on the ground.
        iuse::papr_blower( nullptr, &blower, pos );

        CHECK_FALSE( blower.active );
        CHECK( blower.typeId() == itype_basic_papr_belt_off );
    }

    SECTION( "low filter keeps running without a carrier to warn" ) {
        item blower( itype_basic_papr_belt_on );
        item filter( itype_gasfilter_med );
        filter.ammo_set( filter.ammo_current(), 5 );
        REQUIRE( blower.put_in( filter, pocket_type::MAGAZINE_WELL ).success() );
        REQUIRE( blower.ammo_remaining_in_pocket( "papr_blower_filter" ) == 5 );
        // Enough absorbed gas to consume one filter charge this tick.
        blower.set_var( "gas_absorbed", 60 );

        iuse::papr_blower( nullptr, &blower, pos );

        CHECK( blower.ammo_remaining_in_pocket( "papr_blower_filter" ) == 4 );
        CHECK( blower.active );
        CHECK( blower.typeId() == itype_basic_papr_belt_on );
    }
}

// USE-B04: stop-bleed power was bleed * skill bonus, i.e. 0 at First Aid 0, so an
// untrained character could never stop any bleeding.
TEST_CASE( "use-b04_stop_bleed_works_at_first_aid_0", "[audit]" )
{
    clear_avatar();
    clear_map();
    avatar &dummy = get_avatar();
    REQUIRE( dummy.get_skill_level( skill_firstaid ) == 0 );
    REQUIRE_FALSE( dummy.has_proficiency( proficiency_prof_wound_care ) );
    REQUIRE_FALSE( dummy.has_proficiency( proficiency_prof_wound_care_expert ) );

    item bandage( itype_bandages );
    const use_function *use = bandage.get_use( "heal" );
    REQUIRE( use != nullptr );
    const heal_actor *actor = dynamic_cast<const heal_actor *>( use->get_actor_ptr() );
    REQUIRE( actor != nullptr );
    REQUIRE( actor->bleed > 0 );

    // "bleed" is the base value at First Aid 0 (doc/JSON/ITEM.md).
    CHECK( actor->get_stopbleed_level( dummy ) == actor->bleed );

    // A light bleed is stopped by an untrained character.
    const bodypart_id arm( "arm_r" );
    dummy.add_effect( effect_bleed, 2_minutes, arm );
    REQUIRE( dummy.has_effect( effect_bleed, arm ) );
    REQUIRE( dummy.get_effect_int( effect_bleed, arm ) < 3 * actor->bleed );
    actor->finish_using( dummy, dummy, bandage, arm );
    CHECK_FALSE( dummy.has_effect( effect_bleed, arm ) );

    // Skill still improves the result.
    dummy.set_skill_level( skill_firstaid, 4 );
    CHECK( actor->get_stopbleed_level( dummy ) > actor->bleed );
}
