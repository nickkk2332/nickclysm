#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_utility.h"
#include "character.h"
#include "condition.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "dialogue.h"
#include "display.h"
#include "effect_on_condition.h"
#include "enum_conversions.h"
#include "faction_camp.h"
#include "flexbuffer_json.h"
#include "game_constants.h"
#include "generic_factory.h"
#include "item_group.h"
#include "mapdata.h"
#include "mission_companion.h"
#include "omdata.h"
#include "overmapbuffer.h"
#include "regional_settings.h"
#include "skill.h"
#include "string_formatter.h"
#include "talker.h"
#include "translation.h"
#include "translations.h"
#include "type_id.h"

static const skill_id skill_speech( "speech" );
static const skill_id skill_survival( "survival" );

static std::map<risk_diff_level, translation> risk_diff_to_risk_name( {
    { risk_diff_level::NONE,                 to_translation( "risk level", "Risk: None" ) },
    { risk_diff_level::VERY_LOW,             to_translation( "risk level", "Risk: Very Low" ) },
    { risk_diff_level::LOW,                  to_translation( "risk level", "Risk: Low" ) },
    { risk_diff_level::MEDIUM,               to_translation( "risk level", "Risk: Medium" ) },
    { risk_diff_level::HIGH,                 to_translation( "risk level", "Risk: High" ) },
    { risk_diff_level::VERY_HIGH,            to_translation( "risk level", "Risk: Very High " ) }
} );

static std::map<risk_diff_level, translation> risk_diff_to_diff_name( {
    { risk_diff_level::NONE,                 to_translation( "difficulty level", "Difficulty: None" ) },
    { risk_diff_level::VERY_LOW,             to_translation( "difficulty level", "Difficulty: Very Easy" ) },
    { risk_diff_level::LOW,                  to_translation( "difficulty level", "Difficulty: Easy" ) },
    { risk_diff_level::MEDIUM,               to_translation( "difficulty level", "Difficulty: Moderate" ) },
    { risk_diff_level::HIGH,                 to_translation( "difficulty level", "Difficulty: Hard" ) },
    { risk_diff_level::VERY_HIGH,            to_translation( "difficulty level", "Difficulty: Very Hard" ) }
} );

namespace
{

// the behaviours a faction_mission can select, by their JSON name
const std::array<std::pair<std::string_view, mission_kind>, 14> camp_mission_behaviors = { {
        { "generic", Camp_Generic },
        { "menial", Camp_Menial },
        { "survey_field", Camp_Survey_Field },
        { "survey_expansion", Camp_Survey_Expansion },
        { "cut_logs", Camp_Cut_Logs },
        { "clearcut", Camp_Clearcut },
        { "setup_hide_site", Camp_Setup_Hide_Site },
        { "relay_hide_site", Camp_Relay_Hide_Site },
        { "recruiting", Camp_Recruiting },
        { "scouting", Camp_Scouting },
        { "combat_patrol", Camp_Combat_Patrol },
        { "plow", Camp_Plow },
        { "plant", Camp_Plant },
        { "harvest", Camp_Harvest }
    }
};

// behaviours whose companion is out for the mission's "duration"
bool uses_duration( const mission_kind behavior )
{
    return behavior == Camp_Generic || behavior == Camp_Menial || behavior == Camp_Survey_Field ||
           behavior == Camp_Survey_Expansion || behavior == Camp_Recruiting;
}

void read_camp_mission_eocs( const JsonObject &jo, const std::string_view member,
                             const bool was_loaded, const std::string_view src,
                             std::vector<effect_on_condition_id> &eocs )
{
    if( !jo.has_member( member ) ) {
        if( !was_loaded ) {
            eocs.clear();
        }
        return;
    }
    eocs.clear();
    for( const JsonValue jv : jo.get_array( member ) ) {
        eocs.push_back( effect_on_conditions::load_inline_eoc( jv, src ) );
    }
}

} // namespace

void camp_provide_requirement::deserialize( const JsonValue &jv )
{
    if( jv.test_string() ) {
        id = jv.get_string();
        amount = 1;
        return;
    }
    const JsonObject jo = jv.get_object();
    mandatory( jo, false, "id", id );
    optional( jo, false, "amount", amount, 1 );
}

