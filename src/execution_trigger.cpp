#include "execution_trigger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <utility>

#include "bodypart.h"
#include "condition.h"
#include "damage.h"
#include "debug.h"
#include "execution.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "json.h"
#include "mapdata.h"
#include "magic.h"
#include "martialarts.h"
#include "mtype.h"
#include "skill.h"

namespace
{
generic_factory<execution_trigger> factory( "execution trigger" );
// Reserve deferred and array IDs too; factory.size() alone misses both during loading.
std::set<std::string> declared_ids;
struct channel_index {
    std::vector<const execution_trigger *> common;
    std::map<std::string, std::vector<const execution_trigger *>> weapons;
    std::map<std::string, std::vector<const execution_trigger *>> ammunition;
    std::map<std::string, std::vector<const execution_trigger *>> spells;
    std::map<std::string, std::vector<const execution_trigger *>> monster_attacks;
    std::map<std::string, std::vector<const execution_trigger *>> source_monsters;
    std::map<std::string, std::vector<const execution_trigger *>> target_monsters;
    std::map<std::string, std::vector<const execution_trigger *>> styles;
    std::map<std::string, std::vector<const execution_trigger *>> categories;
    std::map<std::string, std::vector<const execution_trigger *>> techniques;
    bool empty = true;
    bool fatal = false;
};
std::array<channel_index, static_cast<size_t>( execution_combat::channel::count )> indexes;
bool dirty = true;

void rebuild()
{
    if( factory.size() > 2048 ) {
        throw JsonError( "at most 2048 execution trigger rules are supported" );
    }
    indexes = {};
    for( const execution_trigger &rule : factory.get_all() ) {
        for( execution_combat::channel channel : rule.channels ) {
            channel_index &index = indexes[static_cast<size_t>( channel )];
            index.empty = false;
            index.fatal = index.fatal || rule.fatal;
            const auto add = [&]( const auto & keys, auto & buckets ) {
                for( const std::string &key : keys ) {
                    buckets[key].push_back( &rule );
                }
            };
            if( !rule.weapons.empty() ) {
                add( rule.weapons, index.weapons );
            } else if( !rule.ammunition.empty() ) {
                add( rule.ammunition, index.ammunition );
            } else if( !rule.spells.empty() ) {
                add( rule.spells, index.spells );
            } else if( !rule.monster_attacks.empty() ) {
                add( rule.monster_attacks, index.monster_attacks );
            } else if( !rule.source_monsters.empty() ) {
                add( rule.source_monsters, index.source_monsters );
            } else if( !rule.target_monsters.empty() ) {
                add( rule.target_monsters, index.target_monsters );
            } else if( !rule.styles.empty() ) {
                add( rule.styles, index.styles );
            } else if( !rule.weapon_categories.empty() ) {
                add( rule.weapon_categories, index.categories );
            } else if( !rule.techniques.empty() ) {
                add( rule.techniques, index.techniques );
            } else {
                index.common.push_back( &rule );
            }
        }
    }
    dirty = false;
}

int integer( const JsonObject &jo, const std::string_view key, int fallback, int low, int high )
{
    const int result = jo.get_int( key, fallback );
    if( result < low || result > high ) {
        jo.throw_error_at( key, "integer outside supported bounds" );
    }
    return result;
}

execution_trigger_range range( const JsonObject &jo )
{
    execution_trigger_range out;
    out.minimum = integer( jo, "minimum", 0, 0, std::numeric_limits<int>::max() );
    out.maximum = integer( jo, "maximum", std::numeric_limits<int>::max(), 0,
                           std::numeric_limits<int>::max() );
    if( out.maximum < out.minimum ) {
        jo.throw_error( "maximum must be at least minimum" );
    }
    return out;
}

std::set<std::string> strings( const JsonObject &jo, const std::string_view key )
{
    if( !jo.has_member( key ) ) {
        return {};
    }
    const auto out = jo.get_tags<std::string>( key );
    if( out.empty() || out.size() > 64 || out.count( "" ) ) {
        jo.throw_error_at( key, "expected 1–64 nonempty identifiers" );
    }
    return out;
}

bool accepts( const std::set<std::string> &filter, const std::string &value )
{
    return filter.empty() || filter.count( value ) != 0;
}

bool accepts( const std::set<std::string> &filter, const std::optional<std::string> &value )
{
    return filter.empty() || ( value && filter.count( *value ) != 0 );
}
} // namespace

