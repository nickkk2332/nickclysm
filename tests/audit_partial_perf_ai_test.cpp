#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "avatar.h"
#include "behavior.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "character.h"
#include "character_oracle.h"
#include "coordinates.h"
#include "creature.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "horde_entity.h"
#include "horde_map.h"
#include "item.h"
#include "item_location.h"
#include "json_loader.h"
#include "line.h"
#include "magic.h"
#include "map_helpers.h"
#include "map_scale_constants.h"
#include "mongroup.h"
#include "npc.h"
#include "npc_ai_hooks_helpers.h"
#include "npc_ai_tuning.h"
#include "npc_class.h"
#include "npc_goal_state.h"
#include "npc_json_goal.h"
#include "npc_sleep.h"
#include "npc_turn_helpers.h"
#include "options_helpers.h"
#include "overmap.h"
#include "overmap_test_helper.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "rng.h"
#include "talker.h"
#include "type_id.h"
#include "units.h"
#include "visitable.h"
#include "weather.h"

// The rest of four audit items: MONDT-B04 (a signal looks only at the hordes in its reach),
// MAGIC-P01 (casterless casts share one fake caster), INV-P02 (the header-level visit steps) and
// NPC-J10 (more tuning, and needs content adds).  Fixtures: data/mods/TEST_DATA/
// audit_partial_perf_ai.json.

static const bionic_id bio_fuel_wood( "bio_fuel_wood" );
static const bionic_id bio_razors( "bio_razors" );

static const itype_id itype_9mm( "9mm" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );
static const itype_id itype_glockmag( "glockmag" );
static const itype_id itype_knife_combat( "knife_combat" );
static const itype_id itype_water_clean( "water_clean" );

static const mongroup_id GROUP_ZOMBIE( "GROUP_ZOMBIE" );

static const mtype_id mon_zombie( "mon_zombie" );

static const npc_class_id NC_DOCTOR( "NC_DOCTOR" );
static const npc_class_id audit_j10_tuned( "audit_j10_tuned" );

static const spell_id spell_audit_p01_mark( "audit_p01_mark" );
static const spell_id spell_audit_p01_tree( "audit_p01_tree" );
static const spell_id spell_audit_p01_wonder( "audit_p01_wonder" );

static const string_id<behavior::node_t> behavior_node_t_audit_j10_needs( "audit_j10_needs" );
static const string_id<behavior::node_t> behavior_node_t_npc_needs( "npc_needs" );

static const weather_type_id weather_clear( "clear" );

// ---- MONDT-B04: signals visit only the hordes in reach ----

namespace
{

// what a signal can change of an entity
struct horde_entity_state {
    bool active = false;
    tripoint_abs_ms destination;
    int tracking_intensity = 0;
    bool operator==( const horde_entity_state &rhs ) const {
        return std::tie( active, destination, tracking_intensity ) ==
               std::tie( rhs.active, rhs.destination, rhs.tracking_intensity );
    }
};

using horde_states = std::map<tripoint_abs_ms, horde_entity_state>;

horde_states active_and_idle( horde_map &hordes )
{
    horde_states states;
    for( const int flavor : {
             horde_map_flavors::active, horde_map_flavors::idle
         } ) {
        for( std::pair<const tripoint_abs_ms, horde_entity> &e : hordes.get_view( flavor ) ) {
            states[e.first] = { flavor == horde_map_flavors::active, e.second.destination,
                                e.second.tracking_intensity
                              };
        }
    }
    return states;
}

// The signal as the old signal_entities() gave it, entity by entity: each one within reach of
// its submap hears it.
horde_states old_signal( horde_states states, const tripoint_abs_ms &origin, const int volume )
{
    const tripoint_abs_sm sm_dest = project_to<coords::sm>( origin );
    for( std::pair<const tripoint_abs_ms, horde_entity_state> &e : states ) {
        const int eff_power = volume - rl_dist( sm_dest, project_to<coords::sm>( e.first ) );
        if( eff_power <= 0 ) {
            continue;
        }
        const int scaled = eff_power * SEEX;
        if( !e.second.active || e.second.tracking_intensity < scaled ) {
            e.second = { true, origin, scaled };
        }
    }
    return states;
}

// the buckets, (active, submap), that hold these entities, and those within reach of origin
std::set<std::pair<bool, tripoint_abs_sm>> buckets_of( const horde_states &states )
{
    std::set<std::pair<bool, tripoint_abs_sm>> buckets;
    for( const std::pair<const tripoint_abs_ms, horde_entity_state> &e : states ) {
        buckets.emplace( e.second.active, project_to<coords::sm>( e.first ) );
    }
    return buckets;
}

int buckets_in_reach( const horde_states &states, const tripoint_abs_ms &origin, const int volume )
{
    const tripoint_abs_sm sm_dest = project_to<coords::sm>( origin );
    int count = 0;
    for( const std::pair<bool, tripoint_abs_sm> &bucket : buckets_of( states ) ) {
        count += square_dist( bucket.second, sm_dest ) <= volume - 1 ? 1 : 0;
    }
    return count;
}

// An overmap's worth of entities: idle ones every 9 submaps across it, a few on other levels,
// and every fourth one already tracking something.
void fill_hordes( horde_map &hordes )
{
    for( int x = 0; x < 2 * OMAPX; x += 9 ) {
        for( int y = 0; y < 2 * OMAPY; y += 9 ) {
            const int z = ( x + y ) % 45 == 0 ? ( x % 2 == 0 ? 2 : -1 ) : 0;
            const tripoint_om_ms local( x * SEEX + 3, y * SEEX + 5, z );
            hordes.spawn_entity( project_combine( hordes.get_location(), local ), mon_zombie );
            if( ( x / 9 + y / 9 ) % 4 == 0 ) {
                horde_map::node_type node = hordes.extract( hordes.find( local ) );
                node.mapped().tracking_intensity = 1 + ( x + y ) % 60;
                node.mapped().destination = node.key() + tripoint_rel_ms( 7, -3, 0 );
                hordes.insert( std::move( node ) );
            }
        }
    }
}

} // namespace

