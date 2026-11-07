#include "scenario.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <map>
#include <ostream>
#include <unordered_set>

#include "achievement.h"
#include "debug.h"
#include "effect_on_condition.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "localized_comparator.h"
#include "mission.h"
#include "mutation.h"
#include "options.h"
#include "past_achievements_info.h"
#include "profession.h"
#include "rng.h"
#include "start_location.h"
#include "string_formatter.h"
#include "translations.h"

namespace
{
generic_factory<scenario> all_scenarios( "scenario" );
} // namespace

/** @relates string_id */
template<>
const scenario &string_id<scenario>::obj() const
{
    return all_scenarios.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<scenario>::is_valid() const
{
    return all_scenarios.is_valid( *this );
}

static scen_blacklist sc_blacklist;

namespace
{
// A forced trait is a trait id or { "trait": id, "variant": variant id } (PROG-J05)
class forced_trait_reader : public generic_typed_reader<forced_trait_reader>
{
    public:
        explicit forced_trait_reader( std::map<trait_id, std::string> *variants ) :
            variants( variants ) {}
        trait_id get_next( const JsonValue &jv ) const {
            trait_and_var tv;
            tv.deserialize( jv );
            if( !tv.variant.empty() ) {
                ( *variants )[tv.trait] = tv.variant;
            }
            return tv.trait;
        }
    private:
        std::map<trait_id, std::string> *variants;
};

// What "extend" and "delete" can change in a scenario; anything else in them did nothing
// (PROG-J05)
const std::set<std::string> &scenario_changeable_members()
{
    static const std::set<std::string> members = {
        "add_professions", "allowed_locs", "blacklist_professions", "eoc", "flags",
        "forbidden_traits", "forced_traits", "hobbies", "missions", "professions",
        "surround_groups", "traits"
    };
    return members;
}
} // namespace

scenario::scenario()
// NOLINTNEXTLINE(cata-static-string_id-constants)
    : id( "" ), _name_male( no_translation( "null" ) ),
      _name_female( no_translation( "null" ) ),
      _description_male( no_translation( "null" ) ),
      _description_female( no_translation( "null" ) )
{
}

void scenario::load_scenario( const JsonObject &jo, const std::string &src )
{
    all_scenarios.load( jo, src );
}

void scenario::load( const JsonObject &jo, std::string_view )
{
    // TODO: pretty much the same as in profession::load, but different contexts for pgettext.
    // TODO: maybe combine somehow?
    if( !was_loaded || jo.has_string( "name" ) ) {
        // These may differ depending on the language settings!
        const std::string name = jo.get_string( "name" );
        _name_female = to_translation( "scenario_female", name );
        _name_male = to_translation( "scenario_male", name );
    }

    if( !was_loaded || jo.has_string( "description" ) ) {
        // These also may differ depending on the language settings!
        std::string desc;
        mandatory( jo, false, "description", desc, text_style_check_reader() );
        _description_male = to_translation( "scen_desc_male", desc );
        _description_female = to_translation( "scen_desc_female", desc );
    }

    if( !was_loaded || jo.has_string( "start_name" ) ) {
        // Specifying translation context here and above to avoid adding unnecessary json code for every scenario
        // NOLINTNEXTLINE(cata-json-translation-input)
        _start_name = to_translation( "start_name", jo.get_string( "start_name" ) );
    }

    mandatory( jo, was_loaded, "points", _point_cost );

    optional( jo, was_loaded, "blacklist_professions", blacklist );
    optional( jo, was_loaded, "add_professions", extra_professions );
    optional( jo, was_loaded, "professions", professions, string_id_reader<::profession> {} );

    optional( jo, was_loaded, "hobbies", hobby_exclusion );
    optional( jo, was_loaded, "whitelist_hobbies", hobbies_whitelist, true );

    for( const char *change : {
             "extend", "delete"
         } ) {
        if( jo.has_object( change ) ) {
            JsonObject changes = jo.get_object( change );
            changes.allow_omitted_members();
            for( const JsonMember member : changes ) {
                if( scenario_changeable_members().count( member.name() ) == 0 ) {
                    changes.throw_error_at( member.name(), string_format(
                                                "\"%s\" doesn't take \"%s\" in a scenario", member.name(), change ) );
                }
            }
        }
    }

    optional( jo, was_loaded, "traits", _allowed_traits, string_id_reader<::mutation_branch> {} );
    if( jo.has_member( "forced_traits" ) ) {
        _forced_trait_variants.clear();
    }
    optional( jo, was_loaded, "forced_traits", _forced_traits,
              forced_trait_reader( &_forced_trait_variants ) );
    for( auto it = _forced_trait_variants.begin(); it != _forced_trait_variants.end(); ) {
        it = _forced_traits.count( it->first ) ? std::next( it ) : _forced_trait_variants.erase( it );
    }
    optional( jo, was_loaded, "forbidden_traits", _forbidden_traits,
              string_id_reader<::mutation_branch> {} );
    optional( jo, was_loaded, "allowed_locs", _allowed_locs, string_id_reader<::start_location> {} );
    if( _allowed_locs.empty() ) {
        jo.throw_error( "at least one starting location (member \"allowed_locs\") must be defined" );
    }
    optional( jo, was_loaded, "flags", flags, auto_flags_reader<> {} );
    optional( jo, was_loaded, "default_backgrounds", _default_backgrounds );
    optional( jo, was_loaded, "map_extra", _map_extra, map_extra_id::NULL_ID() );
    optional( jo, was_loaded, "missions", _missions, string_id_reader<::mission_type> {} );

    optional( jo, was_loaded, "requirement", _requirement );
    optional( jo, was_loaded, "hard_requirement", hard_requirement, false );

    if( hard_requirement && !_requirement ) {
        jo.throw_error_at( "hard_requirement",
                           "Cannot have hard requirement when object has no requirements" );
    }

    optional( jo, was_loaded, "reveal_locale", reveal_locale, true );
    optional( jo, was_loaded, "distance_initial_visibility", distance_initial_visibility, 15 );

    optional( jo, was_loaded, "eoc", _eoc, auto_flags_reader<effect_on_condition_id> {} );

    optional( jo, was_loaded, "origin_offset", origin_offset, point_rel_om::zero );

    // A copy keeps the parts of the dates it doesn't name, and can change the others (PROG-J07)
    if( !was_loaded ) {
        // The cataclysm started 5 days before game start
        _cataclysm_date = start_date{ 0, ( 1 + get_option<int>( "SEASON_LENGTH" ) / 3 * 2 ) - 5, SPRING, 1 };
        _game_date = start_date{ 8, 1 + get_option<int>( "SEASON_LENGTH" ) / 3 * 2, SPRING, 1 };
    }
    const auto read_date = [&jo]( const std::string_view member, start_date & date ) {
        if( jo.has_member( member ) ) {
            JsonObject jocid = jo.get_member( member );
            optional( jocid, true, "hour", date.hour );
            optional( jocid, true, "day", date.day );
            optional( jocid, true, "season", date.season );
            optional( jocid, true, "year", date.year );
        }
    };
    read_date( "start_of_cataclysm", _cataclysm_date );
    read_date( "start_of_game", _game_date );
    const auto date_time = []( const start_date & date ) {
        return calendar::turn_zero +
               1_hours * date.hour +
               1_days * ( date.day - 1 ) +
               1_days * get_option<int>( "SEASON_LENGTH" ) * date.season +
               calendar::year_length() * ( date.year - 1 );
    };
    _default_start_of_cataclysm = date_time( _cataclysm_date );
    _default_start_of_game = date_time( _game_date );
    reset_calendar();

    if( jo.has_string( "vehicle" ) ) {
        _starting_vehicle = vproto_id( jo.get_string( "vehicle" ) );
    }

    // Given, the list replaces a copied one; "extend" adds to it and "delete" removes groups
    // (PROG-J07)
    const auto add_groups = [this]( const JsonArray & groups ) {
        for( JsonArray ja : groups ) {
            _surround_groups.emplace_back( mongroup_id( ja.get_string( 0 ) ),
                                           static_cast<float>( ja.get_float( 1 ) ) );
        }
    };
    if( jo.has_member( "surround_groups" ) ) {
        _surround_groups.clear();
        add_groups( jo.get_array( "surround_groups" ) );
    }
    if( !was_loaded ) {
        warn_disabled_feature( jo, "extend", "surround_groups", "no copy-from" );
        warn_disabled_feature( jo, "delete", "surround_groups", "no copy-from" );
    } else {
        if( jo.has_object( "delete" ) ) {
            JsonObject del = jo.get_object( "delete" );
            del.allow_omitted_members();
            for( JsonValue jv : del.get_array( "surround_groups" ) ) {
                // a group id, or [ id, weight ]
                const mongroup_id group( jv.test_array() ? jv.get_array().get_string( 0 ) : jv.get_string() );
                const auto removed = std::remove_if( _surround_groups.begin(), _surround_groups.end(),
                [&group]( const std::pair<mongroup_id, float> &entry ) {
                    return entry.first == group;
                } );
                if( removed == _surround_groups.end() ) {
                    jv.throw_error( string_format( "no surround group %s to delete", group.str() ) );
                }
                _surround_groups.erase( removed, _surround_groups.end() );
            }
        }
        if( jo.has_object( "extend" ) ) {
            JsonObject ext = jo.get_object( "extend" );
            ext.allow_omitted_members();
            add_groups( ext.get_array( "surround_groups" ) );
        }
    }

    if( jo.has_object( "starting_npc" ) ) {
        scenario_starting_npc npc_def;
        npc_def.deserialize( jo.get_object( "starting_npc" ) );
        _starting_npc = npc_def;
    }
}

void scenario_starting_npc::deserialize( const JsonObject &jo )
{
    if( jo.has_array( "classes" ) ) {
        for( JsonArray entry : jo.get_array( "classes" ) ) {
            classes.add( npc_class_id( entry.get_string( 0 ) ), entry.get_int( 1 ) );
        }
    }
    optional( jo, false, "chat", chat );
    if( jo.has_array( "eocs" ) ) {
        for( JsonValue jv : jo.get_array( "eocs" ) ) {
            eocs.push_back( effect_on_conditions::load_inline_eoc( jv, "" ) );
        }
    }
}

const scenario *scenario::generic()
{
    static const string_id<scenario> generic_scenario_id(
        get_option<std::string>( "GENERIC_SCENARIO_ID" ) );

    std::vector<const scenario *> all;
    for( const scenario &scen : scenario::get_all() ) {
        if( scen.scen_is_blacklisted() ) {
            continue;
        }
        all.push_back( &scen );
    }
    if( find_if( all.begin(), all.end(), []( const scenario * s ) {
    return s->ident() == generic_scenario_id;
    } ) != all.end() ) {
        // if the default scenario exists return it
        return &generic_scenario_id.obj();
    }

    // if generic doesn't exist just return to the first scenario
    return *all.begin();

}

// Strategy: a third of the time, return the generic scenario.  Otherwise, return a scenario,
// weighting 0 cost scenario more likely--the weight of a scenario with cost n is 2/(|n|+2),
// e.g., cost 1 is 2/3rds as likely, cost -2 is 1/2 as likely.
const scenario *scenario::weighted_random()
{
    if( one_in( 3 ) ) {
        return generic();
    }

    const auto &list = all_scenarios.get_all();
    while( true ) {
        const scenario &scen = random_entry_ref( list );

        if( x_in_y( 2, std::abs( scen.point_cost() ) + 2 ) ) {
            return &scen;
        }
        // else reroll in the while loop.
    }
}

const std::vector<scenario> &scenario::get_all()
{
    return all_scenarios.get_all();
}

void scenario::reset()
{
    all_scenarios.reset();
}

void scenario::check_all()
{
    all_scenarios.check();
}

void scenario::finalize_all()
{
    all_scenarios.finalize();
    sc_blacklist.finalize();
}

static void check_traits( const std::set<trait_id> &traits, const string_id<scenario> &ident )
{
    for( const auto &t : traits ) {
        if( !t.is_valid() ) {
            debugmsg( "trait %s for scenario %s does not exist", t.c_str(), ident.c_str() );
        }
    }
}

void scenario::check() const
{
    for( const auto &p : professions ) {
        if( !p.is_valid() ) {
            debugmsg( "profession %s for scenario %s does not exist", p.c_str(), id.c_str() );
        }
    }

    for( const string_id<profession> &hobby : hobby_exclusion ) {
        if( !hobby.is_valid() ) {
            debugmsg( "hobby %s for scenario %s does not exist", hobby.str(), id.str() );
        } else if( !hobby->is_hobby() ) {
            debugmsg( "hobby %s for scenario %s is a profession", hobby.str(), id.str() );
        }
    }

    std::unordered_set<string_id<profession>> professions_set;
    for( const auto &p : professions ) {
        if( professions_set.count( p ) == 1 ) {
            debugmsg( "Duplicate profession %s in scenario %s.", p.c_str(), this->id.c_str() );
        } else {
            professions_set.insert( p );
        }
    }

    for( const start_location_id &l : _allowed_locs ) {
        if( !l.is_valid() ) {
            debugmsg( "starting location %s for scenario %s does not exist", l.c_str(), id.c_str() );
        }
    }

    if( _default_backgrounds && !_default_backgrounds->is_empty() &&
        !_default_backgrounds->is_valid() ) {
        debugmsg( "default_backgrounds %s for scenario %s is not a profession group",
                  _default_backgrounds->str(), id.str() );
    }

    if( _starting_npc ) {
        for( const std::pair<npc_class_id, int> &cls : _starting_npc->classes ) {
            if( !cls.first.is_valid() ) {
                debugmsg( "starting_npc class %s for scenario %s does not exist", cls.first.str(), id.str() );
            }
        }
        for( const effect_on_condition_id &eoc : _starting_npc->eocs ) {
            if( !eoc.is_valid() ) {
                debugmsg( "starting_npc eoc %s for scenario %s does not exist", eoc.str(), id.str() );
            }
        }
    }

    if( blacklist ) {
        if( professions.empty() ) {
            debugmsg( "Scenario %s: Use an empty whitelist to whitelist everything.", id.c_str() );
        } else {
            permitted_professions(); // Debug msg if every profession is blacklisted
        }
    }

    check_traits( _allowed_traits, id );
    check_traits( _forced_traits, id );
    check_traits( _forbidden_traits, id );
    for( const std::pair<const trait_id, std::string> &tv : _forced_trait_variants ) {
        if( tv.first.is_valid() && tv.first->variant( tv.second ) == nullptr ) {
            debugmsg( "scenario %s forces trait %s with variant %s, which it doesn't have", id.str(),
                      tv.first.str(), tv.second );
        }
    }

    if( !_map_extra.is_valid() )  {
        debugmsg( "there is no map extra with id %s", _map_extra.str() );
    }

    for( const auto &e : eoc() ) {
        if( !e.is_valid() ) {
            debugmsg( "effect on condition %s for scenario %s does not exist", e.c_str(), id.c_str() );
        }
    }

    for( const auto &m : _missions ) {
        if( !m.is_valid() ) {
            debugmsg( "starting mission %s for scenario %s does not exist", m.c_str(), id.c_str() );
        }

        if( std::find( m->origins.begin(), m->origins.end(), ORIGIN_GAME_START ) == m->origins.end() ) {
            debugmsg( "starting mission %s for scenario %s must include an origin of ORIGIN_GAME_START",
                      m.c_str(), id.c_str() );
        }
    }

    if( _starting_vehicle && !_starting_vehicle.is_valid() ) {
        debugmsg( "vehicle prototype %s for profession %s does not exist", _starting_vehicle.c_str(),
                  id.c_str() );
    }
}

const string_id<scenario> &scenario::ident() const
{
    return id;
}

std::string scenario::gender_appropriate_name( bool male ) const
{
    if( male ) {
        return _name_male.translated();
    } else {
        return _name_female.translated();
    }
}

std::string scenario::description( bool male ) const
{
    if( male ) {
        return _description_male.translated();
    } else {
        return _description_female.translated();
    }
}

signed int scenario::point_cost() const
{
    return _point_cost;
}

start_location_id scenario::start_location() const
{
    return _allowed_locs.front();
}
start_location_id scenario::random_start_location() const
{
    return random_entry( _allowed_locs );
}

bool scenario::scen_is_blacklisted() const
{
    return sc_blacklist.scenarios.count( id ) != 0;
}

void scen_blacklist::load_scen_blacklist( const JsonObject &jo, std::string_view src )
{
    sc_blacklist.load( jo, src );
}

void scen_blacklist::load( const JsonObject &jo, std::string_view )
{
    const std::string bl_stype = jo.get_string( "subtype" );
    std::set<string_id<scenario>> listed;
    for( const std::string line : jo.get_array( "scenarios" ) ) {
        listed.emplace( line );
    }

    if( bl_stype == "whitelist" ) {
        // A later whitelist replaces an earlier one
        whitelist = true;
        whitelisted = std::move( listed );
    } else if( bl_stype == "blacklist" ) {
        // Blacklists add up
        blacklisted.insert( listed.begin(), listed.end() );
    } else {
        jo.throw_error( "Blacklist subtype is not a valid subtype." );
    }
}

void scen_blacklist::finalize()
{
    std::vector<string_id<scenario>> all_scens;
    std::vector<scenario> all_scenarios = scenario::get_all();
    all_scens.reserve( all_scenarios.size() );
    for( const scenario &scen : all_scenarios ) {
        all_scens.emplace_back( scen.ident() );
    }
    for( const std::set<string_id<scenario>> *listed : {
             &blacklisted, &whitelisted
         } ) {
        for( const string_id<scenario> &sc : *listed ) {
            if( std::find( all_scens.begin(), all_scens.end(), sc ) == all_scens.end() ) {
                debugmsg( "Scenario blacklist contains invalid scenario %s", sc.str() );
            }
        }
    }

    scenarios.clear();
    if( whitelist ) {
        // Everything the whitelist doesn't list; a blacklist can't hide what it lists
        for( const string_id<scenario> &sc : all_scens ) {
            if( whitelisted.count( sc ) == 0 ) {
                scenarios.insert( sc );
            }
        }
    } else {
        scenarios = blacklisted;
    }
}

void reset_scenarios_blacklist()
{
    sc_blacklist.scenarios.clear();
    sc_blacklist.whitelist = false;
    sc_blacklist.blacklisted.clear();
    sc_blacklist.whitelisted.clear();
}

// The permitted lists are cached for the avatar, and the chargen_allow_npc filter is applied
// on top of them per call: a single cache filled by whichever kind of caller came first used
// to decide the list for the other.
static std::vector<string_id<profession>> filter_for_npcs(
        const std::vector<string_id<profession>> &list )
{
    std::vector<string_id<profession>> res;
    for( const string_id<profession> &p : list ) {
        if( p->chargen_allow_npc() ) {
            res.push_back( p );
        }
    }
    return res;
}

std::vector<string_id<profession>> scenario::permitted_professions( bool is_npc ) const
{
    if( is_npc ) {
        std::vector<string_id<profession>> res = filter_for_npcs( permitted_professions( false ) );
        if( res.empty() ) {
            res.push_back( profession::generic()->ident() );
        }
        return res;
    }

    if( !cached_permitted_professions.empty() ) {
        return cached_permitted_professions;
    }

    const std::vector<profession> &all = profession::get_all();
    std::vector<string_id<profession>> &res = cached_permitted_professions;
    for( const profession &p : all ) {
        if( p.is_hobby() || p.is_blacklisted() ) {
            continue;
        }
        const bool present = std::find( professions.begin(), professions.end(),
                                        p.ident() ) != professions.end();

        bool conflicting_traits = scenario_traits_conflict_with_profession_traits( p );

        if( blacklist || professions.empty() ) {
            if( !present && !p.has_flag( "SCEN_ONLY" ) && !conflicting_traits ) {
                res.push_back( p.ident() );
            }
        } else if( present ) {
            if( !conflicting_traits ) {
                res.push_back( p.ident() );
            } else {
                debugmsg( "Scenario %s and profession %s have conflicting trait requirements",
                          id.c_str(), p.ident().c_str() );
            }
        } else if( extra_professions ) {
            if( !p.has_flag( "SCEN_ONLY" ) && !conflicting_traits ) {
                res.push_back( p.ident() );
            }
        }
    }

    if( res.empty() ) {
        debugmsg( "Why would you blacklist every profession?" );
        res.push_back( profession::generic()->ident() );
    }
    return res;
}

std::vector<string_id<profession>> scenario::permitted_hobbies( bool is_npc ) const
{
    if( is_npc ) {
        return filter_for_npcs( permitted_hobbies( false ) );
    }

    if( !cached_permitted_hobbies.empty() ) {
        return cached_permitted_hobbies;
    }

    std::vector<string_id<profession>> all = profession::get_all_hobbies();
    std::vector<string_id<profession>> &res = cached_permitted_hobbies;
    for( const string_id<profession> &hobby : all ) {
        if( hobby->is_blacklisted() ) {
            continue;
        }
        if( scenario_traits_conflict_with_profession_traits( *hobby ) ) {
            continue;
        }
        if( !hobbies_whitelist && hobby_exclusion.count( hobby ) != 0 ) {
            continue;
        }
        if( hobbies_whitelist && !hobby_exclusion.empty() && hobby_exclusion.count( hobby ) == 0 ) {
            continue;
        }

        res.push_back( hobby );
    }

    if( res.empty() ) {
        debugmsg( "Why would you blacklist every hobby?" );
        res.insert( res.end(), all.begin(), all.end() );
    }

    return res;
}

bool scenario::scenario_traits_conflict_with_profession_traits( const profession &p ) const
{
    for( const auto &pt : p.get_forbidden_traits() ) {
        if( is_locked_trait( pt ) ) {
            return true;
        }
    }

    for( trait_and_var &pt : p.get_locked_traits() ) {
        if( is_forbidden_trait( pt.trait ) ) {
            return true;
        }
    }

    //  check if:
    //  locked traits for scenario prevent taking locked traits for professions
    //  locked traits for professions prevent taking locked traits for scenario
    for( const trait_id &st : get_locked_traits() ) {
        for( trait_and_var &pt : p.get_locked_traits() ) {
            if( are_conflicting_traits( st, pt.trait ) || are_conflicting_traits( pt.trait, st ) ) {
                return true;
            }
        }
    }
    return false;
}

bool scenario::has_hard_requirement() const
{
    return hard_requirement;
}

const profession *scenario::weighted_random_profession( bool is_npc ) const
{
    // Strategy: 1/3 of the time, return the generic profession (if it's permitted).
    // Otherwise, the weight of each permitted profession is 2 / ( |point cost| + 2 )
    const auto choices = permitted_professions( is_npc );
    if( one_in( 3 ) && choices.front() == profession::generic()->ident() ) {
        return profession::generic();
    }

    while( true ) {
        const string_id<profession> &candidate = random_entry_ref( choices );
        if( candidate->can_pick().success() && x_in_y( 2, 2 + std::abs( candidate->point_cost() ) ) ) {
            return &candidate.obj();
        }
    }
    return profession::generic(); // Suppress warnings
}

std::string scenario::prof_count_str() const
{
    if( professions.empty() ) {
        return _( "All" );
    }
    return blacklist ? _( "Almost all" ) : _( "Limited" );
}

std::string scenario::start_name() const
{
    return _start_name.translated();
}

int scenario::start_location_count() const
{
    return _allowed_locs.size();
}

int scenario::start_location_targets_count() const
{
    int cnt = 0;
    for( const auto &sloc : _allowed_locs ) {
        cnt += sloc.obj().targets_count();
    }
    return cnt;
}

std::optional<achievement_id> scenario::get_requirement() const
{
    return _requirement;
}

bool scenario::get_reveal_locale() const
{
    return reveal_locale;
}

int scenario::get_distance_initial_visibility() const
{
    return distance_initial_visibility;
}

void scenario::normalize_calendar() const
{
    scenario *hack = const_cast<scenario *>( this );
    // We don't currently allow to start game before cataclysm
    if( hack->_default_start_of_game < hack->_default_start_of_cataclysm ) {
        hack->_default_start_of_game = hack->_default_start_of_cataclysm;
    }
    if( hack->_start_of_game < hack->_start_of_cataclysm ) {
        hack->_start_of_game = hack->_start_of_cataclysm;
    }
}

void scenario::reset_calendar() const
{
    scenario *hack = const_cast<scenario *>( this );
    hack->_start_of_cataclysm = _default_start_of_cataclysm;
    hack->_start_of_game = _default_start_of_game;
    hack->normalize_calendar();
}

void scenario::change_start_of_cataclysm( const time_point &t ) const
{
    scenario *hack = const_cast<scenario *>( this );
    hack->_start_of_cataclysm = t;
    hack->normalize_calendar();
}

void scenario::change_start_of_game( const time_point &t ) const
{
    scenario *hack = const_cast<scenario *>( this );
    hack->_start_of_game = t;
    hack->normalize_calendar();
}

time_point scenario::start_of_cataclysm() const
{
    return _start_of_cataclysm;
}

time_point scenario::start_of_game() const
{
    return _start_of_game;
}

vproto_id scenario::vehicle() const
{
    return _starting_vehicle;
}

bool scenario::traitquery( const trait_id &trait ) const
{
    return _allowed_traits.count( trait ) != 0 || is_locked_trait( trait ) ||
           ( !is_forbidden_trait( trait ) && trait->startingtrait );
}

std::set<trait_id> scenario::get_locked_traits() const
{
    return _forced_traits;
}

std::string scenario::forced_trait_variant( const trait_id &trait ) const
{
    const auto it = _forced_trait_variants.find( trait );
    return it == _forced_trait_variants.end() ? std::string() : it->second;
}

bool scenario::is_locked_trait( const trait_id &trait ) const
{
    return _forced_traits.count( trait ) != 0;
}

bool scenario::is_forbidden_trait( const trait_id &trait ) const
{
    return _forbidden_traits.count( trait ) != 0;
}

bool scenario::has_flag( const std::string &flag ) const
{
    return flags.count( flag ) != 0;
}

const std::optional<profession_group_id> &scenario::default_backgrounds() const
{
    return _default_backgrounds;
}

bool scenario::allowed_start( const start_location_id &loc ) const
{
    const auto &vec = _allowed_locs;
    return std::find( vec.begin(), vec.end(), loc ) != vec.end();
}

ret_val<void> scenario::can_afford( const scenario &current_scenario, const int points ) const
{
    if( point_cost() - current_scenario.point_cost() <= points ) {
        return ret_val<void>::make_success();

    }

    return ret_val<void>::make_failure( _( "You don't have enough points" ) );
}

ret_val<void> scenario::can_pick() const
{
    const bool meta_progression = get_option<bool>( "META_PROGRESS" );
    // if meta progression is disabled then skip this
    if( get_past_achievements().unlocks_all_content() ||
        ( !meta_progression && !has_hard_requirement() ) ) {
        return ret_val<void>::make_success();
    }

    if( _requirement ) {
        const bool has_req = get_past_achievements().is_completed(
                                 _requirement.value()->id );
        std::string fail_msg = _( "You must complete the achievement \"%s\" to unlock this scenario." );
        if( !meta_progression ) {
            fail_msg += _( "\nThis scenario can only be unlocked through achievements." );
        }
        if( !has_req ) {
            return ret_val<void>::make_failure(
                       fail_msg,
                       _requirement.value()->name() );
        }
    }

    return ret_val<void>::make_success();
}

bool scenario_sorter::operator()( const scenario *a, const scenario *b ) const
{
    if( cities_enabled ) {
        // The generic ("Unemployed") profession should be listed first.
        const scenario *gen = scenario::generic();
        if( b == gen ) {
            return false;
        } else if( a == gen ) {
            return true;
        }
    }

    if( !a->can_pick().success() && b->can_pick().success() ) {
        return false;
    }

    if( a->can_pick().success() && !b->can_pick().success() ) {
        return true;
    }

    if( !cities_enabled && a->has_flag( "CITY_START" ) != b->has_flag( "CITY_START" ) ) {
        return a->has_flag( "CITY_START" ) < b->has_flag( "CITY_START" );
    } else {
        return localized_compare( a->gender_appropriate_name( male ),
                                  b->gender_appropriate_name( male ) );
    }
}

bool scenario::has_map_extra() const
{
    return !_map_extra.is_null();
}
const map_extra_id &scenario::get_map_extra() const
{
    return _map_extra;
}
const point_rel_om &scenario::get_origin_offset() const
{
    return origin_offset;
}
const std::vector<mission_type_id> &scenario::missions() const
{
    return _missions;
}
const std::vector<effect_on_condition_id> &scenario::eoc() const
{
    return _eoc;
}
const std::optional<scenario_starting_npc> &scenario::starting_npc() const
{
    return _starting_npc;
}

const std::vector<std::pair<mongroup_id, float>> &scenario::surround_groups() const
{
    return _surround_groups;
}
// vim:ts=4:sw=4:et:tw=0:fdm=marker:
