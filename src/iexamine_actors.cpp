#include "iexamine_actors.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string_view>
#include <utility>

#include "ammo_effect.h"
#include "calendar.h"
#include "character.h"
#include "clone_ptr.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "effect_on_condition.h"
#include "enum_conversions.h"
#include "explosion.h"
#include "flag.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "game_constants.h"
#include "game_inventory.h"
#include "generic_factory.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_components.h"
#include "item_location.h"
#include "itype.h"
#include "map.h"
#include "map_iterator.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "mapgen_functions.h"
#include "mapgendata.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "output.h"
#include "overmap_ui.h"
#include "overmapbuffer.h"
#include "point.h"
#include "recipe.h"
#include "requirements.h"
#include "ret_val.h"
#include "rng.h"
#include "string_formatter.h"
#include "talker.h"
#include "timed_event.h"
#include "translations.h"
#include "uilist.h"
#include "units.h"
#include "units_utility.h"
#include "value_ptr.h"
#include "veh_appliance.h"
#include "visitable.h"

static const activity_id ACT_MORTAR_AIMING( "ACT_MORTAR_AIMING" );

static const ter_str_id ter_t_card_reader_broken( "t_card_reader_broken" );

void appliance_convert_examine_actor::load( const JsonObject &jo, const std::string & )
{
    optional( jo, false, "furn_set", furn_set );
    optional( jo, false, "ter_set", ter_set );
    mandatory( jo, false, "item", appliance_item );
}

void appliance_convert_examine_actor::call( Character &you, const tripoint_bub_ms &examp ) const
{
    if( !query_yn( _( "Connect %s to grid?" ), item::nname( appliance_item ) ) ) {
        return;
    }
    map &here = get_map();
    if( furn_set ) {
        here.furn_set( examp, *furn_set );
    }
    if( ter_set ) {
        here.ter_set( examp, *ter_set );
    }

    place_appliance( here, examp, vpart_appliance_from_item( appliance_item ), you );
}

void appliance_convert_examine_actor::finalize() const
{
    if( furn_set && !furn_set->is_valid() ) {
        debugmsg( "Invalid furniture id %s in appliance_convert action", furn_set->str() );
    }
    if( ter_set && !ter_set->is_valid() ) {
        debugmsg( "Invalid terrain id %s in appliance_convert action", ter_set->str() );
    }

    if( !appliance_item.is_valid() ) {
        debugmsg( "Invalid appliance item %s in appliance_convert action", appliance_item.str() );
    } else if( !vpart_appliance_from_item( appliance_item ).is_valid() ) {
        // This will never actually trigger now, but is here if the semantics of vpart_appliance_from_item change
        debugmsg( "In appliance_convert action, %s does not correspond to an appliance",
                  appliance_item.str() );
    }
}

std::unique_ptr<iexamine_actor> appliance_convert_examine_actor::clone() const
{
    return std::make_unique<appliance_convert_examine_actor>( *this );
}

void cardreader_examine_actor::consume_card( const std::vector<item_location> &cards ) const
{
    if( !consume ) {
        return;
    }
    std::vector<item_location> opts;
    for( const item_location &it : cards ) {
        const auto stacks = [&it]( const item_location & compare ) {
            return it->stacks_with( *compare );
        };
        if( std::any_of( opts.begin(), opts.end(), stacks ) ) {
            continue;
        }
        opts.push_back( it );
    }

    if( opts.size() == 1 ) {
        opts[0].remove_item();
        return;
    }

    uilist query;
    query.text = _( "Use which item?" );
    query.allow_cancel = false;
    for( size_t i = 0; i < opts.size(); ++i ) {
        query.entries.emplace_back( static_cast<int>( i ), true, -1, opts[i]->tname() );
    }
    query.query();
    opts[query.ret].remove_item();
}

std::vector<item_location> cardreader_examine_actor::get_cards( Character &you,
        const tripoint_bub_ms &examp )const
{
    std::vector<item_location> ret;

    you.visit_items( [&]( const item_location & it ) {
        const auto has_card_flag = [&it]( const flag_id & flag ) {
            return it->has_flag( flag );
        };
        if( std::none_of( allowed_flags.begin(), allowed_flags.end(), has_card_flag ) ) {
            return VisitResponse::NEXT;
        }
        if( omt_allowed_radius ) {
            tripoint_abs_omt cardloc = coords::project_to<coords::omt>(
                                           it->get_var( "spawn_location", tripoint_abs_ms::min ) );
            // Cards without a location are treated as valid
            if( cardloc == tripoint_abs_omt::min ) {
                ret.push_back( it );
                return VisitResponse::NEXT;
            }
            int dist = rl_dist( cardloc.xy(),
                                coords::project_to<coords::omt>( get_map().get_abs( examp ) ).xy() );
            if( dist > *omt_allowed_radius ) {
                return VisitResponse::NEXT;
            }
        }

        ret.push_back( it );
        return VisitResponse::NEXT;
    } );

    return ret;
}

