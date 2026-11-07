#include "faction.h"

#include <algorithm>
#include <bitset>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "calendar.h"
#include "character.h"
#include "debug.h"
#include "dialogue.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "generic_factory.h"
#include "pimpl.h"
#include "shop_cons_rate.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"
#include "vitamin.h"

static const faction_id faction_amf( "amf" );
static const faction_id faction_no_faction( "no_faction" );
static const faction_id faction_your_followers( "your_followers" );

static const mfaction_str_id monfaction_human( "human" );

faction_template::faction_template()
{
    likes_u = 0;
    respects_u = 0;
    trusts_u = 0;
    known_by_u = true;
    _food_supply.clear();
    wealth = 0;
    size = 0;
    power = 0;
    lone_wolf_faction = false;
    limited_area_claim = false;
    consumes_food = false;
    currency = itype_id::NULL_ID();
    steal_persist = std::nullopt; // Default ask
    mutiny_faction = faction_amf;
}

faction::faction( const faction_template &templ )
{
    id = templ.id;
    // first init *all* members, than copy those from the template
    static_cast<faction_template &>( *this ) = templ;
}

// generic_factory holds objects whose id is a string_id of their own type, but a
// faction_template's id is the id of the faction it starts (faction_id, a string_id<faction>):
// faction derives from faction_template and is what a save stores, and faction_id's is_valid()
// and obj() look up the game's live factions, which include factions made in play.  So the
// factory holds each template in one of these (NPC-J02).
class faction_template_entry
{
    public:
        string_id<faction_template_entry> id;
        bool was_loaded = false;
        faction_template templ;

        void load( const JsonObject &jsobj, std::string_view ) {
            templ.load_members( jsobj, was_loaded );
            templ.id = faction_id( id.str() );
        }
};

namespace
{
generic_factory<faction_template_entry> faction_template_factory( "faction" );

// "epilogues": a copy takes "extend", and "delete" with the snippet ids of the epilogues to remove
class faction_epilogue_reader : public generic_typed_reader<faction_epilogue_reader>
{
    public:
        faction_epilogue_data get_next( const JsonValue &jv ) const {
            faction_epilogue_data epi;
            epi.deserialize( jv.get_object() );
            return epi;
        }
        template<typename C>
        void erase_next( const JsonValue &jv, C &container ) const {
            const snippet_id epilogue( jv.get_string() );
            const auto removed = std::remove_if( container.begin(), container.end(),
            [&epilogue]( const faction_epilogue_data & epi ) {
                return epi.epilogue == epilogue;
            } );
            if( removed == container.end() ) {
                jv.throw_error( string_format( "no epilogue %s to delete", epilogue.str() ) );
            }
            container.erase( removed, container.end() );
        }
};
} // namespace

void faction_template::load( const JsonObject &jsobj, const std::string &src )
{
    // A later definition (another mod's) replaces the earlier one, and a "copy-from" naming a
    // faction defined later waits for it until finalize.
    faction_template_factory.load( jsobj, src );
}

void faction_template::finalize()
{
    faction_template_factory.finalize();
}

const faction_template *faction_template::find( const faction_id &id )
{
    const string_id<faction_template_entry> entry_id( id.str() );
    if( !faction_template_factory.is_valid( entry_id ) ) {
        return nullptr;
    }
    return &faction_template_factory.obj( entry_id ).templ;
}

void faction_template::check_consistency()
{
    for( const faction_template_entry &entry : faction_template_factory.get_all() ) {
        const faction_template &fac = entry.templ;
        for( const faction_epilogue_data &epi : fac.epilogue_data ) {
            if( !epi.epilogue.is_valid() ) {
                debugmsg( "There's no snippet with id %s", epi.epilogue.str() );
            }
        }
        if( find( fac.mutiny_faction ) == nullptr ) {
            debugmsg( "faction %s has mutiny_faction %s, which isn't a faction", fac.id.str(),
                      fac.mutiny_faction.str() );
        }
        for( const faction_price_rule &rule : fac.price_rules ) {
            rule.check( string_format( "faction %s price_rules", fac.id.str() ) );
        }
    }
}