template<>
const execution_trigger &execution_trigger_id::obj() const
{
    return factory.obj( *this );
}

template<>
bool execution_trigger_id::is_valid() const
{
    return factory.is_valid( *this );
}

bool execution_trigger_range::contains( int value ) const
{
    return value >= minimum && value <= maximum;
}

void execution_trigger::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "execution", execution );
    const std::string event = jo.get_string( "event", fatal ? "fatal_hit" : "attack_resolved" );
    if( event != "attack_resolved" && event != "fatal_hit" ) {
        jo.throw_error_at( "event", "expected attack_resolved or fatal_hit" );
    }
    fatal = event == "fatal_hit";
    priority = integer( jo, "priority", priority, -1000000, 1000000 );
    weight = integer( jo, "weight", weight, 1, 1000000 );
    chance = jo.get_float( "chance", chance );
    if( !std::isfinite( chance ) || chance < 0 || chance > 1 ) {
        jo.throw_error_at( "chance", "expected a finite probability from zero to one" );
    }
    if( jo.has_member( "cooldown" ) ) {
        const JsonObject cooldown = jo.get_object( "cooldown" );
        cooldown_ticks = integer( cooldown, "ticks", 0, 0, 31536000 );
        cooldown_scope = cooldown.get_string( "scope", "actor_target_pair" );
        if( cooldown_scope != "actor" && cooldown_scope != "target" &&
            cooldown_scope != "actor_target_pair" && cooldown_scope != "world" ) {
            cooldown.throw_error_at( "scope", "expected actor, target, actor_target_pair or world" );
        }
    }
    if( channels.empty() ) {
        channels = { execution_combat::channel::character_melee, execution_combat::channel::monster_melee };
    }
    if( jo.has_member( "match" ) ) {
        const JsonObject match = jo.get_object( "match" );
        const std::set<std::string> allowed = { "channels", "source_kind", "target_kind", "source_monsters",
                                                "target_monsters", "weapons", "ammunition", "weapon_categories", "weapon_skills", "styles", "techniques",
                                                "body_parts", "weakpoints", "terrains", "furniture", "hit", "critical", "hp_loss", "damage", "fatal_state",
                                                "spells", "monster_attacks"
                                              };
        for( const JsonMember member : match ) {
            if( !allowed.count( member.name() ) ) {
                member.throw_error( "unsupported execution trigger match field" );
            }
        }
        const auto read = [&]( const std::string_view key, auto & destination ) {
            if( match.has_member( key ) ) {
                destination = strings( match, key );
            }
        };
        if( match.has_member( "channels" ) ) {
            channels.clear();
            for( const std::string &channel : strings( match, "channels" ) ) {
                if( channel == "melee" || channel == "character_melee" ) {
                    channels.insert( execution_combat::channel::character_melee );
                }
                if( channel == "melee" || channel == "monster_melee" ) {
                    channels.insert( execution_combat::channel::monster_melee );
                }
                if( channel == "projectile" ) {
                    channels.insert( execution_combat::channel::projectile );
                }
                if( channel == "thrown" ) {
                    channels.insert( execution_combat::channel::thrown );
                }
                if( channel == "spell" ) {
                    channels.insert( execution_combat::channel::spell );
                }
                if( channel == "monster_special" ) {
                    channels.insert( execution_combat::channel::monster_special );
                }
                if( channel != "melee" && channel != "character_melee" && channel != "monster_melee" &&
                    channel != "projectile" && channel != "thrown" && channel != "spell" &&
                    channel != "monster_special" ) {
                    match.throw_error_at( "channels", "unsupported combat producer" );
                }
            }
        }
        read( "source_kind", source_kinds );
        read( "target_kind", target_kinds );
        for( const auto *kinds : {
                 &source_kinds, &target_kinds
             } ) {
            for( const std::string &kind : *kinds ) {
                if( kind != "avatar" && kind != "npc" && kind != "monster" ) {
                    match.throw_error( "participant kinds are avatar, npc or monster" );
                }
            }
        }
        read( "source_monsters", source_monsters );
        read( "target_monsters", target_monsters );
        read( "weapons", weapons );
        read( "ammunition", ammunition );
        read( "spells", spells );
        read( "monster_attacks", monster_attacks );
        read( "weapon_categories", weapon_categories );
        read( "weapon_skills", weapon_skills );
        read( "styles", styles );
        read( "techniques", techniques );
        read( "body_parts", bodyparts );
        read( "weakpoints", weakpoints );
        read( "terrains", terrains );
        read( "furniture", furniture );
        hit = match.get_bool( "hit", hit );
        if( match.has_member( "critical" ) ) {
            critical = match.get_bool( "critical" );
        }
        if( match.get_string( "fatal_state", fatal ? "fatal" : "alive" ) !=
            ( fatal ? "fatal" : "alive" ) ) {
            match.throw_error_at( "fatal_state", "fatal_state must agree with the trigger event" );
        }
        if( match.has_member( "hp_loss" ) ) {
            hp_loss = range( match.get_object( "hp_loss" ) );
        }
        if( match.has_member( "damage" ) ) {
            damage.clear();
            for( const JsonObject entry : match.get_array( "damage" ) ) {
                const std::string type = entry.get_string( "type" );
                if( type.empty() || !damage.emplace( type, range( entry ) ).second || damage.size() > 64 ) {
                    entry.throw_error( "expected up to 64 distinct damage types" );
                }
            }
        }
    }
    if( !hit && ( channels.count( execution_combat::channel::projectile ) ||
                  channels.count( execution_combat::channel::thrown ) ||
                  channels.count( execution_combat::channel::spell ) ||
                  channels.count( execution_combat::channel::monster_special ) ) ) {
        jo.throw_error( "projectile, thrown, spell and monster_special rules support direct hits only" );
    }
    if( jo.has_member( "condition" ) ) {
        read_condition( jo, "condition", condition, false );
    }
}

