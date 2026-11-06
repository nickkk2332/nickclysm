#include "npc_ai_tuning.h"

#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "character.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "npc.h"
#include "npc_arbitration.h"
#include "npc_class.h"
#include "string_formatter.h"

// The defaults are the constants the code had before tuning (npc_arbitration.h and elsewhere).
npc_ai_tuning::npc_ai_tuning()
{
    need_critical_urgency = ::need_critical_urgency;
    // thirst on its 1200 scale; emergency where any need is critical
    drink_water = { npc_thirst_act_level / npc_thirst_scale, 800.0f / npc_thirst_scale,
                    ::need_critical_urgency
                  };
    // the share of healthy stored calories missing
    eat_food = { food_act_level, 0.4f, 0.6f };
    go_to_sleep = { static_cast<float>( sleepiness_levels::TIRED ) / npc_sleepiness_scale,
                    static_cast<float>( sleepiness_levels::DEAD_TIRED ) / npc_sleepiness_scale,
                    static_cast<float>( sleepiness_levels::EXHAUSTED ) / npc_sleepiness_scale
                  };
    // no option costs declared: overrides wait for the critical level
    regulate_temperature = { ::need_critical_urgency, ::need_critical_urgency, ::need_critical_urgency };

    duty = 0.45f;
    duty_max = 0.5f;
    follow_max = 0.6f;
    goto_order = 0.65f;
    camp_work = 0.4f;
    return_to_camp = 0.5f;
    free_time = 0.35f;

    starvation_hunger = 300;
    camp_meal_kcal = 500;

    settle_interval = 3_minutes;
    settle_timeout = 30_minutes;
    stall_wakes = 3;
    stall_window = 30_minutes;
    stall_duration = 30_minutes;
}

const npc_ai_tuning &npc_ai_tuning::defaults()
{
    static const npc_ai_tuning vanilla;
    return vanilla;
}

int npc_ai_tuning::thirst_act_level() const
{
    return static_cast<int>( std::lround( drink_water.act * npc_thirst_scale ) );
}

int npc_ai_tuning::tired_level() const
{
    return static_cast<int>( std::lround( go_to_sleep.act * npc_sleepiness_scale ) );
}

// the levels of the need with this goal, for a tuning or a const one
template<typename Tuning>
static auto *need_levels( Tuning &t, const std::string_view goal )
{
    decltype( &t.drink_water ) none = nullptr;
    if( goal == "drink_water" ) {
        return &t.drink_water;
    } else if( goal == "eat_food" ) {
        return &t.eat_food;
    } else if( goal == "go_to_sleep" ) {
        return &t.go_to_sleep;
    } else if( goal == "regulate_temperature" ) {
        return &t.regulate_temperature;
    }
    return none;
}

const npc_need_levels *npc_ai_tuning::need( const std::string_view goal ) const
{
    return need_levels( *this, goal );
}

void npc_ai_tuning::load( const JsonObject &jo )
{
    if( jo.has_object( "needs" ) ) {
        const JsonObject needs = jo.get_object( "needs" );
        for( const JsonMember member : needs ) {
            npc_need_levels *levels = need_levels( *this, member.name() );
            if( levels == nullptr ) {
                member.throw_error( string_format(
                                        "%s isn't a need: drink_water, eat_food, go_to_sleep or regulate_temperature",
                                        member.name() ) );
            }
            const JsonObject lv = member.get_object();
            optional( lv, true, "act", levels->act );
            optional( lv, true, "insist", levels->insist );
            optional( lv, true, "emergency", levels->emergency );
        }
    }
    optional( jo, true, "need_critical_urgency", need_critical_urgency );
    if( jo.has_object( "urgency" ) ) {
        const JsonObject urgency = jo.get_object( "urgency" );
        optional( urgency, true, "duty", duty );
        optional( urgency, true, "duty_max", duty_max );
        optional( urgency, true, "follow_max", follow_max );
        optional( urgency, true, "goto", goto_order );
        optional( urgency, true, "camp_work", camp_work );
        optional( urgency, true, "return_to_camp", return_to_camp );
        optional( urgency, true, "free_time", free_time );
    }
    optional( jo, true, "starvation_hunger", starvation_hunger );
    optional( jo, true, "camp_meal_kcal", camp_meal_kcal );
    if( jo.has_object( "sleep" ) ) {
        const JsonObject sleep = jo.get_object( "sleep" );
        optional( sleep, true, "settle_interval", settle_interval );
        optional( sleep, true, "settle_timeout", settle_timeout );
        optional( sleep, true, "stall_wakes", stall_wakes );
        optional( sleep, true, "stall_window", stall_window );
        optional( sleep, true, "stall_duration", stall_duration );
    }
    const std::string wrong = problems();
    if( !wrong.empty() ) {
        jo.throw_error( wrong );
    }
}