bool cardreader_examine_actor::apply( const tripoint_bub_ms &examp ) const
{
    bool open = true;

    map &here = get_map();
    if( map_regen ) {
        tripoint_abs_omt omt_pos( coords::project_to<coords::omt>( here.get_abs( examp ) ) );
        const ret_val<void> has_colliding_vehicle = run_mapgen_update_func( mapgen_id, omt_pos, {}, nullptr,
                false );
        if( !has_colliding_vehicle.success() ) {
            debugmsg( "Failed to apply magen function %s, collision with %s", mapgen_id.str(),
                      has_colliding_vehicle.str() );
        }
        set_queued_points();
        here.set_transparency_cache_dirty( examp.z() );
    } else {
        open = false;
        const tripoint_range<tripoint_bub_ms> points = here.points_in_radius( examp, radius );
        for( const tripoint_bub_ms &tmp : points ) {
            const auto ter_iter = terrain_changes.find( here.ter( tmp ).id() );
            const auto furn_iter = furn_changes.find( here.furn( tmp ).id() );
            if( ter_iter != terrain_changes.end() ) {
                here.ter_set( tmp, ter_iter->second );
                open = true;
            }
            if( furn_iter != furn_changes.end() ) {
                here.furn_set( tmp, furn_iter->second );
                open = true;
            }
        }
    }

    return open;
}

/**
 * Use id/hack reader. Using an id despawns turrets.
 */
void cardreader_examine_actor::call( Character &you, const tripoint_bub_ms &examp ) const
{
    bool open = false;
    map &here = get_map();

    std::vector<item_location> cards = get_cards( you, examp );

    if( !cards.empty() && query_yn( _( query_msg ) ) ) {
        you.mod_moves( -to_moves<int>( 1_seconds ) );
        open = apply( examp );
        for( monster &critter : g->all_monsters() ) {
            if( !despawn_monsters ) {
                break;
            }
            // Check 1) same overmap coords, 2) turret, 3) hostile
            if( coords::project_to<coords::omt>( here.get_abs( critter.pos_bub() ) ) ==
                coords::project_to<coords::omt>( here.get_abs( examp ) ) &&
                critter.has_flag( mon_flag_ID_CARD_DESPAWN ) &&
                critter.attitude_to( you ) == Creature::Attitude::HOSTILE ) {
                g->remove_zombie( critter );
            }
        }
        if( open ) {
            add_msg( _( success_msg ) );
            consume_card( cards );
        } else {
            add_msg( _( redundant_msg ) );
        }
    } else if( allow_hacking && iexamine::can_hack( you ) &&
               query_yn( _( "Attempt to hack this card-reader?" ) ) ) {
        iexamine::try_start_hacking( you, examp );
    } else if( !allow_hacking && iexamine::can_hack( you ) ) {
        add_msg( _( "This card-reader cannot be hacked." ) );
    }
}

void cardreader_examine_actor::load( const JsonObject &jo, const std::string & )
{
    mandatory( jo, false, "flags", allowed_flags );
    optional( jo, false, "consume_card", consume, true );
    optional( jo, false, "allow_hacking", allow_hacking, true );
    optional( jo, false, "despawn_monsters", despawn_monsters, true );
    optional( jo, false, "omt_allowed_radius", omt_allowed_radius );
    if( jo.has_string( "mapgen_id" ) ) {
        optional( jo, false, "mapgen_id", mapgen_id );
        map_regen = true;
    } else {
        optional( jo, false, "radius", radius, 3 );
        optional( jo, false, "terrain_changes", terrain_changes );
        optional( jo, false, "furn_changes", furn_changes );
    }
    optional( jo, false, "query", query, true );
    optional( jo, false, "query_msg", query_msg );
    mandatory( jo, false, "success_msg", success_msg );
    mandatory( jo, false, "redundant_msg", redundant_msg );
    optional( jo, false, "hacked_ter", hacked_ter );
    optional( jo, false, "hacked_furn", hacked_furn );

}