void execution_trigger::check() const
{
    if( !execution.is_valid() ) {
        debugmsg( "Execution trigger %s references unknown execution %s", id.str(), execution.str() );
    }
    if( fatal && execution.is_valid() && !execution->allow_fatal_target ) {
        debugmsg( "Fatal execution trigger %s requires allow_fatal_target on %s", id.str(),
                  execution.str() );
    }
    const auto check_ids = [&]( const auto & values, const auto & make_id ) {
        for( const std::string &value : values ) {
            if( !make_id( value ).is_valid() ) {
                debugmsg( "Execution trigger %s references unknown identifier %s", id.str(), value );
            }
        }
    };
    check_ids( source_monsters, []( const std::string & s ) {
        return mtype_id( s );
    } );
    check_ids( target_monsters, []( const std::string & s ) {
        return mtype_id( s );
    } );
    check_ids( weapons, []( const std::string & s ) {
        return itype_id( s );
    } );
    check_ids( ammunition, []( const std::string & s ) {
        return itype_id( s );
    } );
    check_ids( spells, []( const std::string & s ) {
        return spell_id( s );
    } );
    check_ids( weapon_categories, []( const std::string & s ) {
        return weapon_category_id( s );
    } );
    check_ids( weapon_skills, []( const std::string & s ) {
        return skill_id( s );
    } );
    check_ids( styles, []( const std::string & s ) {
        return matype_id( s );
    } );
    check_ids( techniques, []( const std::string & s ) {
        return matec_id( s );
    } );
    check_ids( bodyparts, []( const std::string & s ) {
        return bodypart_str_id( s );
    } );
    check_ids( terrains, []( const std::string & s ) {
        return ter_str_id( s );
    } );
    check_ids( furniture, []( const std::string & s ) {
        return furn_str_id( s );
    } );
    for( const auto &entry : damage ) {
        if( !damage_type_id( entry.first ).is_valid() ) {
            debugmsg( "Execution trigger %s references unknown damage type %s", id.str(), entry.first );
        }
    }
}

std::string execution_trigger::matches( const execution_combat::attack_snapshot &s ) const
{
    if( fatal != s.fatal || !channels.count( s.producer ) || hit != s.hit ||
        !accepts( source_kinds, s.source_kind ) || !accepts( target_kinds, s.target_kind ) ||
        !accepts( source_monsters, s.source_monster ) || !accepts( target_monsters, s.target_monster ) ||
        !accepts( weapons, s.weapon ) || !accepts( ammunition, s.ammunition ) ||
        !accepts( spells, s.spell_id ) || !accepts( monster_attacks, s.monster_attack ) ||
        !accepts( weapon_skills, s.weapon_skill ) ||
        !accepts( styles, s.style ) || !accepts( techniques, s.technique ) ||
        !accepts( bodyparts, s.bodypart ) || !accepts( weakpoints, s.weakpoint ) ||
        !accepts( terrains, s.terrain ) || !accepts( furniture, s.furniture ) ) {
        return "filtered";
    }
    if( critical && ( !s.critical || *critical != *s.critical ) ) {
        return "critical_unavailable_or_mismatched";
    }
    if( !weapon_categories.empty() && std::none_of( weapon_categories.begin(), weapon_categories.end(),
    [&]( const std::string & category ) {
    return s.weapon_categories.count( category ) != 0;
    } ) ) {
        return "weapon_category";
    }
    if( hp_loss && ( !s.hp_loss || !hp_loss->contains( *s.hp_loss ) ) ) {
        return "hp_loss";
    }
    if( !damage.empty() && !s.hp_loss ) {
        return "damage_unavailable";
    }
    for( const auto &entry : damage ) {
        const auto found = s.damage.find( entry.first );
        if( !entry.second.contains( found == s.damage.end() ? 0 : found->second ) ) {
            return "damage";
        }
    }
    return {};
}