void faction_template::reset()
{
    faction_template_factory.reset();
}

std::string faction_template::get_name() const
{
    return _( name );
}

void faction_template::set_name( std::string new_name )
{
    name = std::move( new_name );
}

// Adds or replaces the relation to each faction listed (also used by faction::deserialize)
void faction_template::load_relations( const JsonObject &jsobj )
{
    for( const JsonMember fac : jsobj.get_object( "relations" ) ) {
        JsonObject rel_jo = fac.get_object();
        std::bitset<static_cast<size_t>( npc_factions::relationship::rel_types )> fac_relation( 0 );
        for( const auto &rel_flag : npc_factions::relation_strs ) {
            fac_relation.set( static_cast<size_t>( rel_flag.second ),
                              rel_jo.get_bool( rel_flag.first, false ) );
        }
        relations[fac.name()] = fac_relation;
    }
}

// A copy's "extend": { "relations": { ... } } adds or replaces relations like "relations" does,
// and "delete": { "relations": [ ids ] } removes the relations to those factions.
void faction_template::change_relations( const JsonObject &jsobj, const bool was_loaded )
{
    if( !was_loaded ) {
        warn_disabled_feature( jsobj, "extend", "relations", "no copy-from" );
        warn_disabled_feature( jsobj, "delete", "relations", "no copy-from" );
        return;
    }
    if( jsobj.has_object( "extend" ) ) {
        JsonObject ext = jsobj.get_object( "extend" );
        ext.allow_omitted_members();
        if( ext.has_member( "relations" ) ) {
            load_relations( ext );
        }
    }
    if( jsobj.has_object( "delete" ) ) {
        JsonObject del = jsobj.get_object( "delete" );
        del.allow_omitted_members();
        if( del.has_member( "relations" ) ) {
            for( const std::string other : del.get_array( "relations" ) ) {
                if( relations.erase( other ) == 0 ) {
                    del.throw_error_at( "relations", string_format(
                                            "no relation to %s to delete", other ) );
                }
            }
        }
    }
}

double faction_price_rule::value_for( dbl_or_var const &value, item const &it, npc const &trader )
{
    if( value.is_constant() ) {
        return value.constant();
    }
    return value.evaluate( icg_entry::trade_dialogue( it, trader ) );
}

faction_price_rule faction_price_rules_reader::get_next( JsonValue &jv )
{
    JsonObject jo = jv.get_object();
    faction_price_rule ret( icg_entry_reader::_part_get_next( jo ) );
    optional( jo, false, "markup", ret.markup, 1.0 );
    optional( jo, false, "premium", ret.premium, 1.0 );
    optional( jo, false, "fixed_adj", ret.fixed_adj, std::nullopt );
    optional( jo, false, "price", ret.price, std::nullopt );
    return ret;
}

// With "copy-from" (was_loaded), it starts from that faction and changes only the members given.
void faction_template::load_members( const JsonObject &jsobj, const bool was_loaded )
{
    // members a full definition must give; a copy may change them, also with relative
    const auto required = [&]( const std::string_view member, auto & value ) {
        if( was_loaded ) {
            optional( jsobj, true, member, value );
        } else {
            mandatory( jsobj, false, member, value );
        }
    };
    required( "name", name );
    required( "likes_u", likes_u );
    required( "respects_u", respects_u );
    optional( jsobj, was_loaded, "trusts_u", trusts_u, 0 );
    required( "known_by_u", known_by_u );
    required( "size", size );
    required( "power", power );
    required( "wealth", wealth );
    required( "description", desc );
    optional( jsobj, was_loaded, "consumes_food", consumes_food, false );
    optional( jsobj, was_loaded, "price_rules", price_rules, faction_price_rules_reader {} );
    jsobj.read( "fac_food_supply", _food_supply, true );
    if( jsobj.has_string( "currency" ) ) {
        jsobj.read( "currency", currency, true );
        price_rules.emplace_back( currency, 1, 0 );
    } else if( !was_loaded ) {
        currency = itype_id::NULL_ID();
    }
    optional( jsobj, was_loaded, "lone_wolf_faction", lone_wolf_faction, false );
    optional( jsobj, was_loaded, "limited_area_claim", limited_area_claim, false );
    // a copy keeps the parent's relations to the factions it doesn't list
    load_relations( jsobj );
    change_relations( jsobj, was_loaded );
    optional( jsobj, was_loaded, "mon_faction", mon_faction, monfaction_human );
    optional( jsobj, was_loaded, "epilogues", epilogue_data, faction_epilogue_reader {} );
    optional( jsobj, was_loaded, "mutiny_faction", mutiny_faction, faction_amf );
}