TEST_CASE( "mondt-b04_signal_entities_looks_only_at_buckets_in_reach", "[audit][hordes]" )
{
    restore_on_out_of_scope restore_trigdist( trigdist );
    trigdist = GENERATE( false, true );
    CAPTURE( trigdist );
    horde_map hordes;
    hordes.set_location( point_abs_om( 17, -4 ) );
    fill_hordes( hordes );
    const horde_states before = active_and_idle( hordes );
    const int total_buckets = static_cast<int>( buckets_of( before ).size() );
    REQUIRE( total_buckets > 1000 );
    REQUIRE( std::count_if( before.begin(), before.end(), []( const auto & e ) {
        return e.second.active;
    } ) > 100 );

    const tripoint_abs_ms center = project_combine( hordes.get_location(),
                                   tripoint_om_ms( 2000, 2100, 0 ) );
    SECTION( "a quiet signal looks only at the buckets near it" ) {
        // small enough that both maps are looked up by submap: a 7 x 7 x 7 box at the most
        for( const int volume : {
                 1, 2, 3, 4
             } ) {
            CAPTURE( volume );
            hordes.clear();
            fill_hordes( hordes );
            const int visited = hordes.signal_entities( center, volume );
            CHECK( active_and_idle( hordes ) == old_signal( before, center, volume ) );
            CHECK( visited == buckets_in_reach( before, center, volume ) );
            CHECK( visited < total_buckets / 10 );
        }
    }
    SECTION( "a signal from the next overmap looks at no bucket out of its reach" ) {
        // 3 submaps west of this overmap's west edge
        const tripoint_abs_ms west = project_combine( hordes.get_location(),
                                     tripoint_om_ms( -3 * SEEX, 2100, 0 ) );
        const int visited = hordes.signal_entities( west, 5 );
        CHECK( active_and_idle( hordes ) == old_signal( before, west, 5 ) );
        CHECK( visited == buckets_in_reach( before, west, 5 ) );
        CHECK( hordes.signal_entities( west, 2 ) == 0 );
    }
    SECTION( "a loud signal walks every bucket, with the same result" ) {
        const int visited = hordes.signal_entities( center, 400 );
        CHECK( active_and_idle( hordes ) == old_signal( before, center, 400 ) );
        CHECK( visited == total_buckets );
    }
    SECTION( "no signal" ) {
        CHECK( hordes.signal_entities( center, 0 ) == 0 );
        CHECK( active_and_idle( hordes ) == before );
    }
}

