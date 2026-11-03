#include <functional>
#include <string>
#include <vector>

#include "activity_actor.h"
#include "activity_actor_eoc.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "debug.h"
#include "dialogue.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "global_vars.h"
#include "item.h"
#include "item_components.h"
#include "item_location.h"
#include "json_error.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "npc.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "pocket_type.h"
#include "point.h"
#include "recipe.h"
#include "requirements.h"
#include "skill.h"
#include "talker.h"
#include "type_id.h"

// ACT-J02: u_assign_activity with a target ("target_item", "target_loc", "recipe",
// "butcher_type"), for the avatar and NPCs, with no menus.

static const activity_id ACT_BUTCHER( "ACT_BUTCHER" );
static const activity_id ACT_CRAFT( "ACT_CRAFT" );
static const activity_id ACT_DISASSEMBLE( "ACT_DISASSEMBLE" );
static const activity_id ACT_READ( "ACT_READ" );
static const activity_id ACT_RELOAD( "ACT_RELOAD" );
static const activity_id ACT_UNLOAD( "ACT_UNLOAD" );
static const activity_id ACT_WEAR( "ACT_WEAR" );

static const furn_str_id furn_f_workbench( "f_workbench" );

static const itype_id itype_2x4( "2x4" );
static const itype_id itype_556( "556" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_cudgel( "cudgel" );
static const itype_id itype_knife_combat( "knife_combat" );
static const itype_id itype_splinter( "splinter" );
static const itype_id itype_stick( "stick" );
static const itype_id itype_stanag30( "stanag30" );
static const itype_id itype_test_audit_hook_widget( "test_audit_hook_widget" );
static const itype_id itype_test_textbook_fabrication( "test_textbook_fabrication" );
static const itype_id itype_tshirt( "tshirt" );
static const itype_id itype_water_clean( "water_clean" );
static const itype_id itype_bottle_plastic( "bottle_plastic" );

static const mtype_id mon_chicken( "mon_chicken" );

static const recipe_id recipe_cudgel_test_activity_hooks( "cudgel_test_activity_hooks" );
static const recipe_id recipe_cudgel_test_eoc_mixed( "cudgel_test_eoc_mixed" );

static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_survival( "survival" );

static const trait_id trait_DEBUG_CNF( "DEBUG_CNF" );

namespace
{

talk_effect_t load_effect( const std::string &effect )
{
    const JsonObject jo = json_loader::from_string( R"({ "effect": )" + effect + " }" );
    jo.allow_omitted_members();
    talk_effect_t ret( jo, "effect", "eoc_errands_activity_test" );
    finalize_conditions();
    return ret;
}

// The effect, with the test's true_eocs and false_eocs, run with `who` as u and `target` as npc.
void assign( const std::string &fields, Character &who, item_location &target )
{
    dialogue d( get_talker_for( who ), get_talker_for( target ) );
    load_effect( R"({ "u_assign_activity": )" + fields +
                 R"(, "true_eocs": [ "EOC_TEST_ERRAND_STARTED" ], )"
                 R"("false_eocs": [ "EOC_TEST_ERRAND_REFUSED" ] })" ).apply( d );
}

std::string global_string( const std::string &name )
{
    const diag_value *const value = get_globals().maybe_get_global_value( name );
    return value == nullptr ? std::string() : value->str();
}

// "started", "refused" or "" (neither ran)
std::string result()
{
    return global_string( "test_errand_result" );
}

std::string reason()
{
    return global_string( "test_errand_reason" );
}

void run_activity_to_the_end( Character &who, const activity_id &type )
{
    int guard = 0;
    while( who.activity.id() == type ) {
        REQUIRE( ++guard < 100000 );
        who.set_moves( 100 );
        who.activity.do_turn( who );
    }
}

// A clear map at noon, the avatar, and an NPC next to them with nothing on.
npc &setup()
{
    clear_map();
    clear_avatar();
    get_globals().clear_global_values();
    set_time( calendar::turn_zero + 12_hours );
    avatar &u = get_avatar();
    npc &guy = spawn_npc( u.pos_bub().xy() + point::east, "test_talker" );
    clear_character( guy );
    return guy;
}

void teardown()
{
    get_globals().clear_global_values();
    clear_map();
    clear_avatar();
}

item_location on_ground( const tripoint_bub_ms &pos, const item &it )
{
    item &placed = get_map().add_item( pos, it );
    return item_location( map_cursor( pos ), &placed );
}

int count_on_map_and_person( const Character &who, const itype_id &type )
{
    int count = 0;
    for( const item *it : who.items_with( [&type]( const item & i ) {
    return i.typeId() == type;
    } ) ) {
        count += it->count_by_charges() ? it->charges : 1;
    }
    map &here = get_map();
    for( const tripoint_bub_ms &p : here.points_in_radius( who.pos_bub(), 1 ) ) {
        for( const item &it : here.i_at( p ) ) {
            if( it.typeId() == type ) {
                count += it.count_by_charges() ? it.charges : 1;
            }
        }
    }
    return count;
}

} // namespace

TEST_CASE( "act-j02_targeted_activities_are_checked_at_load", "[audit][activity][errands_ui]" )
{
    // A targeted activity needs its target.
    CHECK_THROWS_WITH( load_effect( R"({ "u_assign_activity": "ACT_READ" })" ),
                       Catch::Matchers::Contains( "ACT_READ needs \"target_item\"" ) );
    // The item is the other talker.
    CHECK_THROWS_WITH( load_effect( R"({ "u_assign_activity": "ACT_READ", "target_item": "u" })" ),
                       Catch::Matchers::Contains( "the other talker" ) );
    CHECK_THROWS_AS( load_effect( R"({ "npc_assign_activity": "ACT_READ", "target_item": "npc" })" ),
                     JsonError );
    // An activity that takes no target names itself.
    CHECK_THROWS_WITH( load_effect( R"({ "u_assign_activity": "ACT_WAIT", "target_item": "npc" })" ),
                       Catch::Matchers::Contains( "ACT_WAIT takes no \"target_item\"" ) );
    CHECK_THROWS_WITH( load_effect( R"({ "u_assign_activity": "ACT_GENERIC_EOC", )"
                                    R"("duration": "1 minutes", "recipe": "cudgel" })" ),
                       Catch::Matchers::Contains( "ACT_GENERIC_EOC takes no \"recipe\"" ) );
    // Fields that don't fit the activity.
    CHECK_THROWS_AS( load_effect( R"({ "u_assign_activity": "ACT_DISASSEMBLE", )"
                                  R"("target_item": "npc", "butcher_type": "quick" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_assign_activity": "ACT_BUTCHER", )"
                                  R"("target_item": "npc", "butcher_type": "sideways" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_assign_activity": "ACT_BUTCHER_FULL", )"
                                  R"("target_item": "npc", "butcher_type": "full" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_assign_activity": "ACT_CRAFT" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_assign_activity": "ACT_CRAFT", "recipe": "cudgel", )"
                                  R"("target_item": "npc" })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_assign_activity": "ACT_CRAFT", "target_item": "npc", )"
                                  R"("target_loc": { "context_val": "bench" } })" ), JsonError );
    CHECK_THROWS_AS( load_effect( R"({ "u_assign_activity": "ACT_READ", "target_item": "npc", )"
                                  R"("duration": "1 hours" })" ), JsonError );

    CHECK_NOTHROW( load_effect( R"({ "u_assign_activity": "ACT_READ", "target_item": "npc" })" ) );
    CHECK_NOTHROW( load_effect( R"({ "npc_assign_activity": "ACT_BUTCHER", "target_item": "u", )"
                                R"("butcher_type": "full" })" ) );
    CHECK_NOTHROW( load_effect( R"({ "u_assign_activity": "ACT_CRAFT", "recipe": "cudgel", )"
                                R"("target_loc": { "context_val": "bench" } })" ) );
    CHECK_NOTHROW( load_effect( R"({ "u_assign_activity": "ACT_CRAFT", "target_item": "npc" })" ) );
    CHECK_NOTHROW( load_effect( R"({ "u_assign_activity": "ACT_WEAR", "target_item": "npc", )"
                                R"("true_eocs": [ "EOC_TEST_ERRAND_STARTED" ] })" ) );
    CHECK( activity_actors::takes_eoc_target( ACT_READ ) );
    CHECK_FALSE( activity_actors::takes_eoc_target( activity_id( "ACT_WAIT" ) ) );
}

TEST_CASE( "act-j02_eoc_reads_a_book", "[audit][activity][errands_ui]" )
{
    npc &guy = setup();
    avatar &u = get_avatar();

    SECTION( "the avatar skims a book it hasn't read" ) {
        item_location book = u.i_add( item( itype_test_textbook_fabrication ) );
        REQUIRE( book );
        REQUIRE_FALSE( u.has_identified( itype_test_textbook_fabrication ) );
        assign( R"("ACT_READ", "target_item": "npc")", u, book );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_READ );
        run_activity_to_the_end( u, ACT_READ );
        CHECK( u.has_identified( itype_test_textbook_fabrication ) );
    }
    SECTION( "an NPC reads a book from the ground next to it, with no menu" ) {
        item_location book = on_ground( guy.pos_bub(), item( itype_test_textbook_fabrication ) );
        REQUIRE( guy.get_skill_level_object( skill_fabrication ).knowledgeExperience() == 0 );
        assign( R"("ACT_READ", "target_item": "npc")", guy, book );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_READ );
        run_activity_to_the_end( guy, ACT_READ );
        const SkillLevel &level = guy.get_skill_level_object( skill_fabrication );
        CHECK( ( level.knowledgeExperience() > 0 || level.knowledgeLevel() > 0 ) );
    }
    SECTION( "a book out of reach is refused, with the reason" ) {
        item_location book = on_ground( guy.pos_bub() + tripoint_rel_ms( 3, 0, 0 ),
                                        item( itype_test_textbook_fabrication ) );
        assign( R"("ACT_READ", "target_item": "npc")", guy, book );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "out of reach" ) );
        CHECK( guy.activity.is_null() );
    }
    SECTION( "a book right below is out of reach: not through the floor" ) {
        item_location book = on_ground( guy.pos_bub() + tripoint_rel_ms( 0, 0, -1 ),
                                        item( itype_test_textbook_fabrication ) );
        assign( R"("ACT_READ", "target_item": "npc")", guy, book );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "out of reach" ) );
        CHECK( guy.activity.is_null() );
    }
    SECTION( "something that isn't a book is refused" ) {
        item_location plank = guy.i_add( item( itype_2x4 ) );
        assign( R"("ACT_READ", "target_item": "npc")", guy, plank );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "not good reading material" ) );
        CHECK( guy.activity.is_null() );
    }
    SECTION( "the other talker has to be an item" ) {
        dialogue d( get_talker_for( guy ), get_talker_for( u ) );
        load_effect( R"({ "u_assign_activity": "ACT_READ", "target_item": "npc", )"
                     R"("false_eocs": [ "EOC_TEST_ERRAND_REFUSED" ] })" ).apply( d );
        CHECK( result() == "refused" );
        CHECK( guy.activity.is_null() );
    }
    teardown();
}