void camp_mission_results::deserialize( const JsonObject &jo )
{
    optional( jo, false, "item_group", item_group, item_group_id::NULL_ID() );
    seasonal_item_groups.clear();
    if( jo.has_object( "seasonal_item_groups" ) ) {
        for( const JsonMember member : jo.get_object( "seasonal_item_groups" ) ) {
            const std::optional<season_type> season =
                io::string_to_enum_optional<season_type>( member.name() );
            if( !season ) {
                member.throw_error( string_format( "%s isn't a season (spring, summer, autumn "
                                                   "or winter)", member.name() ) );
            }
            seasonal_item_groups[*season] = item_group_id( member.get_string() );
        }
    }
    optional( jo, false, "monster_groups", monster_groups );
    optional( jo, false, "attempts", attempts, 1 );
    optional( jo, false, "difficulty", difficulty, 0 );
    if( jo.has_object( "skill_score" ) ) {
        skill_score.deserialize( jo.get_object( "skill_score" ) );
    } else {
        skill_score = camp_skill_score();
    }
}

void camp_skill_score::deserialize( const JsonObject &jo )
{
    skills.clear();
    if( jo.has_object( "skills" ) ) {
        for( const JsonMember member : jo.get_object( "skills" ) ) {
            skills.emplace_back( skill_id( member.name() ), member.get_float() );
        }
    }
    stats.clear();
    if( jo.has_object( "stats" ) ) {
        for( const JsonMember member : jo.get_object( "stats" ) ) {
            if( member.name() != "strength" && member.name() != "dexterity" &&
                member.name() != "intelligence" && member.name() != "perception" ) {
                member.throw_error( string_format( "%s isn't a stat (strength, dexterity, "
                                                   "intelligence or perception)", member.name() ) );
            }
            stats.emplace_back( member.name(), member.get_float() );
        }
    }
    optional( jo, false, "constant", constant, 0.0 );
}

double camp_skill_score::evaluate( const Character &who ) const
{
    double score = 0.0;
    for( const std::pair<skill_id, double> &skill : skills ) {
        score += skill.second * who.get_skill_level( skill.first );
    }
    for( const std::pair<std::string, double> &stat : stats ) {
        int value = 0;
        if( stat.first == "strength" ) {
            value = who.get_str();
        } else if( stat.first == "dexterity" ) {
            value = who.get_dex();
        } else if( stat.first == "intelligence" ) {
            value = who.get_int();
        } else {
            value = who.get_per();
        }
        score += stat.second * value;
    }
    return score + constant;
}

item_group_id camp_mission_results::item_group_in( const season_type season ) const
{
    const auto seasonal = seasonal_item_groups.find( season );
    if( seasonal != seasonal_item_groups.end() ) {
        return seasonal->second;
    }
    return item_group;
}

bool camp_mission_results::empty() const
{
    return item_group.is_null() && seasonal_item_groups.empty() && monster_groups.empty();
}

void camp_mission_encounter::deserialize( const JsonObject &jo )
{
    mandatory( jo, false, "one_in", one_in );
    optional( jo, false, "favor", favor, 0 );
    optional( jo, false, "threat", threat, 0 );
    // the built-in encounter's popups need the task; an EOC may do without it
    if( jo.has_member( "eoc" ) ) {
        optional( jo, false, "task", task );
    } else {
        mandatory( jo, false, "task", task );
    }
}

camp_mission_recruiting::camp_mission_recruiting()
    : find_skill( skill_survival ), convince_skill( skill_speech )
{
}

// [ "id", weight ] or "id" (weight 1)
template<typename T>
static void read_weighted_ids( const JsonObject &jo, const std::string_view member,
                               std::vector<std::pair<string_id<T>, int>> &ids )
{
    ids.clear();
    for( const JsonValue jv : jo.get_array( member ) ) {
        if( jv.test_string() ) {
            ids.emplace_back( string_id<T>( jv.get_string() ), 1 );
        } else {
            const JsonArray ja = jv.get_array();
            ids.emplace_back( string_id<T>( ja.get_string( 0 ) ), ja.get_int( 1 ) );
        }
    }
}