namespace
{

// The old overmap::signal_hordes(), over every zone group.
void old_signal_hordes( std::multimap<tripoint_om_sm, mongroup> &zg, const tripoint_abs_ms &p,
                        const int sig_power )
{
    const tripoint_abs_sm absp = project_to<coords::sm>( p );
    for( std::pair<const tripoint_om_sm, mongroup> &elem : zg ) {
        mongroup &mg = elem.second;
        if( !mg.horde ) {
            continue;
        }
        const int dist = rl_dist( absp, mg.abs_pos );
        if( sig_power < dist ) {
            continue;
        }
        if( mg.behaviour == mongroup::horde_behaviour::nemesis ) {
            continue;
        }
        const int calculated_inter = ( sig_power + 1 - dist ) * 15;
        const int roll = rng( 0, mg.interest );
        const int min_capped_inter = std::max( 30, calculated_inter );
        if( roll < min_capped_inter ) {
            const int targ_dist = rl_dist( absp.xy(), mg.target );
            if( targ_dist < 5 ) {
                mg.set_target( midpoint( mg.target, absp.xy() ) );
                mg.inc_interest( rng( 3, calculated_inter ) );
            } else {
                mg.set_target( absp.xy() );
                mg.set_interest( min_capped_inter );
            }
        }
    }
}

using group_view = std::vector<std::tuple<tripoint_om_sm, point_abs_sm, int>>;

group_view view_of( const std::multimap<tripoint_om_sm, mongroup> &zg )
{
    group_view view;
    for( const std::pair<const tripoint_om_sm, mongroup> &elem : zg ) {
        view.emplace_back( elem.first, elem.second.target, elem.second.interest );
    }
    return view;
}

} // namespace

TEST_CASE( "mondt-b04_signal_hordes_looks_only_at_zone_groups_in_reach", "[audit][hordes]" )
{
    restore_on_out_of_scope restore_trigdist( trigdist );
    trigdist = GENERATE( false, true );
    CAPTURE( trigdist );
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine saved_rng = rng_get_engine();
    const on_out_of_scope restore_rng( [&saved_rng]() {
        rng_get_engine() = saved_rng;
    } );

    // an overmap that is never generated: only its groups matter (on the heap: it is several
    // megabytes, too big for the stack)
    const std::unique_ptr<overmap> om_holder = std::make_unique<overmap>( point_abs_om( 23, 31 ) );
    overmap &om = *om_holder;
    const tripoint_abs_sm corner = project_to<coords::sm>( tripoint_abs_om( om.pos(), 0 ) );
    int index = 0;
    for( int x = 0; x < 2 * OMAPX; x += 6 ) {
        for( int y = 0; y < 2 * OMAPY; y += 6 ) {
            const int z = index % 13 == 0 ? 1 : 0;
            mongroup group( GROUP_ZOMBIE, corner + tripoint_rel_sm( x, y, z ), 10 );
            group.horde = index % 5 != 0;
            group.interest = 20 + index % 70;
            if( index % 3 == 0 ) {
                group.target = group.abs_pos.xy() + point_rel_sm( 2, 1 );
            }
            if( index % 97 == 0 ) {
                group.behaviour = mongroup::horde_behaviour::nemesis;
            }
            overmap_test_helper::add_mon_group( om, group );
            ++index;
        }
    }
    std::multimap<tripoint_om_sm, mongroup> &zg = overmap_test_helper::zone_groups( om );
    REQUIRE( zg.size() == static_cast<size_t>( index ) );

    const int sig_power = GENERATE( 3, 8, 20 );
    CAPTURE( sig_power );
    const tripoint_abs_ms here = project_to<coords::ms>( corner + tripoint_rel_sm( 151, 199, 0 ) );
    // the groups in the columns within reach
    const tripoint_abs_sm absp = project_to<coords::sm>( here );
    const int in_columns = static_cast<int>( std::count_if( zg.begin(), zg.end(),
    [&absp, sig_power]( const std::pair<const tripoint_om_sm, mongroup> &e ) {
        return square_dist( e.second.abs_pos.xy(), absp.xy() ) <= sig_power;
    } ) );

    const group_view before = view_of( zg );
    std::multimap<tripoint_om_sm, mongroup> expected = zg;
    const unsigned int seed = 4242u + static_cast<unsigned int>( sig_power ) * 1000003u;
    rng_set_engine_seed( seed );
    old_signal_hordes( expected, here, sig_power );
    // NOLINTNEXTLINE(cata-determinism)
    const cata_default_random_engine old_rng = rng_get_engine();

    rng_set_engine_seed( seed );
    const int visited = overmap_test_helper::signal_hordes( om, here, sig_power );
    // the same groups changed the same way, with the same random numbers
    CHECK( view_of( zg ) == view_of( expected ) );
    CHECK( rng_get_engine() == old_rng );
    CHECK( visited == in_columns );
    CHECK( visited < index / 10 );
    if( sig_power == 20 ) {
        // the signal did reach some
        CHECK( view_of( zg ) != before );
    }
}