TEST_CASE( "act-j02_eoc_butchers_a_corpse", "[audit][activity][errands_ui]" )
{
    npc &guy = setup();
    avatar &u = get_avatar();
    map &here = get_map();
    // Survival 10: the harvest roll can't fail badly.
    u.set_skill_level( skill_survival, 10 );
    guy.set_skill_level( skill_survival, 10 );

    SECTION( "an NPC with a knife butchers a corpse next to it" ) {
        item knife( itype_knife_combat );
        REQUIRE( guy.wield( knife ) );
        const tripoint_bub_ms spot = guy.pos_bub() + tripoint_rel_ms( 1, 0, 0 );
        item_location corpse = on_ground( spot, item::make_corpse( mon_chicken ) );
        assign( R"("ACT_BUTCHER", "target_item": "npc", "butcher_type": "quick")", guy, corpse );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_BUTCHER );
        run_activity_to_the_end( guy, ACT_BUTCHER );
        int corpses = 0;
        int other = 0;
        for( const item &it : here.i_at( spot ) ) {
            ( it.is_corpse() ? corpses : other )++;
        }
        CHECK( corpses == 0 );
        CHECK( other > 0 );
    }
    SECTION( "the avatar butchers quickly when no butcher_type is given" ) {
        item knife( itype_knife_combat );
        REQUIRE( u.wield( knife ) );
        item_location corpse = on_ground( u.pos_bub(), item::make_corpse( mon_chicken ) );
        assign( R"("ACT_BUTCHER", "target_item": "npc")", u, corpse );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_BUTCHER );
        run_activity_to_the_end( u, ACT_BUTCHER );
        for( const item &it : here.i_at( u.pos_bub() ) ) {
            CHECK_FALSE( it.is_corpse() );
        }
    }
    SECTION( "an NPC without a cutting tool is refused, with no popup" ) {
        item_location corpse = on_ground( guy.pos_bub(), item::make_corpse( mon_chicken ) );
        const std::string msg = capture_debugmsg_during( [&]() {
            assign( R"("ACT_BUTCHER", "target_item": "npc", "butcher_type": "full")", guy, corpse );
        } );
        CHECK( msg.empty() );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "tools" ) );
        CHECK( guy.activity.is_null() );
    }
    SECTION( "a corpse that is carried can't be butchered" ) {
        item knife( itype_knife_combat );
        REQUIRE( guy.wield( knife ) );
        item backpack( itype_backpack );
        guy.wear_item( backpack );
        item_location corpse = guy.i_add( item::make_corpse( mon_chicken ) );
        REQUIRE( corpse );
        assign( R"("ACT_BUTCHER", "target_item": "npc")", guy, corpse );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "on the ground" ) );
    }
    teardown();
}