std::string faction::describe() const
{
    std::string ret = desc.translated();
    return ret;
}

void faction_power_spec::deserialize( const JsonObject &jo )
{
    mandatory( jo, false, "faction", faction );
    optional( jo, false, "power_min", power_min );
    optional( jo, false, "power_max", power_max );

    if( !power_min.has_value() && !power_max.has_value() ) {
        jo.throw_error( "Must have either a power_min or a power_max" );
    }
}

void faction_epilogue_data::deserialize( const JsonObject &jo )
{
    optional( jo, false, "power_min", power_min );
    optional( jo, false, "power_max", power_max );
    optional( jo, false, "dynamic", dynamic_conditions );
    mandatory( jo, false, "id", epilogue );
}


bool faction::check_relations( const std::vector<faction_power_spec> &faction_power_specs ) const
{
    if( faction_power_specs.empty() ) {
        return true;
    }
    for( const faction_power_spec &spec : faction_power_specs ) {
        if( spec.power_min.has_value() ) {
            if( spec.faction->power < spec.power_min.value() ) {
                return false;
            }
        }
        if( spec.power_max.has_value() ) {
            if( spec.faction->power >= spec.power_max.value() ) {
                return false;
            }
        }
    }
    return true;
}


std::vector<std::string> faction::epilogue() const
{
    std::vector<std::string> ret;
    for( const faction_epilogue_data &epi : epilogue_data ) {
        if( ( !epi.power_min.has_value() || power >= epi.power_min ) && ( !epi.power_max.has_value() ||
                power < epi.power_max ) ) {
            if( check_relations( epi.dynamic_conditions ) ) {
                ret.emplace_back( epi.epilogue->translated() );
            }
        }
    }
    return ret;
}

void faction::add_to_membership( const character_id &guy_id, const std::string &guy_name,
                                 const bool known )
{
    members[guy_id] = std::make_pair( guy_name, known );
}

void faction::remove_member( const character_id &guy_id )
{
    for( auto it = members.cbegin(), next_it = it; it != members.cend(); it = next_it ) {
        ++next_it;
        if( guy_id == it->first ) {
            members.erase( it );
            break;
        }
    }
    if( members.empty() ) {
        // This is a templated base faction - don't delete it, just leave it as zero members for now.
        // Only want to delete dynamically created factions.
        if( faction_template::find( id ) != nullptr ) {
            return;
        }
        g->faction_manager_ptr->remove_faction( id );
    }
}

// Used in game.cpp
std::string fac_ranking_text( int val )
{
    if( val <= -100 ) {
        return _( "Archenemy" );
    }
    if( val <= -80 ) {
        return _( "Wanted Dead" );
    }
    if( val <= -60 ) {
        return _( "Enemy of the People" );
    }
    if( val <= -40 ) {
        return _( "Wanted Criminal" );
    }
    if( val <= -20 ) {
        return _( "Not Welcome" );
    }
    if( val <= -10 ) {
        return _( "Pariah" );
    }
    if( val <= -5 ) {
        return _( "Disliked" );
    }
    if( val >= 100 ) {
        return _( "Hero" );
    }
    if( val >= 80 ) {
        return _( "Idol" );
    }
    if( val >= 60 ) {
        return _( "Beloved" );
    }
    if( val >= 40 ) {
        return _( "Highly Valued" );
    }
    if( val >= 20 ) {
        return _( "Valued" );
    }
    if( val >= 10 ) {
        return _( "Well-Liked" );
    }
    if( val >= 5 ) {
        return _( "Liked" );
    }

    return _( "Neutral" );
}