bool cardreader_examine_actor::hack( const tripoint_bub_ms &examp, bool furniture_reader ) const
{
    map &here = get_map();
    const ter_id reader_ter = here.ter( examp );
    const furn_id reader_furn = here.furn( examp );
    // the doors (or the mapgen update) first: apply() looks at the tiles around the reader
    const bool opened = apply( examp );
    // the reader itself, unless the changes already replaced it
    if( here.ter( examp ) == reader_ter ) {
        if( hacked_ter ) {
            here.ter_set( examp, *hacked_ter );
        } else if( !furniture_reader ) {
            here.ter_set( examp, ter_t_card_reader_broken );
        }
    }
    if( hacked_furn && here.furn( examp ) == reader_furn ) {
        here.furn_set( examp, *hacked_furn );
    }
    return opened;
}

void cardreader_examine_actor::check( const std::string &owner ) const
{
    if( hacked_ter && !hacked_ter->is_valid() ) {
        debugmsg( "card reader of %s: hacked_ter \"%s\" is not a terrain", owner, hacked_ter->str() );
    }
    if( hacked_furn && !hacked_furn->is_valid() ) {
        debugmsg( "card reader of %s: hacked_furn \"%s\" is not furniture", owner,
                  hacked_furn->str() );
    }
}

void cardreader_examine_actor::finalize() const
{
    if( allowed_flags.empty() ) {
        debugmsg( "Cardreader examine actor has no allowed card flags." );
    }

    for( const flag_id &flag : allowed_flags ) {
        if( !flag.is_valid() ) {
            debugmsg( "Cardreader uses flag %s that does not exist!", flag.str() );
        }
    }

    if( terrain_changes.empty() && furn_changes.empty() && mapgen_id.is_empty() ) {
        debugmsg( "Cardreader examine actor does not change either terrain or furniture" );
    }

    if( query && query_msg.empty() ) {
        debugmsg( "Cardreader is told to query, yet does not have a query message defined." );
    }
    // hacking makes the same changes as a card (ACT-J06), so any reader may allow it
}

std::unique_ptr<iexamine_actor> cardreader_examine_actor::clone() const
{
    return std::make_unique<cardreader_examine_actor>( *this );
}

void eoc_examine_actor::call( Character &you, const tripoint_bub_ms &examp ) const
{
    const map &here = get_map();
    const map_data_common_t &data = here.has_furn( examp ) ?
                                    static_cast<const map_data_common_t &>( here.furn( examp ).obj() ) :
                                    static_cast<const map_data_common_t &>( here.ter( examp ).obj() );
    call_with_context( you, examp, data );
}

void eoc_examine_actor::call_with_context( Character &you, const tripoint_bub_ms &examp,
        const map_data_common_t &data ) const
{
    map &here = get_map();
    const bool furniture = &data == &here.furn( examp ).obj();

    dialogue d( get_talker_for( you ), nullptr );
    d.set_value( "this", furniture ? here.furn( examp ).id().str() : here.ter( examp ).id().str() );
    d.set_value( "terrain", here.ter( examp ).id().str() );
    d.set_value( "furniture", here.furn( examp ).id().str() );
    d.set_value( "examined_layer", furniture ? "furniture" : "terrain" );
    d.set_value( "pos", here.get_abs( examp ) );
    for( const effect_on_condition_id &eoc : eocs ) {
        eoc->activate( d );
    }
}

void eoc_examine_actor::load( const JsonObject &jo, const std::string &src )
{
    for( JsonValue jv : jo.get_array( "effect_on_conditions" ) ) {
        eocs.emplace_back( effect_on_conditions::load_inline_eoc( jv, src ) );
    }
}

void eoc_examine_actor::finalize() const
{
    for( const effect_on_condition_id &eoc : eocs ) {
        if( !eoc.is_valid() ) {
            debugmsg( "Invalid effect_on_condition_id: %s", eoc.str() );
        }
    }
}

std::unique_ptr<iexamine_actor> eoc_examine_actor::clone() const
{
    return std::make_unique<eoc_examine_actor>( *this );
}