TEST_CASE( "act-j02_eoc_disassembles_an_item", "[audit][activity][errands_ui]" )
{
    npc &guy = setup();
    avatar &u = get_avatar();
    global_variables &globvars = get_globals();

    SECTION( "an NPC takes apart an item on the ground next to it" ) {
        item_location widget = on_ground( guy.pos_bub(), item( itype_test_audit_hook_widget ) );
        assign( R"("ACT_DISASSEMBLE", "target_item": "npc")", guy, widget );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_DISASSEMBLE );
        run_activity_to_the_end( guy, ACT_DISASSEMBLE );
        CHECK( globvars.get_global_value( "disassembled" ) == 1 );
        CHECK( count_on_map_and_person( guy, itype_splinter ) == 2 );
    }
    SECTION( "the item is used up: the result EOCs get the disassembly, later effects no item" ) {
        item_location widget = on_ground( guy.pos_bub(), item( itype_test_audit_hook_widget ) );
        dialogue d( get_talker_for( guy ), get_talker_for( widget ) );
        // EOC_TEST_ERRAND_STARTED names npc in a message and in test_errand_npc.
        load_effect( R"([ { "u_assign_activity": "ACT_DISASSEMBLE", "target_item": "npc", )"
                     R"("true_eocs": [ "EOC_TEST_ERRAND_STARTED" ] }, )"
                     R"({ "set_string_var": "<npc_name>|", "target_var": { "global_val": "test_errand_after" }, )"
                     R"("parse_tags": true } ])" ).apply( d );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_DISASSEMBLE );
        const item_location work = guy.activity.actor->hook_target( guy.activity );
        REQUIRE( work );
        CHECK( global_string( "test_errand_npc" ) == work->type_name() );
        // The EOC that started it still holds the widget, which is gone: no name, no crash.
        CHECK_FALSE( widget );
        CHECK( global_string( "test_errand_after" ) == "|" );
        run_activity_to_the_end( guy, ACT_DISASSEMBLE );
        CHECK( globvars.get_global_value( "disassembled" ) == 1 );
    }
    SECTION( "the avatar takes apart an item it carries" ) {
        item_location widget = u.i_add( item( itype_test_audit_hook_widget ) );
        assign( R"("ACT_DISASSEMBLE", "target_item": "npc")", u, widget );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_DISASSEMBLE );
        run_activity_to_the_end( u, ACT_DISASSEMBLE );
        CHECK( globvars.get_global_value( "disassembled" ) == 1 );
    }
    SECTION( "a favorite isn't taken apart: the game would ask" ) {
        item favorite( itype_test_audit_hook_widget );
        favorite.set_favorite( true );
        item_location widget = on_ground( guy.pos_bub(), favorite );
        assign( R"("ACT_DISASSEMBLE", "target_item": "npc")", guy, widget );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "favorite" ) );
        CHECK( guy.activity.is_null() );
    }
    SECTION( "something with no disassembly is refused" ) {
        REQUIRE_FALSE( item( itype_splinter ).is_disassemblable() );
        item_location splinter = on_ground( guy.pos_bub(), item( itype_splinter ) );
        assign( R"("ACT_DISASSEMBLE", "target_item": "npc")", guy, splinter );
        CHECK( result() == "refused" );
        CHECK_FALSE( reason().empty() );
        CHECK( guy.activity.is_null() );
    }
    teardown();
}

