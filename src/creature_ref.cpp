#include "creature_ref.h"

#include <list>
#include <memory>
#include <string>
#include <utility>

#include "avatar.h"
#include "character.h"
#include "character_id.h"
#include "creature.h"
#include "creature_tracker.h"
#include "execution_fatal.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "json.h"
#include "monster.h"
#include "npc.h"

monster_uid::monster_uid( const monster_uid & )
{
}

monster_uid &monster_uid::operator=( const monster_uid &other )
{
    if( this != &other ) {
        value = 0;
    }
    return *this;
}

monster_uid::monster_uid( monster_uid &&other ) noexcept : value( std::exchange( other.value, 0 ) )
{
}

monster_uid &monster_uid::operator=( monster_uid &&other ) noexcept
{
    if( this != &other ) {
        value = std::exchange( other.value, 0 );
    }
    return *this;
}

int64_t monster_uid::get() const
{
    if( value == 0 && g ) {
        value = g->assign_monster_uid();
    }
    return value;
}

void monster_uid::load( int64_t saved )
{
    value = saved > 0 ? saved : 0;
    if( g ) {
        g->reserve_monster_uid( value );
    }
}

creature_ref creature_ref::bind( const Creature &who )
{
    creature_ref result;
    result.dimension = g->get_dimension_prefix();
    if( const Character *person = who.as_character() ) {
        result.type = kind::character;
        result.id = person->getID().get_value();
    } else if( const monster *mon = who.as_monster() ) {
        result.type = kind::monster;
        result.id = mon->persistent_id();
    }
    return result;
}

Creature *creature_ref::resolve() const
{
    Creature *who = resolve_loaded();
    return who && !who->is_dead_state() ? who : nullptr;
}

Creature *creature_ref::resolve_for_execution() const
{
    Creature *who = resolve_loaded();
    return who && ( !who->is_dead_state() || execution_fatal::pending( *who ) ) ? who : nullptr;
}

Creature *creature_ref::resolve_loaded() const
{
    if( !g || id <= 0 || dimension != g->get_dimension_prefix() ) {
        return nullptr;
    }
    Creature *result = nullptr;
    if( type == kind::character ) {
        if( get_avatar().getID().get_value() == id ) {
            result = &get_avatar();
        } else {
            // Only active NPCs can participate. An unloaded resident is not an offscreen actor.
            for( const auto &person : get_creature_tracker().get_npcs_list() ) {
                if( person->getID().get_value() == id ) {
                    result = person.get();
                    break;
                }
            }
        }
    } else if( type == kind::monster ) {
        result = get_creature_tracker().find_by_uid( id ).get();
    }
    return result && !execution_fatal::finalized( *result ) ? result : nullptr;
}

bool creature_ref::operator==( const creature_ref &other ) const
{
    return type == other.type && id == other.id && dimension == other.dimension;
}

void creature_ref::serialize( JsonOut &jsout ) const
{
    jsout.start_object();
    jsout.member( "kind", type == kind::character ? "character" : type == kind::monster ? "monster" :
                  "none" );
    jsout.member( "id", id );
    jsout.member( "dimension", dimension );
    jsout.end_object();
}

void creature_ref::deserialize( const JsonObject &jo )
{
    const std::string name = jo.get_string( "kind" );
    if( name != "character" && name != "monster" && name != "none" ) {
        jo.throw_error_at( "kind", "invalid creature reference kind" );
    }
    type = name == "character" ? kind::character : name == "monster" ? kind::monster : kind::none;
    jo.read( "id", id );
    jo.read( "dimension", dimension );
}
