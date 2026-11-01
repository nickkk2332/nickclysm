#include <utility>

#include "cata_catch.h"
#include "event.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "magic.h"
#include "player_helpers.h"
#include "type_id.h"

static const spell_id spell_test_spell_random_damage( "test_spell_random_damage" );

namespace
{

struct spell_level_up_counter : public event_subscriber {
    using event_subscriber::notify;
    void notify( const cata::event &e ) override {
        if( e.type() == event_type::player_levels_spell ) {
            ++count;
        }
    }
    int count = 0;
};

} // namespace

// The "Study a spell" menu calls these every redraw for RANDOM_DAMAGE spells. They used to level
// temporary avatars through gain_exp(), which sent player_levels_spell for every level, and that
// event's learn_spells handler taught the real player follow-up spells.
TEST_CASE( "magic-p01_spellbook_damage_preview_sends_no_level_up_events", "[audit][magic]" )
{
    clear_avatar();
    const spell_type &sp = spell_test_spell_random_damage.obj();

    spell_level_up_counter counter;
    get_event_bus().subscribe( &counter );
    // min_damage 1, damage_increment 1, max_damage 5, max_level 10
    const std::pair<int, int> at_max = sp.damage_at_max_level();
    const std::pair<float, float> increment = sp.calculate_damage_increment();
    get_event_bus().unsubscribe( &counter );

    CHECK( counter.count == 0 );
    // level 10: 1 + 10 * 1 = 11, against max_damage 5
    CHECK( at_max.first == 5 );
    CHECK( at_max.second == 11 );
    // level 0 -> 1: the lower bound grows by 1, the upper bound stays at max_damage
    CHECK( increment.first == Approx( 1.0f ) );
    CHECK( increment.second == Approx( 0.0f ) );
}