TEST_CASE( "act-j02_eoc_crafts_a_recipe", "[audit][activity][errands_ui]" )
{
    npc &guy = setup();
    avatar &u = get_avatar();
    map &here = get_map();
    for( Character *who : std::vector<Character *> { &u, &guy } ) {
        if( !who->has_trait( trait_DEBUG_CNF ) ) {
            who->toggle_trait( trait_DEBUG_CNF );
        }
    }

    SECTION( "an NPC crafts in hand, with no menu" ) {
        guy.learn_recipe( &recipe_cudgel_test_activity_hooks.obj() );
        on_ground( guy.pos_bub(), item( itype_2x4 ) );
        item_location none;
        assign( R"("ACT_CRAFT", "recipe": "cudgel_test_activity_hooks")", guy, none );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_CRAFT );
        run_activity_to_the_end( guy, ACT_CRAFT );
        CHECK( count_on_map_and_person( guy, itype_cudgel ) == 1 );
        CHECK( count_on_map_and_person( guy, itype_2x4 ) == 0 );
    }
    SECTION( "the avatar crafts at a workbench" ) {
        u.learn_recipe( &recipe_cudgel_test_activity_hooks.obj() );
        // The player's recraft target isn't the EOC's craft.
        u.lastrecipe = recipe_cudgel_test_eoc_mixed;
        u.last_batch = 3;
        const tripoint_bub_ms bench = u.pos_bub() + tripoint_rel_ms( 0, 1, 0 );
        here.furn_set( bench, furn_f_workbench );
        u.i_add( item( itype_2x4 ) );
        dialogue d( get_talker_for( u ), nullptr );
        d.set_value( "bench", here.get_abs( bench ) );
        load_effect( R"({ "u_assign_activity": "ACT_CRAFT", "recipe": "cudgel_test_activity_hooks", )"
                     R"("target_loc": { "context_val": "bench" }, )"
                     R"("true_eocs": [ "EOC_TEST_ERRAND_STARTED" ] })" ).apply( d );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_CRAFT );
        bool on_bench = false;
        for( const item &it : here.i_at( bench ) ) {
            on_bench |= it.is_craft();
        }
        CHECK( on_bench );
        CHECK( u.lastrecipe == recipe_cudgel_test_eoc_mixed );
        CHECK( u.last_batch == 3 );
        run_activity_to_the_end( u, ACT_CRAFT );
        CHECK( count_on_map_and_person( u, itype_cudgel ) == 1 );
    }
    SECTION( "the avatar crafts with components split between pack and ground, with no menu" ) {
        u.learn_recipe( &recipe_cudgel_test_eoc_mixed.obj() );
        item backpack( itype_backpack );
        u.wear_item( backpack );
        u.i_add( item( itype_2x4 ) );
        u.i_add( item( itype_stick ) );
        on_ground( u.pos_bub(), item( itype_2x4 ) );
        on_ground( u.pos_bub(), item( itype_stick ) );
        item_location none;
        const std::string msg = capture_debugmsg_during( [&]() {
            assign( R"("ACT_CRAFT", "recipe": "cudgel_test_eoc_mixed")", u, none );
        } );
        CHECK( msg.empty() );
        CHECK( result() == "started" );
        REQUIRE( u.activity.id() == ACT_CRAFT );
        // The first alternative, from both places.
        CHECK( count_on_map_and_person( u, itype_2x4 ) == 0 );
        CHECK( count_on_map_and_person( u, itype_stick ) == 2 );
    }
    SECTION( "an NPC goes on with a craft that lost a component: it uses one at hand, unasked" ) {
        guy.learn_recipe( &recipe_cudgel_test_activity_hooks.obj() );
        // A craft that used a 2x4 but no longer holds it, as after a failure.
        item lost( &recipe_cudgel_test_activity_hooks.obj(), 1, item_components(),
                   std::vector<item_comp> { item_comp( itype_2x4, 1 ) } );
        REQUIRE_FALSE( lost.get_continue_reqs().is_empty() );
        item_location craft = on_ground( guy.pos_bub(), lost );
        on_ground( guy.pos_bub(), item( itype_2x4 ) );
        const std::string msg = capture_debugmsg_during( [&]() {
            assign( R"("ACT_CRAFT", "target_item": "npc")", guy, craft );
        } );
        CHECK( msg.empty() );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_CRAFT );
        CHECK( count_on_map_and_person( guy, itype_2x4 ) == 0 );
        run_activity_to_the_end( guy, ACT_CRAFT );
        CHECK( count_on_map_and_person( guy, itype_cudgel ) == 1 );
    }
    SECTION( "going on with a craft whose missing component isn't at hand is refused" ) {
        guy.learn_recipe( &recipe_cudgel_test_activity_hooks.obj() );
        item lost( &recipe_cudgel_test_activity_hooks.obj(), 1, item_components(),
                   std::vector<item_comp> { item_comp( itype_2x4, 1 ) } );
        item_location craft = on_ground( guy.pos_bub(), lost );
        assign( R"("ACT_CRAFT", "target_item": "npc")", guy, craft );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "aren't at hand" ) );
        CHECK( guy.activity.is_null() );
    }
    SECTION( "an NPC goes on with a craft in progress" ) {
        guy.learn_recipe( &recipe_cudgel_test_activity_hooks.obj() );
        on_ground( guy.pos_bub(), item( itype_2x4 ) );
        item_location none;
        assign( R"("ACT_CRAFT", "recipe": "cudgel_test_activity_hooks")", guy, none );
        REQUIRE( guy.activity.id() == ACT_CRAFT );
        guy.cancel_activity();
        const std::vector<item *> crafts = guy.items_with( []( const item & it ) {
            return it.is_craft();
        } );
        REQUIRE( crafts.size() == 1 );
        item_location craft( guy, crafts.front() );
        REQUIRE( craft );
        get_globals().clear_global_values();
        assign( R"("ACT_CRAFT", "target_item": "npc")", guy, craft );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_CRAFT );
        run_activity_to_the_end( guy, ACT_CRAFT );
        CHECK( count_on_map_and_person( guy, itype_cudgel ) == 1 );
    }
    SECTION( "an NPC that doesn't know the recipe is refused" ) {
        on_ground( guy.pos_bub(), item( itype_2x4 ) );
        item_location none;
        assign( R"("ACT_CRAFT", "recipe": "cudgel_test_activity_hooks")", guy, none );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "doesn't know how to make" ) );
        CHECK( guy.activity.is_null() );
    }
    SECTION( "a craft without its components is refused" ) {
        guy.learn_recipe( &recipe_cudgel_test_activity_hooks.obj() );
        item_location none;
        assign( R"("ACT_CRAFT", "recipe": "cudgel_test_activity_hooks")", guy, none );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "aren't at hand" ) );
        CHECK( guy.activity.is_null() );
    }
    teardown();
}