void camp_mission_recruiting::load( const JsonObject &jo, const std::string_view src )
{
    if( jo.has_array( "npc_classes" ) ) {
        read_weighted_ids( jo, "npc_classes", npc_classes );
    }
    if( jo.has_array( "npc_templates" ) ) {
        read_weighted_ids( jo, "npc_templates", npc_templates );
    }
    optional( jo, false, "find_skill", find_skill, skill_survival );
    optional( jo, false, "find_difficulty", find_difficulty, 17 );
    optional( jo, false, "convince_skill", convince_skill, skill_speech );
    optional( jo, false, "convince_difficulty", convince_difficulty, 19 );
    optional( jo, false, "join_difficulty", join_difficulty, 10 );
    bonuses.clear();
    if( jo.has_array( "bonuses" ) ) {
        for( const JsonObject bonus_jo : jo.get_array( "bonuses" ) ) {
            camp_recruit_bonus bonus;
            read_condition( bonus_jo, "condition", bonus.condition, true );
            mandatory( bonus_jo, false, "bonus", bonus.bonus );
            bonuses.push_back( bonus );
        }
    }
    recruit_eocs.clear();
    if( jo.has_array( "recruit_eocs" ) ) {
        for( const JsonValue jv : jo.get_array( "recruit_eocs" ) ) {
            recruit_eocs.push_back( effect_on_conditions::load_inline_eoc( jv, src ) );
        }
    }
}

int camp_mission_recruiting::bonus() const
{
    if( bonuses.empty() ) {
        return 0;
    }
    const_dialogue d( get_const_talker_for( get_avatar() ), nullptr );
    int total = 0;
    for( const camp_recruit_bonus &bonus : bonuses ) {
        if( !bonus.condition || bonus.condition( d ) ) {
            total += bonus.bonus;
        }
    }
    return total;
}

void camp_mission_training::deserialize( const JsonObject &jo )
{
    mandatory( jo, false, "skill", skill );
    optional( jo, false, "difficulty", difficulty, 1 );
}

void faction_mission::load( const JsonObject &jo, const std::string_view &src )
{
    mandatory( jo, was_loaded, "name", name );
    mandatory( jo, was_loaded, "desc", description );
    optional( jo, was_loaded, "skill", skill_used, skill_id::NULL_ID() );
    optional( jo, was_loaded, "skill_level", skill_level, 0 );
    optional( jo, was_loaded, "difficulty", difficulty, risk_diff_level::NUM_RISK_DIFF_LEVELS );
    optional( jo, was_loaded, "risk", risk, risk_diff_level::NUM_RISK_DIFF_LEVELS );
    std::string activity_level_str;
    optional( jo, was_loaded, "activity", activity_level_str );
    if( !activity_level_str.empty() ) {
        const auto activity_level_it = activity_levels_map.find( activity_level_str );
        if( activity_level_it == activity_levels_map.end() ) {
            jo.throw_error_at( "activity",
                               string_format( "Invalid faction mission activity level: %s",
                                              activity_level_str ) );
        } else {
            activity_level = activity_level_it->second;
        }
    } else if( !was_loaded ) {
        activity_level = 0.0f;
    }
    optional( jo, was_loaded, "time", time );
    optional( jo, was_loaded, "positions", positions, uint16_t{ 1 } );
    optional( jo, was_loaded, "items_label", items_label );
    optional( jo, was_loaded, "items_possibilities", items_possibilities );
    optional( jo, was_loaded, "effects", effects );
    optional( jo, was_loaded, "footer", footer );

    std::string behavior_str;
    optional( jo, was_loaded, "behavior", behavior_str );
    if( !behavior_str.empty() ) {
        const std::optional<mission_kind> kind = behavior_of( behavior_str );
        if( !kind ) {
            jo.throw_error_at( "behavior",
                               string_format( "Unknown camp mission behavior %s", behavior_str ) );
        }
        behavior = *kind;
    } else if( !was_loaded ) {
        behavior = Camp_Generic;
    }
    optional( jo, was_loaded, "required_provides", required_provides );
    optional( jo, was_loaded, "duration", duration );
    optional( jo, was_loaded, "repeatable", repeatable, false );
    optional( jo, was_loaded, "action_name", action_name );
    optional( jo, was_loaded, "return_name", return_name );
    optional( jo, was_loaded, "progress", progress );
    optional( jo, was_loaded, "depart_message", depart_message );
    optional( jo, was_loaded, "return_message", return_message );
    optional( jo, was_loaded, "train", train );
    optional( jo, was_loaded, "results", results );
    if( jo.has_object( "regional_results" ) ) {
        regional_results.clear();
        for( const JsonMember member : jo.get_object( "regional_results" ) ) {
            camp_mission_results regional;
            regional.deserialize( member.get_object() );
            regional_results[region_settings_id( member.name() )] = regional;
        }
    } else if( !was_loaded ) {
        regional_results.clear();
    }
    if( jo.has_object( "encounter" ) ) {
        const JsonObject encounter_jo = jo.get_object( "encounter" );
        camp_mission_encounter read;
        read.deserialize( encounter_jo );
        if( encounter_jo.has_member( "eoc" ) ) {
            read.eoc = effect_on_conditions::load_inline_eoc( encounter_jo.get_member( "eoc" ), src );
        }
        encounter = read;
    } else if( !was_loaded ) {
        encounter.reset();
    }
    if( jo.has_object( "recruit" ) ) {
        camp_mission_recruiting read;
        read.load( jo.get_object( "recruit" ), src );
        recruit = read;
    } else if( !was_loaded ) {
        recruit.reset();
    }
    optional( jo, was_loaded, "target_terrains", target_terrains,
              string_id_reader<oter_type_t> {} );
    if( jo.has_object( "cleared_terrains" ) ) {
        cleared_terrains.clear();
        for( const JsonMember member : jo.get_object( "cleared_terrains" ) ) {
            cleared_terrains[oter_type_str_id( member.name() )] = oter_type_str_id( member.get_string() );
        }
    } else if( !was_loaded ) {
        cleared_terrains.clear();
    }
    optional( jo, was_loaded, "field_terrains", field_terrains, string_id_reader<ter_t> {} );
    optional( jo, was_loaded, "field_overmap_terrain", field_terrain, oter_str_id::NULL_ID() );
    read_camp_mission_eocs( jo, "start_eocs", was_loaded, src, start_eocs );
    read_camp_mission_eocs( jo, "return_eocs", was_loaded, src, return_eocs );
}

