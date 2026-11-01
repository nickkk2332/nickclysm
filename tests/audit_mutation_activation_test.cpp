#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_iterator.h"
#include "mutation.h"
#include "player_helpers.h"
#include "type_id.h"

static const effect_on_condition_id effect_on_condition_EOC_AUDIT_MUT_B01_LOSE_WEB_ROPE(
    "EOC_AUDIT_MUT_B01_LOSE_WEB_ROPE" );

static const trait_id trait_WEB_ROPE( "WEB_ROPE" );

// How many of the given item the character carries or that lie on or next to their tile.
static int count_nearby( Character &you, const itype_id &what )
{
    map &here = get_map();
    int found = you.amount_of( what );
    for( const tripoint_bub_ms &p : here.points_in_radius( you.pos_bub(), 1 ) ) {
        for( const item &it : here.i_at( p ) ) {
            if( it.typeId() == what ) {
                ++found;
            }
        }
    }
    return found;
}

TEST_CASE( "mut-b01_activation_eoc_removing_its_own_trait", "[audit][mutations]" )
{
    clear_avatar();
    clear_map_without_vision();
    avatar &me = get_avatar();

    // Like Xedra Evolved's ethereal_wings / karma_arms / devil_tail / stalker_eyes: an
    // activation EOC that runs u_lose_trait on the very mutation being activated.  The EOC is
    // built here and attached to a core spawn_item mutation only for the duration of this test.
    if( !effect_on_condition_EOC_AUDIT_MUT_B01_LOSE_WEB_ROPE.is_valid() ) {
        const JsonValue jv = json_loader::from_string( R"({
            "id": "EOC_AUDIT_MUT_B01_LOSE_WEB_ROPE",
            "effect": [ { "u_lose_trait": "WEB_ROPE" } ]
        })" );
        effect_on_conditions::load_inline_eoc( jv, "test_data" );
    }
    REQUIRE( effect_on_condition_EOC_AUDIT_MUT_B01_LOSE_WEB_ROPE.is_valid() );
    const itype_id spawned = trait_WEB_ROPE->spawn_item;
    REQUIRE_FALSE( spawned.is_empty() );

    std::vector<effect_on_condition_id> &activated_eocs =
        const_cast<std::vector<effect_on_condition_id> &>( trait_WEB_ROPE->activated_eocs );
    restore_on_out_of_scope<std::vector<effect_on_condition_id>> restore_eocs( activated_eocs );
    activated_eocs.push_back( effect_on_condition_EOC_AUDIT_MUT_B01_LOSE_WEB_ROPE );

    me.set_mutation( trait_WEB_ROPE );
    REQUIRE( me.has_trait( trait_WEB_ROPE ) );
    REQUIRE( me.can_power_mutation( trait_WEB_ROPE ) );
    REQUIRE( count_nearby( me, spawned ) == 0 );

    // The EOC erases the trait's cached_mutations entry.  Before the fix the activation then
    // wrote through the dangling reference to it (heap-use-after-free, caught by ASan) and
    // carried on activating the removed trait, spawning its item.
    me.activate_mutation( trait_WEB_ROPE );

    CHECK_FALSE( me.has_trait( trait_WEB_ROPE ) );
    CHECK_FALSE( me.has_permanent_trait( trait_WEB_ROPE ) );
    CHECK_FALSE( me.has_active_mutation( trait_WEB_ROPE ) );
    // The trait is gone, so its activation must stop there: no rope from a lost trait.
    CHECK( count_nearby( me, spawned ) == 0 );
}