bool process_item_filter::matches( const item &it ) const
{
    if( fresh && it.rotten() ) {
        return false;
    }
    if( bionic && !it.is_bionic() ) {
        return false;
    }
    if( materials.empty() && flags.empty() && items.empty() ) {
        return true;
    }
    if( !materials.empty() && it.made_of_any( materials ) ) {
        return true;
    }
    if( std::any_of( flags.begin(), flags.end(), [&it]( const flag_id & f ) {
    return it.has_flag( f );
    } ) ) {
        return true;
    }
    return std::find( items.begin(), items.end(), it.typeId() ) != items.end();
}

bool process_item_filter::empty() const
{
    return materials.empty() && flags.empty() && items.empty() && !fresh && !bionic;
}

void process_item_filter::deserialize( const JsonObject &jo )
{
    optional( jo, false, "materials", materials );
    optional( jo, false, "flags", flags );
    optional( jo, false, "items", items );
    optional( jo, false, "fresh", fresh, false );
    optional( jo, false, "bionic", bionic, false );
}

void process_item_filter::check( const std::string &owner ) const
{
    for( const material_id &mat : materials ) {
        if( !mat.is_valid() ) {
            debugmsg( "%s: process takes unknown material %s", owner, mat.str() );
        }
    }
    for( const flag_id &flag : flags ) {
        if( !flag.is_valid() ) {
            debugmsg( "%s: process takes unknown item flag %s", owner, flag.str() );
        }
    }
    for( const itype_id &type : items ) {
        if( !type.is_valid() ) {
            debugmsg( "%s: process takes unknown item %s", owner, type.str() );
        }
    }
}

void process_message::print() const
{
    if( !text.empty() ) {
        add_msg( type, string_format( text.translated() ) );
    }
}

void process_message::print( const std::string &arg ) const
{
    if( !text.empty() ) {
        add_msg( type, string_format( text.translated(), arg ) );
    }
}

void process_message::print( int arg ) const
{
    if( !text.empty() ) {
        add_msg( type, string_format( text.translated(), arg ) );
    }
}

void process_message::print( const std::string &arg1, const std::string &arg2 ) const
{
    if( !text.empty() ) {
        add_msg( type, string_format( text.translated(), arg1, arg2 ) );
    }
}

void process_message::deserialize( const JsonValue &jv )
{
    if( jv.test_object() ) {
        JsonObject jo = jv.get_object();
        if( jo.has_member( "text" ) ) {
            mandatory( jo, false, "text", text );
            if( jo.has_string( "type" ) ) {
                type = io::string_to_enum<game_message_type>( jo.get_string( "type" ) );
            }
            return;
        }
    }
    jv.read( text, true );
}

void process_time_message::print( const time_duration &left ) const
{
    if( !exact.empty() ) {
        add_msg( string_format( exact.translated(), to_string( left ) ) );
        return;
    }
    const int num_hours = to_hours<int>( left );
    const int num_minutes = to_minutes<int>( left ) + 1;
    if( num_minutes > 60 ) {
        if( !hours.empty() ) {
            add_msg( string_format( hours.translated( num_hours ), num_hours ) );
        }
    } else if( num_minutes > 30 ) {
        if( !less_than_an_hour.empty() ) {
            add_msg( string_format( less_than_an_hour.translated() ) );
        }
    } else if( !minutes.empty() ) {
        add_msg( string_format( minutes.translated( num_minutes ), num_minutes ) );
    }
}

void process_time_message::deserialize( const JsonValue &jv )
{
    if( !jv.test_object() ) {
        jv.read( exact, true );
        return;
    }
    JsonObject jo = jv.get_object();
    optional( jo, false, "hours", hours );
    optional( jo, false, "less_than_an_hour", less_than_an_hour );
    optional( jo, false, "minutes", minutes );
}

// A message, or a list of messages.
static void read_messages( const JsonObject &jo, std::string_view name,
                           std::vector<process_message> &messages )
{
    messages.clear();
    if( jo.has_array( name ) ) {
        for( const JsonValue jv : jo.get_array( name ) ) {
            process_message msg;
            msg.deserialize( jv );
            messages.push_back( msg );
        }
    } else if( jo.has_member( name ) ) {
        process_message msg;
        msg.deserialize( jo.get_member( name ) );
        messages.push_back( msg );
    }
}

static void read_process_eocs( const JsonObject &jo, const std::string_view src,
                               std::vector<effect_on_condition_id> &eocs )
{
    eocs.clear();
    for( JsonValue jv : jo.get_array( "effect_on_conditions" ) ) {
        eocs.emplace_back( effect_on_conditions::load_inline_eoc( jv, src ) );
    }
}