TEST_CASE( "act-j02_eoc_wears_an_item", "[audit][activity][errands_ui]" )
{
    npc &guy = setup();

    SECTION( "an NPC puts on a shirt from the ground" ) {
        item_location shirt = on_ground( guy.pos_bub(), item( itype_tshirt ) );
        assign( R"("ACT_WEAR", "target_item": "npc")", guy, shirt );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_WEAR );
        run_activity_to_the_end( guy, ACT_WEAR );
        CHECK( guy.is_wearing( itype_tshirt ) );
    }
    SECTION( "something that can't be worn is refused" ) {
        item_location plank = on_ground( guy.pos_bub(), item( itype_2x4 ) );
        assign( R"("ACT_WEAR", "target_item": "npc")", guy, plank );
        CHECK( result() == "refused" );
        CHECK_FALSE( reason().empty() );
        CHECK( guy.activity.is_null() );
    }
    SECTION( "someone else's shirt is refused" ) {
        item shirt( itype_tshirt );
        shirt.set_owner( faction_id( "free_merchants" ) );
        item_location loc = on_ground( guy.pos_bub(), shirt );
        assign( R"("ACT_WEAR", "target_item": "npc")", guy, loc );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "belongs to" ) );
    }
    teardown();
}

TEST_CASE( "act-j02_eoc_unloads_and_reloads", "[audit][activity][errands_ui]" )
{
    npc &guy = setup();
    item backpack( itype_backpack );
    guy.wear_item( backpack );

    SECTION( "an NPC reloads a magazine it carries" ) {
        item_location mag = guy.i_add( item( itype_stanag30 ) );
        guy.i_add( item( itype_556, calendar::turn, 30 ) );
        REQUIRE( mag->ammo_remaining() == 0 );
        assign( R"("ACT_RELOAD", "target_item": "npc")", guy, mag );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_RELOAD );
        run_activity_to_the_end( guy, ACT_RELOAD );
        CHECK( mag->ammo_remaining() == 30 );
    }
    SECTION( "an NPC unloads a magazine it carries" ) {
        item full( itype_stanag30 );
        full.put_in( item( itype_556, calendar::turn, 10 ), pocket_type::MAGAZINE );
        item_location mag = guy.i_add( full );
        REQUIRE( mag->ammo_remaining() == 10 );
        assign( R"("ACT_UNLOAD", "target_item": "npc")", guy, mag );
        CHECK( result() == "started" );
        REQUIRE( guy.activity.id() == ACT_UNLOAD );
        run_activity_to_the_end( guy, ACT_UNLOAD );
        CHECK( mag->ammo_remaining() == 0 );
        CHECK( count_on_map_and_person( guy, itype_556 ) == 10 );
    }
    SECTION( "nothing to reload with is refused" ) {
        item_location mag = guy.i_add( item( itype_stanag30 ) );
        assign( R"("ACT_RELOAD", "target_item": "npc")", guy, mag );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "Nothing at hand reloads" ) );
        CHECK( guy.activity.is_null() );
    }
    SECTION( "reloading something on the ground is refused: it has to be carried" ) {
        item_location mag = on_ground( guy.pos_bub(), item( itype_stanag30 ) );
        guy.i_add( item( itype_556, calendar::turn, 30 ) );
        assign( R"("ACT_RELOAD", "target_item": "npc")", guy, mag );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "has to be carried" ) );
    }
    SECTION( "unloading liquid is refused: it would ask where to pour it" ) {
        item bottle( itype_bottle_plastic );
        bottle.put_in( item( itype_water_clean, calendar::turn, 2 ), pocket_type::CONTAINER );
        item_location loc = on_ground( guy.pos_bub(), bottle );
        const std::string msg = capture_debugmsg_during( [&]() {
            assign( R"("ACT_UNLOAD", "target_item": "npc")", guy, loc );
        } );
        CHECK( msg.empty() );
        CHECK( result() == "refused" );
        CHECK_THAT( reason(), Catch::Matchers::Contains( "pour out liquid" ) );
        CHECK( guy.activity.is_null() );
    }
    teardown();
}