// ---- MAGIC-P01: casterless casts share one fake caster ----

namespace
{

const std::vector<std::string> p01_globals = {
    "audit_p01_roots", "audit_p01_marks", "audit_p01_reads", "audit_p01_mark_before",
    "audit_p01_seen_mark"
};

void forget_p01_globals()
{
    for( const std::string &name : p01_globals ) {
        get_globals().remove_global_value( name );
    }
}

} // namespace

TEST_CASE( "magic-p01_casterless_casts_share_one_fake_caster", "[audit][magic]" )
{
    clear_map();
    clear_avatar();
    forget_p01_globals();
    const on_out_of_scope cleanup( []() {
        forget_p01_globals();
    } );
    const tripoint_bub_ms target = get_avatar().pos_bub() + tripoint( 5, 5, 0 );

    SECTION( "a spell, its extra spells and theirs" ) {
        const int built = spell::casterless_casters_built();
        spell( spell_audit_p01_tree ).cast_all_effects( target );
        // the old code built nine: three for each of the three spells
        CHECK( spell::casterless_casters_built() - built == 1 );
        // each effect ran once, as before
        CHECK( global_number( "audit_p01_roots" ) == 1 );
        CHECK( global_number( "audit_p01_marks" ) == 1 );
        CHECK( global_number( "audit_p01_reads" ) == 1 );
        // the spell after the one that marked the caster gets it as built
        CHECK( global_number( "audit_p01_seen_mark" ) == 0 );
    }
    SECTION( "a WONDER spell's picks" ) {
        const int built = spell::casterless_casters_built();
        spell( spell_audit_p01_wonder ).cast_all_effects( target );
        CHECK( spell::casterless_casters_built() - built == 1 );
        CHECK( global_number( "audit_p01_roots" ) == 0 );
        CHECK( global_number( "audit_p01_marks" ) == 3 );
        // every pick found the caster unmarked
        CHECK( global_number( "audit_p01_mark_before" ) == 0 );
    }
    SECTION( "one effect, or the extra spells alone" ) {
        int built = spell::casterless_casters_built();
        spell( spell_audit_p01_mark ).cast_spell_effect( target );
        CHECK( spell::casterless_casters_built() - built == 1 );
        built = spell::casterless_casters_built();
        spell( spell_audit_p01_tree ).cast_extra_spell_effects( target );
        CHECK( spell::casterless_casters_built() - built == 1 );
        CHECK( global_number( "audit_p01_marks" ) == 2 );
        CHECK( global_number( "audit_p01_reads" ) == 1 );
        CHECK( global_number( "audit_p01_seen_mark" ) == 0 );
        CHECK( global_number( "audit_p01_roots" ) == 0 );
    }
    SECTION( "separate casts don't share a caster" ) {
        spell( spell_audit_p01_mark ).cast_all_effects( target );
        spell( spell_audit_p01_mark ).cast_all_effects( target );
        CHECK( global_number( "audit_p01_marks" ) == 2 );
        CHECK( global_number( "audit_p01_mark_before" ) == 0 );
    }
}

// ---- INV-P02: a bitmask pocket set, pseudo items by reference, a public raw visitor ----