namespace
{

generic_factory<faction_mission> faction_mission_factory( "faction_mission" );

} // namespace

/** @relates string_id */
template<>
const faction_mission &string_id<faction_mission>::obj() const
{
    return faction_mission_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<faction_mission>::is_valid() const
{
    return faction_mission_factory.is_valid( *this );
}

namespace io
{

template<>
std::string enum_to_string<risk_diff_level>( risk_diff_level data )
{
    switch( data ) {
        // *INDENT-OFF*
        case risk_diff_level::NONE: return "NONE";
        case risk_diff_level::VERY_LOW: return "VERY_LOW";
        case risk_diff_level::LOW: return "LOW";
        case risk_diff_level::MEDIUM: return "MEDIUM";
        case risk_diff_level::HIGH: return "HIGH";
        case risk_diff_level::VERY_HIGH: return "VERY_HIGH";
        // *INDENT-ON*
        case risk_diff_level::NUM_RISK_DIFF_LEVELS:
            break;
    }
    cata_fatal( "Invalid risk_diff_level" );
}

} // namespace io


void faction_mission::load_faction_missions( const JsonObject &jo, const std::string &src )
{
    faction_mission_factory.load( jo, src );
}

void faction_mission::reset()
{
    faction_mission_factory.reset();
}

void faction_mission::finalize_all()
{
    faction_mission_factory.finalize();
}

const std::vector<faction_mission> &faction_mission::get_all()
{
    return faction_mission_factory.get_all();
}

std::optional<mission_kind> faction_mission::behavior_of( const std::string_view name )
{
    for( const std::pair<std::string_view, mission_kind> &behavior : camp_mission_behaviors ) {
        if( behavior.first == name ) {
            return behavior.second;
        }
    }
    return std::nullopt;
}

std::string faction_mission::behavior_name( const mission_kind kind )
{
    for( const std::pair<std::string_view, mission_kind> &behavior : camp_mission_behaviors ) {
        if( behavior.second == kind ) {
            return std::string( behavior.first );
        }
    }
    return std::string();
}

const camp_mission_results &faction_mission::results_at( const tripoint_abs_omt &camp_pos ) const
{
    if( regional_results.empty() ) {
        return results;
    }
    const auto regional = regional_results.find( overmap_buffer.get_settings( camp_pos ).id );
    return regional == regional_results.end() ? results : regional->second;
}

float faction_mission::exertion() const
{
    return activity_level > 0.0f ? activity_level : NO_EXERCISE;
}

std::map<skill_id, int> faction_mission::companion_skills() const
{
    std::map<skill_id, int> skills;
    if( !skill_used.is_null() ) {
        skills[skill_used] = skill_level;
    }
    return skills;
}

mission_id faction_mission::mission_at( const point_rel_omt &dir ) const
{
    mission_id miss_id;
    miss_id.id = behavior;
    miss_id.dir = dir;
    miss_id.type = id;
    return miss_id;
}

bool faction_mission::base_only() const
{
    return behavior == Camp_Survey_Field || behavior == Camp_Survey_Expansion;
}

std::string faction_mission::action_text() const
{
    return action_name.empty() ? name.translated() : action_name.translated();
}

std::string faction_mission::return_name_text() const
{
    if( !return_name.empty() ) {
        return return_name.translated();
    }
    //~ %s: the name of a camp mission
    return string_format( _( "Recover Ally from %s" ), name );
}

std::string faction_mission::progress_text() const
{
    return progress.empty() ? name.translated() : progress.translated();
}

std::string faction_mission::depart_text() const
{
    return depart_message.empty() ? _( "departs on a mission…" ) : depart_message.translated();
}

std::string faction_mission::return_text() const
{
    return return_message.empty() ? _( "returns from the mission…" ) : return_message.translated();
}

///Format the string as a list item, e.g. "> foo"
static std::string desc_list_item( const std::string &item )
{
    return string_format( _( "> %s" ), item );
}

std::string faction_mission::describe( int npc_count,
                                       const std::function<std::vector<std::string>()> &get_items_possibilities,
                                       const std::function<std::vector<std::string>()> &get_description ) const
{
    std::vector<std::string> lines;
    lines.emplace_back( name.translated() );
    lines.emplace_back( description.translated() );
    if( get_description ) {
        for( const std::string &line : get_description() ) {
            lines.emplace_back( line );
        }
    }
    lines.emplace_back( "" );
    if( !skill_used.is_null() ) {
        if( skill_level > 0 ) {
            //~ %1$s: skill name, %2$d: the level a companion needs
            lines.emplace_back( string_format( _( "Skill used: %1$s, at least level %2$d" ),
                                               skill_used->name(), skill_level ) );
        } else {
            lines.emplace_back( string_format( _( "Skill used: %s" ), skill_used->name() ) );
        }
    }
    if( difficulty != risk_diff_level::NUM_RISK_DIFF_LEVELS ) {
        lines.emplace_back( risk_diff_to_diff_name[difficulty].translated() );
    }
    if( risk != risk_diff_level::NUM_RISK_DIFF_LEVELS ) {
        lines.emplace_back( risk_diff_to_risk_name[risk].translated() );
    }
    if( activity_level > 0 ) {
        lines.emplace_back( string_format( _( "Activity level: %s" ),
                                           display::activity_level_str( activity_level ) ) );
    }
    if( !time.empty() ) {
        lines.emplace_back( string_format( _( "Time: %s" ), time.translated() ) );
    } else if( duration ) {
        if( repeatable ) {
            //~ %s: how long one cycle of a repeatable camp mission takes
            lines.emplace_back( string_format( _( "Time: %s, Repeated" ), to_string( *duration ) ) );
        } else {
            lines.emplace_back( string_format( _( "Time: %s" ), to_string( *duration ) ) );
        }
    }
    if( !items_label.empty() ) {
        lines.emplace_back( string_format( _( "%s possibilities:" ), items_label.translated() ) );
        if( get_items_possibilities ) {
            for( const std::string &poss : get_items_possibilities() ) {
                lines.emplace_back( desc_list_item( poss ) );
            }
        } else {
            for( const translation &poss : items_possibilities ) {
                lines.emplace_back( desc_list_item( poss.translated() ) );
            }
        }
    }
    if( !effects.empty() ) {
        lines.emplace_back( _( "Effects:" ) );
        for( const translation &eff : effects ) {
            lines.emplace_back( desc_list_item( eff.translated() ) );
        }
    }
    if( !footer.empty() ) {
        lines.emplace_back( footer.translated() );
    }
    if( positions != 0 ) {
        lines.emplace_back( string_format( _( "Positions: %d/%d" ), npc_count, positions ) );
    }
    return string_join( lines, "\n" );
}

void faction_mission::check_consistency()
{
    faction_mission_factory.check();
}

void faction_mission::check() const
{
    const std::string &mid = id.str();
    if( !skill_used.is_valid() && !skill_used.is_null() ) {
        debugmsg( "Invalid skill in faction mission: %s, %s", mid, skill_used.str() );
    }
    if( skill_level < 0 ) {
        debugmsg( "faction mission %s has a negative skill_level", mid );
    }
    if( skill_level > 0 && skill_used.is_null() ) {
        debugmsg( "faction mission %s has a skill_level but no skill", mid );
    }
    if( positions == 0 ) {
        debugmsg( "faction mission %s has no positions, so no companion could be sent on it", mid );
    }
    for( const camp_provide_requirement &req : required_provides ) {
        if( req.id.empty() || req.amount < 1 ) {
            debugmsg( "faction mission %s requires an empty provide or an amount below 1", mid );
        }
    }
    if( uses_duration( behavior ) ) {
        if( !duration ) {
            debugmsg( "faction mission %s (behavior %s) needs a duration", mid,
                      behavior_name( behavior ) );
        } else if( *duration < 0_seconds ) {
            debugmsg( "faction mission %s has a negative duration", mid );
        }
    } else if( duration ) {
        debugmsg( "faction mission %s: behavior %s works out its own time and ignores duration", mid,
                  behavior_name( behavior ) );
    }
    if( behavior != Camp_Generic ) {
        if( repeatable ) {
            debugmsg( "faction mission %s: only generic missions can be repeatable", mid );
        }
        if( !results.empty() ) {
            debugmsg( "faction mission %s: only generic missions bring back results", mid );
        }
        if( encounter ) {
            debugmsg( "faction mission %s: only generic missions have an encounter", mid );
        }
    } else if( repeatable && duration && *duration <= 0_seconds ) {
        debugmsg( "faction mission %s is repeatable, so it needs a duration above 0", mid );
    }
    if( train ) {
        const std::string &trained = train->skill;
        bool known = trained == "combat" || skill_id( trained ).is_valid();
        for( const Skill &sk : Skill::skills ) {
            known |= sk.get_companion_skill_practice( trained ) > 0;
        }
        if( !known ) {
            debugmsg( "faction mission %s trains %s, which is neither a skill nor a "
                      "companion_skill_practice group", mid, trained );
        }
        if( train->difficulty < 0 ) {
            debugmsg( "faction mission %s trains at a negative difficulty", mid );
        }
    }
    std::vector<const camp_mission_results *> all_results{ &results };
    for( const std::pair<const region_settings_id, camp_mission_results> &regional :
         regional_results ) {
        if( !regional.first.is_valid() ) {
            debugmsg( "faction mission %s has results for the unknown region %s", mid,
                      regional.first.str() );
        }
        all_results.push_back( &regional.second );
    }
    if( behavior != Camp_Generic && !regional_results.empty() ) {
        debugmsg( "faction mission %s: only generic missions bring back regional_results", mid );
    }
    for( const camp_mission_results *checked : all_results ) {
        if( !checked->item_group.is_null() && !item_group::group_is_defined( checked->item_group ) ) {
            debugmsg( "faction mission %s has an unknown item group %s", mid, checked->item_group.str() );
        }
        for( const std::pair<const season_type, item_group_id> &seasonal :
             checked->seasonal_item_groups ) {
            if( !item_group::group_is_defined( seasonal.second ) ) {
                debugmsg( "faction mission %s has an unknown %s item group %s", mid,
                          io::enum_to_string( seasonal.first ), seasonal.second.str() );
            }
        }
        for( const mongroup_id &group : checked->monster_groups ) {
            if( !group.is_valid() ) {
                debugmsg( "faction mission %s has an unknown monster group %s", mid, group.str() );
            }
        }
        for( const std::pair<skill_id, double> &skill : checked->skill_score.skills ) {
            if( !skill.first.is_valid() ) {
                debugmsg( "faction mission %s scores the unknown skill %s", mid, skill.first.str() );
            }
        }
        if( checked->attempts < 0 ) {
            debugmsg( "faction mission %s has negative attempts", mid );
        }
    }
    if( encounter && encounter->one_in < 0 ) {
        debugmsg( "faction mission %s has a negative encounter one_in", mid );
    }
    if( encounter && !encounter->eoc.is_null() && !encounter->eoc.is_valid() ) {
        debugmsg( "faction mission %s has an unknown encounter effect_on_condition %s", mid,
                  encounter->eoc.str() );
    }
    if( recruit ) {
        if( behavior != Camp_Recruiting ) {
            debugmsg( "faction mission %s: only recruiting missions have a recruit", mid );
        }
        for( const std::pair<npc_class_id, int> &cls : recruit->npc_classes ) {
            if( !cls.first.is_valid() || cls.second < 1 ) {
                debugmsg( "faction mission %s recruits the unknown npc class %s, or with a weight "
                          "below 1", mid, cls.first.str() );
            }
        }
        for( const std::pair<npc_template_id, int> &tem : recruit->npc_templates ) {
            if( !tem.first.is_valid() || tem.second < 1 ) {
                debugmsg( "faction mission %s recruits the unknown npc template %s, or with a weight "
                          "below 1", mid, tem.first.str() );
            }
        }
        for( const skill_id &sk : {
                 recruit->find_skill, recruit->convince_skill
             } ) {
            if( !sk.is_valid() ) {
                debugmsg( "faction mission %s recruits with the unknown skill %s", mid, sk.str() );
            }
        }
        for( const effect_on_condition_id &eoc : recruit->recruit_eocs ) {
            if( !eoc.is_valid() ) {
                debugmsg( "faction mission %s has an unknown recruit_eocs entry %s", mid, eoc.str() );
            }
        }
    }
    const bool picks_target = behavior == Camp_Cut_Logs || behavior == Camp_Clearcut ||
                              behavior == Camp_Setup_Hide_Site;
    if( picks_target && target_terrains.empty() ) {
        debugmsg( "faction mission %s (behavior %s) needs target_terrains", mid,
                  behavior_name( behavior ) );
    } else if( !picks_target && !target_terrains.empty() ) {
        debugmsg( "faction mission %s: behavior %s has no target and ignores target_terrains", mid,
                  behavior_name( behavior ) );
    }
    for( const oter_type_str_id &target : target_terrains ) {
        if( !target.is_valid() ) {
            debugmsg( "faction mission %s targets the unknown overmap terrain type %s", mid,
                      target.str() );
        }
    }
    if( !cleared_terrains.empty() && behavior != Camp_Cut_Logs && behavior != Camp_Clearcut ) {
        debugmsg( "faction mission %s: only logging missions use cleared_terrains", mid );
    }
    for( const std::pair<const oter_type_str_id, oter_type_str_id> &cleared : cleared_terrains ) {
        if( !cleared.first.is_valid() || !cleared.second.is_valid() ) {
            debugmsg( "faction mission %s clears %s into %s, an unknown overmap terrain type", mid,
                      cleared.first.str(), cleared.second.str() );
        } else if( cleared.second->is_linear() && !cleared.first->is_linear() ) {
            debugmsg( "faction mission %s clears %s into the linear %s, but %s isn't linear", mid,
                      cleared.first.str(), cleared.second.str(), cleared.first.str() );
        }
    }
    if( behavior == Camp_Survey_Field ) {
        if( field_terrains.empty() || field_terrain.is_null() ) {
            debugmsg( "faction mission %s (behavior survey_field) needs field_terrains and "
                      "field_overmap_terrain", mid );
        }
    } else if( !field_terrains.empty() || !field_terrain.is_null() ) {
        debugmsg( "faction mission %s: only survey_field missions use field_terrains and "
                  "field_overmap_terrain", mid );
    }
    for( const ter_str_id &ter : field_terrains ) {
        if( !ter.is_valid() ) {
            debugmsg( "faction mission %s has the unknown field terrain %s", mid, ter.str() );
        }
    }
    if( !field_terrain.is_null() && !field_terrain.is_valid() ) {
        debugmsg( "faction mission %s turns tiles into the unknown overmap terrain %s", mid,
                  field_terrain.str() );
    }
    for( const std::vector<effect_on_condition_id> *eocs : {
             &start_eocs, &return_eocs
         } ) {
        for( const effect_on_condition_id &eoc : *eocs ) {
            if( !eoc.is_valid() ) {
                debugmsg( "faction mission %s has an unknown effect_on_condition %s", mid, eoc.str() );
            }
        }
    }
}