std::string execution_trigger::cooldown_key( const execution_combat::attack_snapshot &s ) const
{
    std::ostringstream text;
    JsonOut out( text );
    out.start_array();
    out.write( id.str() );
    out.write( cooldown_scope );
    out.write( s.actor.dimension.str() );
    if( cooldown_scope == "actor" || cooldown_scope == "actor_target_pair" ) {
        out.write( s.actor );
    }
    if( cooldown_scope == "target" || cooldown_scope == "actor_target_pair" ) {
        out.write( s.target );
    }
    out.end_array();
    return text.str();
}

void execution_triggers::load( const JsonObject &jo, const std::string &source )
{
    const std::set<std::string> ids = jo.has_member( "id" ) ?
                                      jo.get_tags<std::string>( "id" ) : std::set<std::string>();
    const size_t additional = std::count_if( ids.begin(), ids.end(), []( const std::string & id ) {
        return declared_ids.count( id ) == 0;
    } );
    if( declared_ids.size() + additional > 2048 ) {
        jo.throw_error( "at most 2048 execution trigger rules are supported" );
    }
    dirty = true;
    factory.load( jo, source );
    declared_ids.insert( ids.begin(), ids.end() );
}

void execution_triggers::reset()
{
    factory.reset();
    declared_ids.clear();
    indexes = {};
    dirty = true;
}

void execution_triggers::finalize()
{
    factory.finalize();
    rebuild();
}

void execution_triggers::check()
{
    factory.check();
}

bool execution_triggers::has_rules( execution_combat::channel channel )
{
    if( static_cast<size_t>( channel ) >= indexes.size() ) {
        return false;
    }
    if( dirty ) {
        rebuild();
    }
    return !indexes[static_cast<size_t>( channel )].empty;
}

bool execution_triggers::has_fatal_rules( execution_combat::channel channel )
{
    if( static_cast<size_t>( channel ) >= indexes.size() ) {
        return false;
    }
    if( dirty ) {
        rebuild();
    }
    return indexes[static_cast<size_t>( channel )].fatal;
}

std::vector<const execution_trigger *> execution_triggers::candidates(
    const execution_combat::attack_snapshot &s )
{
    if( static_cast<size_t>( s.producer ) >= indexes.size() ) {
        return {};
    }
    if( dirty ) {
        rebuild();
    }
    const channel_index &index = indexes[static_cast<size_t>( s.producer )];
    std::vector<const execution_trigger *> out = index.common;
    const auto add = [&]( const auto & buckets, const std::optional<std::string> &key ) {
        if( key ) {
            const auto found = buckets.find( *key );
            if( found != buckets.end() ) {
                out.insert( out.end(), found->second.begin(), found->second.end() );
            }
        }
    };
    add( index.weapons, s.weapon );
    add( index.ammunition, s.ammunition );
    add( index.spells, s.spell_id );
    add( index.monster_attacks, s.monster_attack );
    add( index.source_monsters, s.source_monster );
    add( index.target_monsters, s.target_monster );
    add( index.styles, s.style );
    add( index.techniques, s.technique );
    for( const std::string &category : s.weapon_categories ) {
        add( index.categories, category );
    }
    std::sort( out.begin(), out.end(), []( const execution_trigger * a, const execution_trigger * b ) {
        // NOLINTBEGIN(cata-use-localized-sorting)
        return a->priority != b->priority ? a->priority > b->priority : a->id.str() <
               b->id.str();
        // NOLINTEND(cata-use-localized-sorting)
    } );
    out.erase( std::unique( out.begin(), out.end() ), out.end() );
    return out;
}