static void check_process_eocs( const std::string &owner,
                                const std::vector<effect_on_condition_id> &eocs )
{
    for( const effect_on_condition_id &eoc : eocs ) {
        if( !eoc.is_valid() ) {
            debugmsg( "%s: process runs unknown effect_on_condition %s", owner, eoc.str() );
        }
    }
}

static dialogue process_dialogue( Character &you, const tripoint_bub_ms &examp )
{
    map &here = get_map();
    dialogue d( get_talker_for( you ), nullptr );
    d.set_value( "this", here.furn( examp ).id().str() );
    d.set_value( "pos", here.get_abs( examp ) );
    return d;
}

dialogue process_start_examine_actor::make_dialogue( Character &you,
        const tripoint_bub_ms &examp, const units::volume &total ) const
{
    dialogue d = process_dialogue( you, examp );
    d.set_value( "volume", static_cast<double>( to_milliliter( total ) ) );
    if( skill ) {
        d.set_value( "skill_level", static_cast<double>( you.get_skill_level( *skill ) ) );
    }
    return d;
}

bool process_start_examine_actor::prep( Character &, const tripoint_bub_ms &examp ) const
{
    map &here = get_map();
    bool present = false;
    for( const item &it : here.i_at( examp ) ) {
        if( !reject_if_contains.empty() && reject_if_contains.matches( it ) ) {
            for( const process_message &msg : contains_message ) {
                msg.print( it.tname( 1, false ) );
            }
            return false;
        } else if( accepts.matches( it ) ) {
            present = true;
        } else {
            reject_message.print( it.tname( 1, false ) );
            return false;
        }
    }
    if( !present ) {
        empty_message.print();
        return false;
    }
    if( requires_outside && !here.is_outside( examp ) ) {
        outside_message.print();
        return false;
    }
    return true;
}

bool process_start_examine_actor::begin( Character &you, const tripoint_bub_ms &examp,
        bool ask ) const
{
    map &here = get_map();
    units::volume total = 0_ml;
    for( const item &it : here.i_at( examp ) ) {
        total += it.volume();
    }
    dialogue d = make_dialogue( you, examp, total );

    int charges = 0;
    if( !intermediate.is_null() ) {
        // whole percents, as the kilns always had
        const int percent = static_cast<int>( yield_percent.evaluate( d ) );
        charges = item::find_type( intermediate )->charges_per_volume( percent * total / 100 );
        if( charges < 1 ) {
            too_small_message.print();
            return false;
        }
    }

    std::optional<requirement_data> reqs;
    if( requirements ) {
        reqs = requirements->obj();
        if( !reqs->can_make_with_inventory( &you, you.crafting_inventory(), is_crafting_component ) ) {
            popup( "%s", reqs->list_missing() );
            return false;
        }
    }

    units::energy needed = 0_kJ;
    if( energy ) {
        needed = units::from_kilojoule( static_cast<std::int64_t>( energy->evaluate( d ) ) );
        if( you.available_ups() < needed ) {
            no_energy_message.print();
            return false;
        }
    }

    if( ask ) {
        ready_message.print( format_volume( total ), volume_units_abbr() );
        if( !query.empty() && !query_yn( query.translated() ) ) {
            return false;
        }
    }

    if( reqs ) {
        for( const std::vector<item_comp> &comp : reqs->get_components() ) {
            you.consume_items( comp, 1, is_crafting_component );
        }
        for( const std::vector<tool_comp> &tool : reqs->get_tools() ) {
            you.consume_tools( tool );
        }
        you.invalidate_crafting_inventory();
    }
    if( energy ) {
        you.consume_ups( needed );
    }
    if( !intermediate.is_null() ) {
        here.i_clear( examp );
    } else if( reset_age ) {
        for( item &it : here.i_at( examp ) ) {
            it.set_birthday( calendar::turn );
        }
    }
    here.furn_set( examp, furn_set );
    if( !intermediate.is_null() ) {
        item result( intermediate, calendar::turn );
        result.charges = charges;
        here.add_item( examp, result );
    }
    success_message.print();

    for( const effect_on_condition_id &eoc : eocs ) {
        eoc->activate( d );
    }
    return true;
}

bool process_start_examine_actor::fire( Character &you, const tripoint_bub_ms &examp ) const
{
    if( !prep( you, examp ) ) {
        return false;
    }
    return begin( you, examp, false );
}