TEST_CASE( "inv-p02_pocket_type_set_holds_what_a_set_of_pocket_types_did", "[audit][inventory]" )
{
    static_assert( pocket_type_set{ pocket_type::CONTAINER }.contains( pocket_type::CONTAINER ),
                   "a pocket type set is built at compile time" );
    // the same list, as bits and as the set the visit APIs took before
    const auto same = []( const pocket_type_set bits, const std::set<pocket_type> &as_set ) {
        CHECK( bits.empty() == as_set.empty() );
        for( int i = 0; i < static_cast<int>( pocket_type::LAST ); ++i ) {
            const pocket_type type = static_cast<pocket_type>( i );
            CAPTURE( i );
            CHECK( bits.contains( type ) == ( as_set.count( type ) == 1 ) );
        }
    };
    same( {}, {} );
    same( { pocket_type::CONTAINER }, { pocket_type::CONTAINER } );
    same( { pocket_type::CONTAINER, pocket_type::MAGAZINE, pocket_type::MAGAZINE_WELL },
    { pocket_type::CONTAINER, pocket_type::MAGAZINE, pocket_type::MAGAZINE_WELL } );
    same( { pocket_type::MOD, pocket_type::EBOOK, pocket_type::MOD },
    { pocket_type::MOD, pocket_type::EBOOK, pocket_type::MOD } );
    CHECK( pocket_type_set{ pocket_type::MOD, pocket_type::EBOOK } ==
           pocket_type_set{ pocket_type::EBOOK, pocket_type::MOD } );
    CHECK( pocket_type_set{ pocket_type::MOD } != pocket_type_set{ pocket_type::EBOOK } );

    // a visit walks the pockets it is given, the container ones by default
    item mag( itype_glockmag );
    mag.ammo_set( itype_9mm, 5 );
    const auto count_visited = [&mag]( const std::optional<pocket_type_set> &pockets ) {
        int visited = 0;
        const std::function<VisitResponse( item *, item * )> count = [&visited]( item *, item * ) {
            ++visited;
            return VisitResponse::NEXT;
        };
        if( pockets ) {
            mag.visit_contents_legacy( count, &mag, *pockets );
        } else {
            mag.visit_contents_legacy( count, &mag );
        }
        return visited;
    };
    CHECK( count_visited( std::nullopt ) == 0 );
    CHECK( count_visited( pocket_type_set{ pocket_type::MAGAZINE } ) == 1 );
    CHECK( count_visited( pocket_type_set{ pocket_type::CONTAINER, pocket_type::MAGAZINE, pocket_type::MAGAZINE_WELL } )
           == 1 );
}

TEST_CASE( "inv-p02_pseudo_items_by_reference_and_the_raw_visitor", "[audit][inventory]" )
{
    clear_avatar();
    const on_out_of_scope cleanup( []() {
        clear_avatar();
    } );
    avatar &u = get_avatar();

    SECTION( "pseudo items come back as the cached list itself" ) {
        const std::vector<const item *> &none = u.get_pseudo_items();
        CHECK( none.empty() );
        // its razor is a plain pseudo item (bio_fuel_wood's woodshed is INTEGRATED: worn, not one)
        u.add_bionic( bio_razors );
        const std::vector<const item *> &first = u.get_pseudo_items();
        const std::vector<const item *> &second = u.get_pseudo_items();
        CHECK( &first == &second );
        CHECK( first.size() == 1 );
        // still found by what reads them
        REQUIRE_FALSE( first.empty() );
        CHECK( u.has_item( *first.front() ) );
        CHECK( u.amount_of( first.front()->typeId() ) == 1 );
    }

    SECTION( "the raw visitor walks what visit_items walks, in its order" ) {
        u.add_bionic( bio_fuel_wood );
        item bottle( itype_bottle_plastic );
        REQUIRE( bottle.put_in( item( itype_water_clean, calendar::turn, 2 ),
                                pocket_type::CONTAINER ).success() );
        item pack( itype_backpack );
        REQUIRE( pack.put_in( bottle, pocket_type::CONTAINER ).success() );
        u.wear_item( pack, false );
        u.set_wielded_item( item( itype_knife_combat ) );

        const auto walk_located = [&u]( const std::function<VisitResponse( const item & )> &say ) {
            std::vector<const item *> seen;
            const VisitResponse r = u.visit_items( [&]( const item_location & loc ) {
                seen.push_back( loc.get_item() );
                return say( *loc );
            } );
            return std::make_pair( seen, r );
        };
        const auto walk_raw = [&u]( const std::function<VisitResponse( const item & )> &say ) {
            std::vector<const item *> seen;
            const VisitResponse r = u.visit_items_raw( [&]( const item & it ) {
                seen.push_back( &it );
                return say( it );
            } );
            return std::make_pair( seen, r );
        };
        const auto next = []( const item & ) {
            return VisitResponse::NEXT;
        };
        const auto skip_backpack = []( const item & it ) {
            return it.typeId() == itype_backpack ? VisitResponse::SKIP : VisitResponse::NEXT;
        };
        const auto abort_at_water = []( const item & it ) {
            return it.typeId() == itype_water_clean ? VisitResponse::ABORT : VisitResponse::NEXT;
        };
        for( const std::function<VisitResponse( const item & )> &say : {
                 std::function<VisitResponse( const item & )>( next ),
                 std::function<VisitResponse( const item & )>( skip_backpack ),
                 std::function<VisitResponse( const item & )>( abort_at_water )
             } ) {
            const std::pair<std::vector<const item *>, VisitResponse> located = walk_located( say );
            const std::pair<std::vector<const item *>, VisitResponse> raw = walk_raw( say );
            CHECK( raw.first == located.first );
            CHECK( raw.second == located.second );
        }
        // skipping the backpack skips the bottle and the water in it
        CHECK( walk_raw( next ).first.size() == walk_raw( skip_backpack ).first.size() + 2 );
        CHECK( walk_raw( next ).second == VisitResponse::NEXT );
        CHECK( walk_raw( abort_at_water ).second == VisitResponse::ABORT );
        CHECK( walk_raw( abort_at_water ).first.back()->typeId() == itype_water_clean );
    }
}