// Used in game.cpp
std::string fac_respect_text( int val )
{
    // Respected, feared, etc.
    if( val >= 100 ) {
        return pgettext( "Faction respect", "Legendary" );
    }
    if( val >= 80 ) {
        return pgettext( "Faction respect", "Unchallenged" );
    }
    if( val >= 60 ) {
        return pgettext( "Faction respect", "Mighty" );
    }
    if( val >= 40 ) {
        return pgettext( "Faction respect", "Famous" );
    }
    if( val >= 20 ) {
        return pgettext( "Faction respect", "Well-Known" );
    }
    if( val >= 10 ) {
        return pgettext( "Faction respect", "Spoken Of" );
    }

    // Disrespected, laughed at, etc.
    if( val <= -100 ) {
        return pgettext( "Faction respect", "Worthless Scum" );
    }
    if( val <= -80 ) {
        return pgettext( "Faction respect", "Vermin" );
    }
    if( val <= -60 ) {
        return pgettext( "Faction respect", "Despicable" );
    }
    if( val <= -40 ) {
        return pgettext( "Faction respect", "Parasite" );
    }
    if( val <= -20 ) {
        return pgettext( "Faction respect", "Leech" );
    }
    if( val <= -10 ) {
        return pgettext( "Faction respect", "Laughingstock" );
    }

    return pgettext( "Faction respect", "Neutral" );
}

std::string fac_wealth_text( int val, int size )
{
    //Wealth per person
    val = val / size;
    if( val >= 1000000 ) {
        return pgettext( "Faction wealth", "Filthy rich" );
    }
    if( val >= 750000 ) {
        return pgettext( "Faction wealth", "Affluent" );
    }
    if( val >= 500000 ) {
        return pgettext( "Faction wealth", "Prosperous" );
    }
    if( val >= 250000 ) {
        return pgettext( "Faction wealth", "Well-Off" );
    }
    if( val >= 100000 ) {
        return pgettext( "Faction wealth", "Comfortable" );
    }
    if( val >= 85000 ) {
        return pgettext( "Faction wealth", "Wanting" );
    }
    if( val >= 70000 ) {
        return pgettext( "Faction wealth", "Failing" );
    }
    if( val >= 50000 ) {
        return pgettext( "Faction wealth", "Impoverished" );
    }
    return pgettext( "Faction wealth", "Destitute" );
}

// What one person eats in a day, as vitamin_stores() counts it
static constexpr int kcal_per_person_day = 3000;

// NPC-B12: how many days the food supply feeds the faction. This divided kcal by the old
// 288-a-day nutrition unit (about 10 times too many days) and by the JSON size, which is 1 for
// your followers whatever their number, and 0 for some factions.
static int food_supply_days( const faction &fac )
{
    // your followers and you, or as many as an NPC faction's size says
    const int eaters = fac.id == faction_your_followers ?
                       static_cast<int>( g->get_follower_list().size() ) + 1 : fac.size;
    return fac.food_supply().kcal() / ( std::max( eaters, 1 ) * kcal_per_person_day );
}

std::string faction::food_supply_text() const
{
    //Convert to how many days you can support the population
    const int val = food_supply_days( *this );
    if( val >= 30 ) {
        return pgettext( "Faction food", "Overflowing" );
    }
    if( val >= 14 ) {
        return pgettext( "Faction food", "Well-Stocked" );
    }
    if( val >= 6 ) {
        return pgettext( "Faction food", "Scrapping By" );
    }
    if( val >= 3 ) {
        return pgettext( "Faction food", "Malnourished" );
    }
    return pgettext( "Faction food", "Starving" );
}