void process_start_examine_actor::call( Character &you, const tripoint_bub_ms &examp ) const
{
    if( !prep( you, examp ) ) {
        return;
    }
    if( firestarter ) {
        iexamine::fire_process( you, examp );
        return;
    }
    begin( you, examp, true );
}

const process_start_examine_actor *process_start_examine_actor::firestarter_process(
    const furn_t &furn )
{
    for( const cata::clone_ptr<iexamine_actor> &act : furn.get_examine_actors() ) {
        const process_start_examine_actor *process =
            dynamic_cast<const process_start_examine_actor *>( act.get() );
        if( process != nullptr && process->firestarter ) {
            return process;
        }
    }
    return nullptr;
}

void process_start_examine_actor::load( const JsonObject &jo, const std::string &src )
{
    optional( jo, false, "accepts", accepts );
    if( jo.has_member( "reject_if_contains" ) ) {
        optional( jo, false, "reject_if_contains", reject_if_contains );
        if( reject_if_contains.materials.empty() && reject_if_contains.flags.empty() &&
            reject_if_contains.items.empty() ) {
            jo.throw_error_at( "reject_if_contains", "Needs materials, flags or items" );
        }
    }
    read_messages( jo, "contains_message", contains_message );
    optional( jo, false, "reject_message", reject_message, process_message( m_bad ) );
    optional( jo, false, "empty_message", empty_message );
    optional( jo, false, "requires_outside", requires_outside, false );
    optional( jo, false, "outside_message", outside_message );
    optional( jo, false, "skill", skill );
    optional( jo, false, "intermediate", intermediate, itype_id::NULL_ID() );
    if( !intermediate.is_null() ) {
        mandatory( jo, false, "yield_percent", yield_percent );
    } else if( jo.has_member( "yield_percent" ) ) {
        jo.throw_error_at( "yield_percent", "A yield needs an \"intermediate\" item" );
    }
    optional( jo, false, "too_small_message", too_small_message );
    optional( jo, false, "energy", energy );
    optional( jo, false, "no_energy_message", no_energy_message );
    optional( jo, false, "requirements", requirements );
    optional( jo, false, "firestarter", firestarter, false );
    optional( jo, false, "ready_message", ready_message );
    optional( jo, false, "query", query );
    optional( jo, false, "reset_age", reset_age, false );
    optional( jo, false, "success_message", success_message );
    mandatory( jo, false, "furn_set", furn_set );
    read_process_eocs( jo, src, eocs );
}

void process_start_examine_actor::finalize() const
{
}

void process_start_examine_actor::check( const std::string &owner ) const
{
    accepts.check( owner );
    reject_if_contains.check( owner );
    if( !furn_set.is_valid() ) {
        debugmsg( "%s: process_start sets unknown furniture %s", owner, furn_set.str() );
    }
    if( !intermediate.is_null() && !intermediate.is_valid() ) {
        debugmsg( "%s: process_start makes unknown item %s", owner, intermediate.str() );
    }
    if( skill && !skill->is_valid() ) {
        debugmsg( "%s: process_start uses unknown skill %s", owner, skill->str() );
    }
    if( requirements && !requirements->is_valid() ) {
        debugmsg( "%s: process_start needs unknown requirement %s", owner, requirements->str() );
    }
    check_process_eocs( owner, eocs );
}

std::unique_ptr<iexamine_actor> process_start_examine_actor::clone() const
{
    return std::make_unique<process_start_examine_actor>( *this );
}

// The stook's drying: each smokable item becomes its smoking result, as old as it was.
static void dry_to_smoking_result( item &it )
{
    item result( it.get_comestible()->smoking_result, it.birthday() );
    recipe rec;
    result.inherit_flags( it, rec );
    if( !result.has_flag( flag_NUTRIENT_OVERRIDE ) ) {
        // If the item has "cooks_like" it will be replaced by that item as a component.
        if( !it.get_comestible()->cooks_like.is_empty() ) {
            // Set charges to 1 for stacking purposes.
            it = item( it.get_comestible()->cooks_like, it.birthday(), 1 );
        }
        result.components.add( it );
    }
    it = result;
}