std::string npc_ai_tuning::problems() const
{
    std::vector<std::string> wrong;
    const auto in_range = [&wrong]( const std::string & name, const float value ) {
        if( !( value >= 0.0f && value <= 1.0f ) ) {
            wrong.push_back( string_format( "%s is %g, not between 0 and 1", name, value ) );
        }
    };
    for( const char *goal : {
             "drink_water", "eat_food", "go_to_sleep", "regulate_temperature"
         } ) {
        const npc_need_levels &levels = *need( goal );
        in_range( string_format( "needs.%s.act", goal ), levels.act );
        in_range( string_format( "needs.%s.insist", goal ), levels.insist );
        in_range( string_format( "needs.%s.emergency", goal ), levels.emergency );
        if( !( levels.act <= levels.insist && levels.insist <= levels.emergency ) ) {
            wrong.push_back( string_format(
                                 "needs.%s: act (%g), insist (%g) and emergency (%g) must not go down", goal,
                                 levels.act, levels.insist, levels.emergency ) );
        }
    }
    in_range( "need_critical_urgency", need_critical_urgency );
    in_range( "urgency.duty", duty );
    in_range( "urgency.duty_max", duty_max );
    in_range( "urgency.follow_max", follow_max );
    in_range( "urgency.goto", goto_order );
    in_range( "urgency.camp_work", camp_work );
    in_range( "urgency.return_to_camp", return_to_camp );
    in_range( "urgency.free_time", free_time );
    if( duty > duty_max ) {
        wrong.push_back( string_format( "urgency.duty (%g) is above urgency.duty_max (%g)", duty,
                                        duty_max ) );
    }
    if( starvation_hunger < 0 ) {
        wrong.push_back( string_format( "starvation_hunger is %d, below 0", starvation_hunger ) );
    }
    if( camp_meal_kcal < 0 ) {
        wrong.push_back( string_format( "camp_meal_kcal is %d, below 0", camp_meal_kcal ) );
    }
    for( const std::pair<const char *, time_duration> &dur : {
             std::pair<const char *, time_duration>( "sleep.settle_interval", settle_interval ),
             std::pair<const char *, time_duration>( "sleep.settle_timeout", settle_timeout ),
             std::pair<const char *, time_duration>( "sleep.stall_window", stall_window ),
             std::pair<const char *, time_duration>( "sleep.stall_duration", stall_duration )
         } ) {
        if( dur.second < 1_turns ) {
            wrong.push_back( string_format( "%s must be a turn or longer", dur.first ) );
        }
    }
    if( stall_wakes < 1 ) {
        wrong.push_back( string_format( "sleep.stall_wakes is %d, below 1", stall_wakes ) );
    }
    std::string ret;
    for( const std::string &w : wrong ) {
        ret += ret.empty() ? "ai_tuning: " + w : "; " + w;
    }
    return ret;
}

const npc_ai_tuning &ai_tuning_of( const npc &guy )
{
    return guy.myclass.is_valid() ? guy.myclass->get_ai_tuning() : npc_ai_tuning::defaults();
}

const npc_ai_tuning &ai_tuning_of( const Character &who )
{
    const npc *guy = who.as_npc();
    return guy == nullptr ? npc_ai_tuning::defaults() : ai_tuning_of( *guy );
}

float npc_need_floor( const npc_need_levels &levels, const npc_permission_verdict &v )
{
    switch( v.permission ) {
        case npc_permission::allowed:
            return 0.0f;
        case npc_permission::discouraged:
            // relaxes from insist toward the need's own level as ease grows
            return levels.act + ( levels.insist - levels.act ) * ( 1.0f - v.ease );
        case npc_permission::emergency_only:
            return levels.emergency;
        case npc_permission::blocked:
            break;
    }
    return std::numeric_limits<float>::infinity();
}