bool faction::guaranteed_hostile() const
{
    return likes_u < -10;
}

bool faction::wont_talk_to_you() const
{
    return guaranteed_hostile() || trusts_u < 0;
}

nc_color faction::food_supply_color() const
{
    const int val = food_supply_days( *this );
    if( val >= 30 ) {
        return c_green;
    } else if( val >= 14 ) {
        return c_light_green;
    } else if( val >= 6 ) {
        return c_yellow;
    } else if( val >= 3 ) {
        return c_light_red;
    } else {
        return c_red;
    }
}

std::pair<nc_color, std::string> faction::vitamin_stores( vitamin_type vit_type )
{
    bool is_toxin = vit_type == vitamin_type::TOXIN;
    const double days_of_food = food_supply().kcal() / static_cast<double>( kcal_per_person_day );
    std::map<vitamin_id, int> stored_vits = food_supply().vitamins();
    // First, pare down our search to only the relevant type
    for( auto it = stored_vits.cbegin(); it != stored_vits.cend(); ) {
        if( it->first->type() != vit_type ) {
            it = stored_vits.erase( it );
        } else {
            ++it;
        }
    }
    if( stored_vits.empty() ) {
        return std::pair<nc_color, std::string>( !is_toxin ? c_red : c_green, _( "None present (NONE)" ) );
    }
    std::vector<std::pair<vitamin_id, double>> vitamins;
    // Iterate the map's content into a sortable container...
    for( auto &vit : stored_vits ) {
        int units_per_day = vit.first.obj().units_absorption_per_day();
        // with no food (or a vitamin absorbed at no rate) this was 0 / 0, which sorts badly
        double relative_intake = days_of_food > 0.0 && units_per_day > 0 ?
                                 static_cast<double>( vit.second ) / static_cast<double>
                                 ( units_per_day ) / days_of_food : 0.0;
        // We use the inverse value for toxins, since they are bad.
        if( is_toxin ) {
            relative_intake = relative_intake > 0.0 ? 1 / relative_intake :
                              std::numeric_limits<double>::max();
        }
        vitamins.emplace_back( vit.first, relative_intake );
    }
    // Sort to find the worst-case scenario, lowest relative_intake is first
    // NPC-B12: it sorted the highest first, so "Worst vitamin" showed the best one
    std::sort( vitamins.begin(), vitamins.end(), []( const auto & x, const auto & y ) {
        return x.second < y.second;
    } );
    const double worst_intake = vitamins.at( 0 ).second;
    std::string vit_name = vitamins.at( 0 ).first.obj().name();
    std::string msg = is_toxin ? _( "(TRACE)" ) : _( "(PLENTY)" );
    if( worst_intake <= 0.3 ) {
        msg = is_toxin ? _( "(POISON)" ) : _( "(LACK)" );
        return std::pair<nc_color, std::string>( c_red, string_format( _( "%1$s %2$s" ), vit_name,
                msg ) );
    }
    if( worst_intake <= 1.0 ) {
        msg = is_toxin ? _( "(DANGER)" ) : _( "(MEAGER)" );
        return std::pair<nc_color, std::string>( c_yellow, string_format( _( "%1$s %2$s" ), vit_name,
                msg ) );
    }
    return std::pair<nc_color, std::string>( c_green, string_format( _( "%1$s %2$s" ), vit_name,
            msg ) );
}

faction_price_rule const *faction::get_price_rules( item const &it, npc const &guy ) const
{
    auto const el = std::find_if(
    price_rules.crbegin(), price_rules.crend(), [&it, &guy]( faction_price_rule const & fc ) {
        return fc.matches( it, guy );
    } );
    if( el != price_rules.crend() ) {
        return &*el;
    }
    return nullptr;
}

bool faction::has_relationship( const faction_id &guy_id, npc_factions::relationship flag ) const
{
    for( const auto &rel_data : relations ) {
        if( rel_data.first == guy_id.c_str() ) {
            return rel_data.second.test( static_cast<size_t>( flag ) );
        }
    }
    return false;
}