void process_finish_examine_actor::call( Character &you, const tripoint_bub_ms &examp ) const
{
    map &here = get_map();
    map_stack items = here.i_at( examp );
    if( items.empty() ) {
        empty_message.print();
        here.furn_set( examp, furn_set );
        return;
    }
    if( !accepts.empty() ) {
        for( const item &it : items ) {
            if( !accepts.matches( it ) ) {
                reject_message.print( it.tname( 1, false ) );
                return;
            }
        }
    }
    examine_message.print();

    dialogue d = process_dialogue( you, examp );
    const time_duration time_left = duration.evaluate( d ) - items.begin()->age();
    if( time_left > 0_turns ) {
        time_left_message.print( time_left );
        return;
    }

    int made = 0;
    if( !output.is_null() ) {
        units::volume total = 0_ml;
        for( map_stack::iterator it = items.begin(); it != items.end(); ) {
            if( std::find( output_from.begin(), output_from.end(), it->typeId() ) != output_from.end() ) {
                total += it->volume();
                it = items.erase( it );
            } else {
                ++it;
            }
        }
        item result( output, calendar::turn );
        result.charges = item::find_type( output )->charges_per_volume( total );
        made = result.charges;
        here.add_item( examp, result );
    }
    if( convert_to_smoking_result ) {
        for( item &it : items ) {
            if( it.is_smokable() ) {
                dry_to_smoking_result( it );
                made++;
            }
        }
    }
    bool all_unpacked = false;
    if( sterilize_cbms ) {
        all_unpacked = std::all_of( items.begin(), items.end(), []( const item & it ) {
            return it.is_bionic() && it.has_flag( flag_NO_PACKED );
        } );
        for( item &it : items ) {
            if( !it.has_flag( flag_NO_PACKED ) ) {
                it.unset_flag( flag_NO_STERILE );
            }
        }
    }
    here.furn_set( examp, furn_set );

    if( !convert_to_smoking_result || made > 0 ) {
        done_message.print( made );
    }
    if( all_unpacked ) {
        add_msg( m_info,
                 _( "CBMs in direct contact with the environment will almost immediately become contaminated." ) );
    }
    for( const effect_on_condition_id &eoc : eocs ) {
        eoc->activate( d );
    }
}

void process_finish_examine_actor::load( const JsonObject &jo, const std::string &src )
{
    mandatory( jo, false, "duration", duration );
    mandatory( jo, false, "furn_set", furn_set );
    optional( jo, false, "empty_message", empty_message );
    optional( jo, false, "accepts", accepts );
    optional( jo, false, "reject_message", reject_message, process_message( m_bad ) );
    optional( jo, false, "examine_message", examine_message );
    optional( jo, false, "time_left_message", time_left_message );
    if( jo.has_object( "output" ) ) {
        JsonObject out = jo.get_object( "output" );
        mandatory( out, false, "item", output );
        mandatory( out, false, "from", output_from );
    }
    optional( jo, false, "convert_to_smoking_result", convert_to_smoking_result, false );
    optional( jo, false, "sterilize_cbms", sterilize_cbms, false );
    optional( jo, false, "done_message", done_message );
    read_process_eocs( jo, src, eocs );
}

void process_finish_examine_actor::finalize() const
{
}

void process_finish_examine_actor::check( const std::string &owner ) const
{
    accepts.check( owner );
    if( !furn_set.is_valid() ) {
        debugmsg( "%s: process_finish sets unknown furniture %s", owner, furn_set.str() );
    }
    if( !output.is_null() && !output.is_valid() ) {
        debugmsg( "%s: process_finish makes unknown item %s", owner, output.str() );
    }
    for( const itype_id &from : output_from ) {
        if( !from.is_valid() ) {
            debugmsg( "%s: process_finish uses up unknown item %s", owner, from.str() );
        }
    }
    check_process_eocs( owner, eocs );
}

std::unique_ptr<iexamine_actor> process_finish_examine_actor::clone() const
{
    return std::make_unique<process_finish_examine_actor>( *this );
}