// ---- NPC-J10: the embark urgencies, the cold the temperature need fires at, the noise window ----

TEST_CASE( "npc-j10_more_tuning_defaults_to_the_old_constants", "[audit][npc][npc_tuning]" )
{
    const npc_ai_tuning &t = npc_ai_tuning::defaults();
    CHECK( t.embark == 0.6f );
    CHECK( t.embark_seated == 0.15f );
    CHECK( t.cold_body_temperature == BODYTEMP_VERY_COLD );
    CHECK( t.restless_window == 30_minutes );
    CHECK( t.problems().empty() );
    for( const npc_class &cls : npc_class::get_all() ) {
        if( cls.id == audit_j10_tuned ) {
            continue;
        }
        CAPTURE( cls.id.str() );
        const npc_ai_tuning &c = cls.get_ai_tuning();
        CHECK( c.embark == t.embark );
        CHECK( c.embark_seated == t.embark_seated );
        CHECK( c.cold_body_temperature == t.cold_body_temperature );
        CHECK( c.restless_window == t.restless_window );
    }
    const npc_ai_tuning &tuned = audit_j10_tuned->get_ai_tuning();
    CHECK( tuned.embark == 0.3f );
    CHECK( tuned.embark_seated == 0.05f );
    CHECK( tuned.cold_body_temperature == units::from_celsius( 34 ) );
    CHECK( tuned.restless_window == 5_minutes );

    const auto rejects = []( const std::string & json, const std::string & why ) {
        CAPTURE( json );
        npc_ai_tuning fresh;
        const JsonObject jo = json_loader::from_string( json );
        jo.allow_omitted_members();
        CHECK_THROWS_WITH( fresh.load( jo ), Catch::Matchers::Contains( why ) );
    };
    rejects( R"({ "urgency": { "embark": 1.5 } })", "not between 0 and 1" );
    rejects( R"({ "urgency": { "embark_seated": -0.5 } })", "not between 0 and 1" );
    rejects( R"({ "sleep": { "restless_window": "0 seconds" } })", "a turn or longer" );
}

TEST_CASE( "npc-j10_embark_cold_and_noise_window_follow_the_class", "[audit][npc][npc_tuning]" )
{
    const ai_hooks_world world;
    npc &guy = hooks_npc();
    REQUIRE( guy.myclass == NC_DOCTOR );
    const behavior::character_oracle_t oracle( &guy );
    const auto tuned = [&guy]( const bool on ) {
        guy.myclass = on ? audit_j10_tuned : NC_DOCTOR;
    };
    const on_out_of_scope untune( [&tuned]() {
        tuned( false );
    } );

    SECTION( "boarding the avatar's vehicle" ) {
        avatar &u = get_avatar();
        restore_on_out_of_scope restore_in_vehicle( u.in_vehicle );
        guy.set_attitude( NPCATT_FOLLOW );
        u.in_vehicle = true;
        REQUIRE( oracle.npc_should_embark( "" ) == behavior::status_t::running );
        CHECK( oracle.npc_embark_urgency( "" ) == 0.6f );
        tuned( true );
        CHECK( oracle.npc_embark_urgency( "" ) == 0.3f );
    }
    SECTION( "a cold body part" ) {
        guy.set_all_parts_temp_conv( units::from_celsius( 33 ) );
        CHECK( oracle.needs_temperature_regulation( "" ) == behavior::status_t::success );
        tuned( true );
        CHECK( oracle.needs_temperature_regulation( "" ) == behavior::status_t::running );
        guy.set_all_parts_temp_conv( units::from_celsius( 30 ) );
        tuned( false );
        CHECK( oracle.needs_temperature_regulation( "" ) == behavior::status_t::running );
    }
    SECTION( "noise wakes ten minutes apart" ) {
        guy.note_noise( true );
        calendar::turn += 10_minutes;
        guy.note_noise( true );
        // within the half hour: the wakes add up
        CHECK( guy.get_sleep_session().noise_wakes == 2 );
        tuned( true );
        calendar::turn += 10_minutes;
        guy.note_noise( true );
        // the class counts only five minutes back
        CHECK( guy.get_sleep_session().noise_wakes == 1 );
    }
}