std::string fac_combat_ability_text( int val )
{
    if( val >= 150 ) {
        return pgettext( "Faction combat lvl", "Legendary" );
    }
    if( val >= 130 ) {
        return pgettext( "Faction combat lvl", "Expert" );
    }
    if( val >= 115 ) {
        return pgettext( "Faction combat lvl", "Veteran" );
    }
    if( val >= 105 ) {
        return pgettext( "Faction combat lvl", "Skilled" );
    }
    if( val >= 95 ) {
        return pgettext( "Faction combat lvl", "Competent" );
    }
    if( val >= 85 ) {
        return pgettext( "Faction combat lvl", "Untrained" );
    }
    if( val >= 75 ) {
        return pgettext( "Faction combat lvl", "Crippled" );
    }
    if( val >= 50 ) {
        return pgettext( "Faction combat lvl", "Feeble" );
    }
    return pgettext( "Faction combat lvl", "Worthless" );
}

void npc_factions::finalize()
{
    faction_template::finalize();
    g->faction_manager_ptr->create_if_needed();
}

void faction_manager::clear()
{
    factions.clear();
}

void faction_manager::remove_faction( const faction_id &id )
{
    if( id.str().empty() || id == faction_no_faction ) {
        return;
    }
    for( auto it = factions.cbegin(), next_it = it; it != factions.cend(); it = next_it ) {
        ++next_it;
        if( id == it->first ) {
            factions.erase( it );
            break;
        }
    }
}

void faction_manager::create_if_needed()
{
    if( !factions.empty() ) {
        return;
    }
    for( const faction_template_entry &entry : faction_template_factory.get_all() ) {
        factions[entry.templ.id] = faction( entry.templ );
    }
}

faction *faction_manager::add_new_faction( const std::string &name_new, const faction_id &id_new,
        const faction_id &template_id )
{
    const faction_template *fac_temp = faction_template::find( template_id );
    if( fac_temp == nullptr ) {
        return nullptr;
    }
    faction fac( *fac_temp );
    fac.set_name( name_new );
    fac.id = id_new;
    factions[fac.id] = fac;
    return &factions[fac.id];
}

faction *faction_manager::get( const faction_id &id, const bool complain )
{
    if( id.is_null() ) {
        return get( faction_no_faction );
    }
    for( auto &elem : factions ) {
        if( elem.first == id ) {
            if( !elem.second.validated ) {
                if( const faction_template *fac_temp = faction_template::find( id ) ) {
                    elem.second.currency = fac_temp->currency;
                    elem.second.price_rules = fac_temp->price_rules;
                    elem.second.lone_wolf_faction = fac_temp->lone_wolf_faction;
                    elem.second.limited_area_claim = fac_temp->limited_area_claim;
                    elem.second.set_name( fac_temp->get_name() );
                    elem.second.desc = fac_temp->desc;
                    elem.second.mon_faction = fac_temp->mon_faction;
                    elem.second.epilogue_data = fac_temp->epilogue_data;
                    elem.second.mutiny_faction = fac_temp->mutiny_faction;
                    for( const auto &rel_data : fac_temp->relations ) {
                        if( elem.second.relations.find( rel_data.first ) == elem.second.relations.end() ) {
                            elem.second.relations[rel_data.first] = rel_data.second;
                        }
                    }
                }
                elem.second.validated = true;
            }
            return &elem.second;
        }
    }
    // id isn't already in factions map, so load in the template.
    if( const faction_template *elem = faction_template::find( id ) ) {
        factions[elem->id] = faction( *elem );
        if( !factions.empty() ) {
            factions[elem->id].validated = true;
        }
        return &factions[elem->id];
    }
    // Sometimes we add new IDs to the map, sometimes we want to check if its already there.
    if( complain ) {
        debugmsg( "Requested non-existing faction '%s'", id.str() );
    }
    return nullptr;
}

template<>
bool string_id<faction>::is_valid() const
{
    return g->faction_manager_ptr->get( *this, false ) != nullptr;
}