void mortar_examine_actor::call( Character &you, const tripoint_bub_ms &examp ) const
{
    map &here = get_map();

    dialogue d( get_talker_for( you ), nullptr );

    if( has_condition && !condition( d ) ) {
        add_msg( condition_fail_msg.translated() );
        return;
    }

    std::vector<ammotype> expected_ammo_types = ammo_type;
    inventory_filter_preset preset( [expected_ammo_types]( const item_location & loc ) {
        for( ammotype desired_ammo : expected_ammo_types ) {
            if( desired_ammo == loc->ammo_type() ) {
                return true;
            };
        }
        return false;
    } );
    inventory_pick_selector inv_s( you, preset );
    inv_s.add_nearby_items( PICKUP_RANGE );
    inv_s.add_character_items( you );
    inv_s.set_title( _( "Pick a projectile to be used." ) );

    if( inv_s.empty() ) {
        add_msg( _( "You have no rounds to use the mortar." ) );
        return;
    }

    item_location loc;
    if( inv_s.item_entry_count() == 0 ) {
        add_msg( _( "You have no rounds to use the mortar." ) );
        return;
    } else if( inv_s.item_entry_count() == 1 ) {
        loc = inv_s.get_only_choice().first;
    } else {
        loc = inv_s.execute();
    }

    if( loc == item_location::nowhere ) {
        return;
    }

    const int aim_range = range / 24;
    const tripoint_abs_omt pos_omt = project_to<coords::omt>( here.get_abs( examp ) );
    tripoint_abs_omt target = ui::omap::choose_point( "Pick a target.", pos_omt, false, aim_range );

    if( target == tripoint_abs_omt::invalid ) {
        return;
    }

    if( rl_dist( you.pos_abs_omt(), target ) <=
        std::ceil( static_cast<float>( MAX_VIEW_DISTANCE ) / ( 2 * SEEX ) ) ) {
        add_msg( _( "Target is too close." ) );
        return;
    }
    time_duration aim_dur = aim_duration.evaluate( d );
    you.assign_activity( ACT_MORTAR_AIMING, to_moves<int>( aim_dur ) );

    tripoint_abs_ms target_abs_ms = project_to<coords::ms>( target );

    const int deviation = ( aim_deviation.evaluate( d ) * rl_dist( you.pos_abs(), target_abs_ms ) / 2 );
    // aim at the center of OMT, but with some deviation
    // we just assume mortar projectiles fall at 90 degrees, duh
    target_abs_ms.x() += rng_float( 12 + deviation, 12 - deviation );
    target_abs_ms.y() += rng_float( 12 + deviation, 12 - deviation );
    // we can have edge cases with it if, for example, we target radio tower (high building, but with small profile)
    target_abs_ms.z() = overmap_buffer.highest_omt_point( project_to<coords::omt>( target_abs_ms ) );

    for( ammo_effect_str_id ammo_eff : loc.get_item()->ammo_data()->ammo->ammo_effects ) {
        if( ammo_eff.obj().aoe_explosion_data.power > 0 ) {
            get_timed_events().add( timed_event_type::EXPLOSION,
                                    calendar::turn + flight_time.evaluate( d ) + aim_dur,
                                    target_abs_ms, ammo_eff.obj().aoe_explosion_data );
        }

    }

    loc->charges--;
    if( loc->charges <= 0 ) {
        loc.remove_item();
    }

    d.set_value( "this", here.furn( examp ).id().str() );
    d.set_value( "pos", here.get_abs( examp ) );
    d.set_value( "target", target_abs_ms );
    for( const effect_on_condition_id &eoc : eocs ) {
        eoc->activate( d );
    }
}

void mortar_examine_actor::load( const JsonObject &jo, const std::string &src )
{
    mandatory( jo, false, "ammo", ammo_type );
    mandatory( jo, false, "range", range );
    if( jo.has_member( "condition" ) ) {
        read_condition( jo, "condition", condition, false );
        has_condition = true;
    }
    optional( jo, false, "condition_fail_msg", condition_fail_msg,
              to_translation( "You can't use this mortar." ) );

    optional( jo, false, "aim_deviation", aim_deviation, 0.0f );
    optional( jo, false, "aim_duration", aim_duration, 0_seconds );
    optional( jo, false, "flight_time", flight_time, 0_seconds );

    for( JsonValue jv : jo.get_array( "effect_on_conditions" ) ) {
        eocs.emplace_back( effect_on_conditions::load_inline_eoc( jv, src ) );
    }
}

void mortar_examine_actor::finalize() const
{
    for( ammotype ammo : ammo_type ) {
        if( !ammo.is_valid() ) {
            debugmsg( "Invalid ammo type: %s", ammo.str() );
        }
    }
}

std::vector<ammotype> mortar_examine_actor::get_ammotypes() const
{
    return ammo_type;
}

std::unique_ptr<iexamine_actor> mortar_examine_actor::clone() const
{
    return std::make_unique<mortar_examine_actor>( *this );
}