// ---- NPC-J10: needs content adds ----

namespace
{

const std::vector<std::string> j10_globals = {
    "audit_j10_starts", "audit_j10_turns", "audit_j10_ends", "audit_j10_end_reason"
};

std::vector<std::string> child_names( const behavior::node_t &node )
{
    std::vector<std::string> names;
    for( const behavior::node_t *child : node.get_children() ) {
        names.push_back( child->id.str() );
    }
    return names;
}

std::pair<std::string, float> decide( const behavior::node_t &root, const Character &who )
{
    const behavior::character_oracle_t oracle( &who );
    behavior::tree tree;
    tree.add( &root );
    return tree.tick_full( &oracle );
}

} // namespace

TEST_CASE( "npc-j10_a_custom_need_is_weighed_with_the_built_in_needs", "[audit][npc][npc_goal]" )
{
    const ai_hooks_world world;
    const scoped_weather_override weather( weather_clear );
    restore_on_out_of_scope restore_temperature( get_weather().forced_temperature );
    get_weather().forced_temperature = units::from_celsius( 20 );
    get_weather().clear_temp_cache();
    const on_out_of_scope cleanup( []() {
        set_npc_decision_tree_for_test( std::nullopt );
        for( const std::string &name : j10_globals ) {
            get_globals().remove_global_value( name );
        }
        get_weather().clear_temp_cache();
    } );
    npc &guy = hooks_npc();
    guy.set_hunger( 0 );
    guy.set_thirst( 0 );
    guy.set_sleepiness( 0 );
    guy.set_stored_kcal( guy.get_healthy_kcal() );
    guy.set_all_parts_temp_conv( BODYTEMP_NORM );
    guy.set_all_parts_temp_cur( BODYTEMP_NORM );

    // the built-in needs keep their node; the copy holds them and the custom need after them
    const std::vector<std::string> built_in = child_names( *behavior_node_t_npc_needs );
    CHECK( built_in == std::vector<std::string> {
        "npc_homeostasis", "npc_thirst", "npc_hunger", "npc_sleepiness"
    } );
    std::vector<std::string> with_need = built_in;
    with_need.emplace_back( "npc_need_audit_j10_need" );
    CHECK( child_names( *behavior_node_t_audit_j10_needs ) == with_need );
    const npc_json_goal *goal = npc_json_goals::find( "audit_j10_need" );
    REQUIRE( goal != nullptr );
    CHECK( goal->category.str() == "needs" );

    const behavior::node_t &needs = *behavior_node_t_audit_j10_needs;
    const auto set_need = [&guy]( const bool on, const double urgency ) {
        guy.set_value( "audit_j10_need_on", on ? 1.0 : 0.0 );
        guy.set_value( "audit_j10_urgency", urgency );
    };
    const const_dialogue d( get_const_talker_for( guy ), nullptr );

    SECTION( "felt from its act level, by the NPCs its condition picks" ) {
        set_need( true, 0.3 );
        CHECK( decide( needs, guy ).first == "idle" );
        CHECK( eval_math( "npc_need_urgency('audit_j10_need')", d ) == Approx( 0.3 ) );
        set_need( true, 0.5 );
        const std::pair<std::string, float> picked = decide( needs, guy );
        CHECK( picked.first == "audit_j10_need" );
        CHECK( picked.second == Approx( 0.5f ) );
        set_need( false, 0.9 );
        CHECK( decide( needs, guy ).first == "idle" );
        // the vanilla needs don't have it
        set_need( true, 0.9 );
        CHECK( decide( *behavior_node_t_npc_needs, guy ).first == "idle" );
        // an urgency past 1 counts as 1
        set_need( true, 7.0 );
        CHECK( eval_math( "npc_need_urgency('audit_j10_need')", d ) == Approx( 1.0 ) );
    }
    SECTION( "against thirst, the more urgent need wins" ) {
        guy.wear_item( item( itype_backpack ), false );
        item bottle( itype_bottle_plastic );
        REQUIRE( bottle.put_in( item( itype_water_clean, calendar::turn, 2 ),
                                pocket_type::CONTAINER ).success() );
        REQUIRE( guy.i_add( bottle ) );
        // thirst 600 of 1200
        guy.set_thirst( 600 );
        set_need( true, 0.9 );
        CHECK( decide( needs, guy ).first == "audit_j10_need" );
        set_need( true, 0.45 );
        CHECK( decide( needs, guy ).first == "drink_water" );
    }
    SECTION( "the NPC pursues it until it isn't felt" ) {
        set_npc_decision_tree_for_test( behavior_node_t_audit_j10_needs );
        set_need( true, 0.8 );
        run_npc_turn( guy );
        CHECK( guy.get_committed_goal() == "audit_j10_need" );
        CHECK( global_number( "audit_j10_starts" ) == 1 );
        CHECK( global_number( "audit_j10_turns" ) == 1 );
        run_npc_turn( guy );
        CHECK( guy.get_committed_goal() == "audit_j10_need" );
        CHECK( global_number( "audit_j10_turns" ) == 2 );
        set_need( true, 0.1 );
        run_npc_turn( guy );
        CHECK_FALSE( guy.json_goal->active() );
        CHECK( global_number( "audit_j10_ends" ) == 1 );
        CHECK( global_string( "audit_j10_end_reason" ) == "done" );
        CHECK( global_number( "audit_j10_starts" ) == 1 );
    }
    guy.remove_value( "audit_j10_need_on" );
    guy.remove_value( "audit_j10_urgency" );
}

TEST_CASE( "npc-j10_custom_need_loading", "[audit][npc][npc_goal]" )
{
    const auto load = []( const std::string & json ) {
        npc_custom_need need;
        need.id = string_id<npc_custom_need>( "audit_j10_loaded" );
        const JsonObject jo = json_loader::from_string( json );
        jo.allow_omitted_members();
        need.load( jo, "audit_partial_perf_ai_test" );
        return need;
    };
    // what isn't given: acts from 0.5, a need under npc_needs, done once it isn't felt
    const npc_custom_need plain = load( R"({ "urgency": 0.25 })" );
    CHECK( plain.act == 0.5f );
    CHECK( plain.category.str() == "needs" );
    CHECK( plain.parent.str() == "npc_needs" );
    CHECK( plain.node_id().str() == "npc_need_audit_j10_loaded" );
    const npc_json_goal goal = plain.goal();
    CHECK( goal.id == "audit_j10_loaded" );
    CHECK( goal.node == plain.node_id() );
    CHECK( static_cast<bool>( goal.until ) );
    CHECK_FALSE( goal.timeout );
    // the avatar feels no NPC need
    CHECK( plain.urgency_for( get_avatar() ) == 0.0f );
    CHECK_FALSE( plain.felt_by( get_avatar() ) );

    CHECK_THROWS_WITH( load( R"({ "urgency": 0.25, "act": 1.5 })" ),
                       Catch::Matchers::Contains( "not between 0 and 1" ) );
    CHECK_THROWS_WITH( load( R"({ "urgency": 0.25, "timeout": "0 seconds" })" ),
                       Catch::Matchers::Contains( "more than 0 turns" ) );
    CHECK_THROWS( load( R"({ "act": 0.2 })" ) );

    // its goal can't take a built-in goal's id or another goal's
    npc_goal_registry reg;
    npc_json_goal builtin = goal;
    builtin.id = "drink_water";
    CHECK_THAT( reg.add_goal( builtin, "npc_need a" ), Catch::Matchers::Contains( "built-in" ) );
    CHECK( reg.add_goal( goal, "npc_need b" ).empty() );
    npc_json_goal twin = goal;
    twin.node = string_id<behavior::node_t>( "elsewhere" );
    CHECK_THAT( reg.add_goal( twin, "npc_need c" ), Catch::Matchers::Contains( "defined by" ) );
    // the same source gives it again: replaced, not doubled
    CHECK( reg.add_goal( goal, "npc_need b" ).empty() );
    CHECK( reg.all().size() == 1 );
    CHECK( reg.claims().size() == 1 );
}
