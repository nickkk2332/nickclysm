#include "skill.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "character.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "game_constants.h"
#include "generic_factory.h"
#include "item.h"
#include "options.h"
#include "recipe.h"
#include "rng.h"
#include "string_formatter.h"

static const skill_id skill_archery( "archery" );
static const skill_id skill_bashing( "bashing" );
static const skill_id skill_computer( "computer" );
static const skill_id skill_cooking( "cooking" );
static const skill_id skill_cutting( "cutting" );
static const skill_id skill_dodge( "dodge" );
static const skill_id skill_driving( "driving" );
static const skill_id skill_electronics( "electronics" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_gun( "gun" );
static const skill_id skill_launcher( "launcher" );
static const skill_id skill_mechanics( "mechanics" );
static const skill_id skill_melee( "melee" );
static const skill_id skill_pistol( "pistol" );
static const skill_id skill_rifle( "rifle" );
static const skill_id skill_shotgun( "shotgun" );
static const skill_id skill_smg( "smg" );
static const skill_id skill_speech( "speech" );
static const skill_id skill_stabbing( "stabbing" );
static const skill_id skill_survival( "survival" );
static const skill_id skill_swimming( "swimming" );
static const skill_id skill_tailor( "tailor" );
static const skill_id skill_throw( "throw" );
static const skill_id skill_traps( "traps" );
static const skill_id skill_unarmed( "unarmed" );

// TODO: a map, for Barry's sake make this a map.
std::vector<Skill> Skill::skills;
std::map<skill_id, Skill> Skill::contextual_skills;

std::vector<SkillDisplayType> SkillDisplayType::skillTypes;

static const Skill invalid_skill;
static const SkillDisplayType invalid_skill_type;

namespace
{
// Holds every skill definition; Skill::skills and Skill::contextual_skills are built from it.
generic_factory<Skill> skill_factory( "skill" );
uint64_t next_skill_load_order = 0;
} // namespace

/** @relates string_id */
template<>
const Skill &string_id<Skill>::obj() const
{
    for( const Skill &skill : Skill::skills ) {
        if( skill.ident() == *this ) {
            return skill;
        }
    }

    const auto iter = Skill::contextual_skills.find( *this );
    if( iter != Skill::contextual_skills.end() ) {
        return iter->second;
    }

    return invalid_skill;
}

/** @relates string_id */
template<>
bool string_id<Skill>::is_valid() const
{
    return &obj() != &invalid_skill;
}

Skill::Skill() : Skill( skill_id::NULL_ID(), to_translation( "nothing" ),
                            to_translation( "The zen-most skill there is." ),
                            std::set<std::string> {}, skill_displayType_id::NULL_ID() )
{
}

Skill::Skill( const skill_id &ident, const translation &name, const translation &description,
              const std::set<std::string> &tags, skill_displayType_id display_type )
    : _name( name ), _description( description ), _tags( tags ), _display_type( display_type ),
      id( ident )
{
}

std::vector<const Skill *> Skill::get_skills_for_chr_display(
    Character &chr, std::function<bool ( const Skill &, const Skill & )> pred )
{
    std::vector<const Skill *> result;
    result.reserve( skills.size() );

    for( const Skill &sk : skills ) {
        if( !sk.obsolete() && sk.can_chr_use( chr ) ) {
            result.push_back( &sk );
        }
    }

    std::sort( begin( result ), end( result ), [&]( const Skill * lhs, const Skill * rhs ) {
        return pred( *lhs, *rhs );
    } );

    return result;
}

std::vector<const Skill *> Skill::get_skills_sorted_by(
    std::function<bool ( const Skill &, const Skill & )> pred )
{
    std::vector<const Skill *> result;
    result.reserve( skills.size() );

    for( const Skill &sk : skills ) {
        if( !sk.obsolete() ) {
            result.push_back( &sk );
        }
    }

    std::sort( begin( result ), end( result ), [&]( const Skill * lhs, const Skill * rhs ) {
        return pred( *lhs, *rhs );
    } );

    return result;
}

void Skill::reset()
{
    skill_factory.reset();
    skills.clear();
    contextual_skills.clear();
    next_skill_load_order = 0;
}

void Skill::check_consistency()
{
    for( Skill &skill : skills )  {
        auto rt = skill._requires_all_traits.begin();
        while( rt != skill._requires_all_traits.end() ) {
            auto current = rt++;
            if( !trait_id( *current ).is_valid() ) {
                debugmsg( "trait %s does not exist", current->c_str() );
                rt = skill._requires_all_traits.erase( current );
            }
        }

        rt = skill._requires_any_traits.begin();
        while( rt != skill._requires_any_traits.end() ) {
            auto current = rt++;
            if( !trait_id( *current ).is_valid() ) {
                debugmsg( "trait %s does not exist", current->c_str() );
                rt = skill._requires_any_traits.erase( current );
            }
        }
    }
}

void Skill::load_skill( const JsonObject &jsobj, const std::string &src_name )
{
    skill_factory.load( jsobj, src_name );
    update_skill_lists();
}

void Skill::finalize_all()
{
    skill_factory.finalize();
    update_skill_lists();
}

void Skill::update_skill_lists()
{
    skills.clear();
    std::set<skill_id> contextual_ids;
    for( const Skill &sk : skill_factory.get_all() ) {
        if( sk.is_contextual_skill() ) {
            // assigned in place, so references to the skills that stay remain valid
            contextual_skills[sk.ident()] = sk;
            contextual_ids.insert( sk.ident() );
        } else {
            skills.push_back( sk );
        }
    }
    for( auto it = contextual_skills.begin(); it != contextual_skills.end(); ) {
        it = contextual_ids.count( it->first ) > 0 ? std::next( it ) : contextual_skills.erase( it );
    }
    std::stable_sort( skills.begin(), skills.end(), []( const Skill & a, const Skill & b ) {
        return a._load_order < b._load_order;
    } );
}

// A copy (or a later mod's "copy-from" of the same id) keeps every member it doesn't name
// (DATA-J02).
void Skill::load( const JsonObject &jsobj, std::string_view )
{
    _load_order = next_skill_load_order++;

    optional( jsobj, was_loaded, "name", _name );
    optional( jsobj, was_loaded, "description", _description );
    if( jsobj.has_member( "companion_skill_practice" ) ) {
        _companion_skill_practice.clear();
        for( JsonObject jo_csp : jsobj.get_array( "companion_skill_practice" ) ) {
            _companion_skill_practice.emplace( jo_csp.get_string( "skill" ), jo_csp.get_int( "weight" ) );
        }
    }
    const auto read_level_descriptions = [&jsobj]( const std::string_view member,
    std::map<int, translation> &descriptions ) {
        if( !jsobj.has_member( member ) ) {
            return;
        }
        descriptions.clear();
        for( JsonObject jo : jsobj.get_array( member ) ) {
            translation desc;
            jo.read( "description", desc );
            descriptions.emplace( jo.get_int( "level" ), desc );
        }
    };
    read_level_descriptions( "level_descriptions_theory", _level_descriptions_theory );
    read_level_descriptions( "level_descriptions_practice", _level_descriptions_practice );
    // a copy's "time_to_attack" changes the members it names
    if( !was_loaded ) {
        _time_to_attack = time_info_t();
    }
    if( jsobj.has_object( "time_to_attack" ) ) {
        JsonObject jso_tta = jsobj.get_object( "time_to_attack" );
        jso_tta.read( "min_time", _time_to_attack.min_time );
        jso_tta.read( "base_time", _time_to_attack.base_time );
        jso_tta.read( "time_reduction_per_level", _time_to_attack.time_reduction_per_level );
    }
    mandatory( jsobj, was_loaded, "display_category", _display_type );
    optional( jsobj, was_loaded, "tags", _tags, string_reader{} );
    if( jsobj.has_int( "sort_rank" ) ) {
        _sort_rank = jsobj.get_int( "sort_rank" );
    } else if( !was_loaded ) {
        _sort_rank = 1000000;
        debugmsg( "skill '%s' missing 'sort_rank' field.", id.str() );
    }

    optional( jsobj, was_loaded, "consumes_focus", consumes_focus, true );
    optional( jsobj, was_loaded, "companion_combat_rank_factor", _companion_combat_rank_factor, 0 );
    optional( jsobj, was_loaded, "companion_survival_rank_factor", _companion_survival_rank_factor,
              0 );
    optional( jsobj, was_loaded, "companion_industry_rank_factor", _companion_industry_rank_factor,
              0 );
    optional( jsobj, was_loaded, "obsolete", _obsolete, false );
    optional( jsobj, was_loaded, "teachable", _teachable, true );
    optional( jsobj, was_loaded, "training_time", _training_time );
    optional( jsobj, was_loaded, "training_cost", _training_cost );
    optional( jsobj, was_loaded, "max_level", _max_level, MAX_SKILL );
    if( _max_level < 0 || _max_level > MAX_SKILL ) {
        jsobj.throw_error_at( "max_level", string_format( "max_level must be between 0 and %d",
                              MAX_SKILL ) );
    }
    optional( jsobj, was_loaded, "requires_all_traits", _requires_all_traits, string_reader{} );
    optional( jsobj, was_loaded, "requires_any_traits", _requires_any_traits, string_reader{} );

    // How the skill progresses (PROG-J08); a copy's "rust" changes the members it names
    optional( jsobj, was_loaded, "training_multiplier", _training_multiplier, 1.0f );
    optional( jsobj, was_loaded, "catchup_multiplier", _catchup_multiplier, 1.0f );
    optional( jsobj, was_loaded, "book_learning_multiplier", _book_learning_multiplier, 1.0f );
    for( const char *member : {
             "training_multiplier", "catchup_multiplier", "book_learning_multiplier"
         } ) {
        if( jsobj.has_member( member ) && jsobj.get_float( member ) < 0 ) {
            jsobj.throw_error_at( member, "must not be negative" );
        }
    }
    if( !was_loaded ) {
        _rusts = true;
        _rust_grace_period = 24_hours;
        _rust_multiplier = 1.0f;
        _rust_max_levels = 3.0f;
    }
    if( jsobj.has_object( "rust" ) ) {
        JsonObject jo_rust = jsobj.get_object( "rust" );
        jo_rust.read( "enabled", _rusts );
        jo_rust.read( "grace_period", _rust_grace_period );
        jo_rust.read( "multiplier", _rust_multiplier );
        jo_rust.read( "max_levels", _rust_max_levels );
        if( _rust_grace_period < 0_turns ) {
            jo_rust.throw_error_at( "grace_period", "must not be negative" );
        }
        if( _rust_multiplier < 0 ) {
            jo_rust.throw_error_at( "multiplier", "must not be negative" );
        }
        if( _rust_max_levels < 0 ) {
            jo_rust.throw_error_at( "max_levels", "must not be negative" );
        }
    }

    // How crafting uses it (UIINV-J04); the defaults are the built-in rules
    optional( jsobj, was_loaded, "vision_penalty", _vision_penalty, 0.0f );
    if( _vision_penalty < 0 ) {
        jsobj.throw_error_at( "vision_penalty", "must not be negative" );
    }
    if( jsobj.has_member( "crafting_stat" ) ) {
        static const std::array<std::pair<std::string_view, scaling_stat>, 4> crafting_stats = { {
                { "str", STAT_STR }, { "dex", STAT_DEX }, { "int", STAT_INT }, { "per", STAT_PER }
            }
        };
        const std::string stat = jsobj.get_string( "crafting_stat" );
        const auto found = std::find_if( crafting_stats.begin(), crafting_stats.end(),
        [&stat]( const std::pair<std::string_view, scaling_stat> &entry ) {
            return entry.first == stat;
        } );
        if( found == crafting_stats.end() ) {
            jsobj.throw_error_at( "crafting_stat", R"(must be "str", "dex", "int" or "per")" );
        }
        _crafting_stat = found->second;
    } else if( !was_loaded ) {
        _crafting_stat = STAT_INT;
    }
    optional( jsobj, was_loaded, "crafting_stat_divisor", _crafting_stat_divisor, 4.0f );
    if( _crafting_stat_divisor <= 0 ) {
        jsobj.throw_error_at( "crafting_stat_divisor", "must be more than 0" );
    }
}

SkillDisplayType::SkillDisplayType() : SkillDisplayType( skill_displayType_id::NULL_ID(),
            to_translation( "invalid" ) )
{
}

SkillDisplayType::SkillDisplayType( const skill_displayType_id &ident,
                                    const translation &display_string )
    : _ident( ident ), _display_string( display_string )
{
}

/** @relates string_id */
template<>
const SkillDisplayType &skill_displayType_id::obj() const
{
    for( const SkillDisplayType &skill : SkillDisplayType::skillTypes ) {
        if( skill.ident() == *this ) {
            return skill;
        }
    }

    return invalid_skill_type;
}

void SkillDisplayType::load( const JsonObject &jsobj )
{
    skill_displayType_id ident = skill_displayType_id( jsobj.get_string( "id" ) );
    skillTypes.erase( std::remove_if( begin( skillTypes ),
    end( skillTypes ), [&]( const SkillDisplayType & s ) {
        return s._ident == ident;
    } ), end( skillTypes ) );

    translation display_string;
    jsobj.read( "display_string", display_string );
    const SkillDisplayType sk( ident, display_string );
    skillTypes.push_back( sk );
}

const SkillDisplayType &SkillDisplayType::get_skill_type( const skill_displayType_id &id )
{
    for( SkillDisplayType &i : skillTypes ) {
        if( i._ident == id ) {
            return i;
        }
    }
    return invalid_skill_type;
}

skill_id Skill::from_legacy_int( const int legacy_id )
{
    static const std::array<skill_id, 28> legacy_skills = { {
            skill_id::NULL_ID(), skill_dodge, skill_melee, skill_unarmed,
            skill_bashing, skill_cutting, skill_stabbing, skill_throw,
            skill_gun, skill_pistol, skill_shotgun, skill_smg,
            skill_rifle, skill_archery, skill_launcher, skill_mechanics,
            skill_electronics, skill_cooking, skill_tailor, skill_id::NULL_ID(),
            skill_firstaid, skill_speech, skill_computer,
            skill_survival, skill_traps, skill_swimming, skill_driving,
        }
    };
    if( static_cast<size_t>( legacy_id ) < legacy_skills.size() ) {
        return legacy_skills[legacy_id];
    }
    debugmsg( "legacy skill id %d is invalid", legacy_id );
    return skills.front().ident(); // return a non-null id because callers might not expect a null-id
}

skill_id Skill::random_skill()
{
    return random_entry_ref( skills ).ident();
}

bool Skill::can_chr_use( Character &chr ) const
{
    for( std::string tid : _requires_all_traits ) {
        if( !chr.has_trait( trait_id( tid ) ) ) {
            return false;
        }
    }

    if( !_requires_any_traits.empty() ) {
        for( std::string tid : _requires_any_traits ) {
            if( chr.has_trait( trait_id( tid ) ) ) {
                return true;
            }
        }
        return false;
    }

    return true;
}

// used for the pacifist trait
bool Skill::is_combat_skill() const
{
    static const std::string combat_skill( "combat_skill" );
    return _tags.count( combat_skill ) > 0;
}

bool Skill::is_contextual_skill() const
{
    static const std::string contextual_skill( "contextual_skill" );
    return _tags.count( contextual_skill ) > 0;
}

std::string Skill::get_level_description( int skill_lvl, bool practical ) const
{
    std::map<int, translation> description_map;

    if( practical ) {
        description_map = _level_descriptions_practice;
    } else {
        description_map = _level_descriptions_theory;
    }

    auto it = description_map.upper_bound( skill_lvl );
    if( it != description_map.begin() ) {
        --it;
    } else {
        return "";
    }
    return it->second.translated();
}

void SkillLevel::train( int amount, float catchup_modifier, float knowledge_modifier,
                        bool allow_multilevel )
{
    if( amount < 0 ) {
        debugmsg( "train() called with negative xp: %d", amount );
        return;
    }
    // catchup gets faster the higher the level gap gets.
    float level_gap = 1.0f * std::max( knowledgeLevel(), 1 ) / std::max( level(), 1 );
    float catchup_amount = amount * catchup_modifier;
    float knowledge_amount = amount * knowledge_modifier;
    if( knowledgeLevel() > level() ) {
        catchup_amount *= level_gap;
    } else if( knowledgeLevel() == level() && _knowledgeExperience > _exercise ) {
        // when you're in the same level, the catchup starts to slow down.
        catchup_amount = amount * std::max( catchup_modifier - 1.0f * _exercise / _knowledgeExperience,
                                            1.0f );
        knowledge_amount = amount * std::max( knowledge_modifier - 0.1f * _exercise / _knowledgeExperience,
                                              1.0f );
    } else {
        // When your two xp's are equal just do the basic thing.
        catchup_amount = amount * 1.0f;
        knowledge_amount = amount * 1.0f;
    }

    // Learning knowledge faster than skill, when you're actually practicing, will generate some annoying problems.
    if( knowledge_amount > catchup_amount * 0.9f ) {
        knowledge_amount = catchup_amount * 0.9f;
    }

    if( unadjustedKnowledgeLevel() >= MAX_SKILL ) {
        knowledge_amount = 0;
    }

    const double scaling = get_option<float>( "SKILL_TRAINING_SPEED" );
    if( scaling > 0.0 ) {
        catchup_amount *= scaling;
        knowledge_amount *= scaling;
    }
    _exercise += catchup_amount;
    if( _exercise < 0 ) {
        debugmsg( "integer overflow in train() amount=%d catchup_modifier=%g knowledge_modifier=%g level_gap=%g catchup_amount=%g knowledge_amount=%g scaling=%g _exercise=%d",
                  amount, catchup_modifier, knowledge_modifier, level_gap, catchup_amount, knowledge_amount, scaling,
                  _exercise );
        _exercise -= catchup_amount;
        return;
    }
    _rustAccumulator -= catchup_amount;
    _knowledgeExperience += knowledge_amount;

    on_exercise_change( allow_multilevel );
    practice();
}

void SkillLevel::knowledge_train( int amount, int npc_knowledge )
{
    float level_gap = 1.0f;
    // when your _level is the same or 1 level below your knowledge, gain xp at the normal rate.
    // as your practical skill lags behind your knowledge, it gets harder to contextualize that
    // theoretical knowledge, and your ability to learn the theory gets slower.

    // The same formula applies to NPCs teaching you, but in that case the level decreases as their knowledge
    // level exceeds your own.  The best teacher is one who is only somewhat more knowledgeable than you.
    if( npc_knowledge > 0 ) {
        // This should later be modified by NPC teaching proficiencies.
        level_gap = std::max<float>( npc_knowledge - unadjustedKnowledgeLevel(), 1.0f );
    } else {
        // Some day this should be affected by json specific to the skill, some skills are more amenable
        // to book learning.
        level_gap = std::max<float>( unadjustedKnowledgeLevel() - unadjustedLevel(), 1.0f );
    }
    float level_mult = 2.0f / ( level_gap + 1.0f );
    amount *= level_mult;

    const double scaling = get_option<float>( "SKILL_TRAINING_SPEED" );
    if( scaling > 0.0 ) {
        amount = std::ceil( amount * scaling );
    }
    _knowledgeExperience += amount;

    if( _knowledgeExperience >= 10000 * pow( unadjustedKnowledgeLevel() + 1, 2U ) ) {
        _knowledgeExperience = 0;
        ++_knowledgeLevel;
    }

}

bool SkillLevel::isRusty() const
{
    // skill is considered rusty if the practical xp lags knowledge xp by at least 1%
    return level() != knowledgeLevel() ||
           _knowledgeExperience - _exercise >= pow( level() + 1, 2U ) * 10;
}

bool SkillLevel::rust( int rust_resist, float rust_multiplier, const time_duration &grace_period,
                       float max_levels )
{
    if( ( calendar::turn - _lastPracticed ) < grace_period ) {
        // don't rust within the grace period
        return false;
    }

    if( unadjustedLevel() >= MAX_SKILL ) {
        // don't rust any more once you hit the level cap, at least until we have a way to "pause" rust for a while.
        return false;
    }

    const int level_multiplier = pow( unadjustedLevel() + 1, 2U );
    float level_exp = level_multiplier * 10000.0f;
    if( _rustAccumulator > level_exp * max_levels ) {
        // at this point the numbers ahead will be too small to bother.  Just cap it off.
        return false;
    }

    // Future plans: Have rust_slowdown impacted by intelligence and other memory-affecting things
    float rust_slowdown = std::max( static_cast<float>( std::sqrt( _rustAccumulator / level_exp ) ),
                                    0.04f );

    // rust amount starts at 4% of a level's xp, run every 24 hours.
    // Once the accumulated rust exceeds 16% of a level, rust_amount starts to drop.
    int rust_amount = level_multiplier * rust_multiplier * 16 / rust_slowdown;

    if( rust_resist > 0 ) {
        rust_amount = std::lround( rust_amount * ( std::max( ( 100 - rust_resist ), 0 ) / 100.0 ) );
    }

    if( level() == 0 ) {
        rust_amount = std::min( rust_amount, _exercise );
    }

    if( rust_amount <= 0 ) {
        return false;
    }

    _rustAccumulator += rust_amount;
    _exercise -= rust_amount;
    on_exercise_change();

    return false;
}

void SkillLevel::practice()
{
    _lastPracticed = calendar::turn;
}

void SkillLevel::readBook( int minimumGain, int maximumGain, int maximumLevel, float multiplier )
{
    if( knowledgeLevel() < maximumLevel || maximumLevel < 0 ) {
        int amount = ( knowledgeLevel() + 1 ) * rng( minimumGain, maximumGain ) * 100;
        if( multiplier != 1.0f ) {
            amount = static_cast<int>( std::lround( amount * multiplier ) );
        }
        knowledge_train( amount );
    }

    practice();
}

bool SkillLevel::can_train() const
{
    return get_option<float>( "SKILL_TRAINING_SPEED" ) > 0.0;
}

void SkillLevel::set_exercise( int value, bool raw )
{
    _exercise = raw ? value : value * ( 100 * ( level() + 1 ) * ( level() + 1 ) );
    on_exercise_change( true );
}

void SkillLevel::set_knowledge_exercise( int value, bool raw )
{
    const auto xp_to_level = [this]() {
        return 100 * 100 * pow( unadjustedKnowledgeLevel() + 1, 2U );
    };
    _knowledgeExperience = std::max( raw ? value : value * ( 100 * ( _knowledgeLevel + 1 ) *
                                     ( _knowledgeLevel + 1 ) ), 0 );
    while( _knowledgeExperience >= xp_to_level() && unadjustedKnowledgeLevel() < MAX_SKILL ) {
        _knowledgeExperience -= xp_to_level();
        ++_knowledgeLevel;
    }
    if( unadjustedKnowledgeLevel() >= MAX_SKILL ) {
        _knowledgeExperience = std::min<int>( _knowledgeExperience, xp_to_level() - 1 );
    }
    // Theory doesn't lag the practice it comes from
    if( level() == knowledgeLevel() && _knowledgeExperience < _exercise ) {
        _knowledgeExperience = _exercise;
    }
}

void SkillLevel::on_exercise_change( bool allow_multilevel )
{
    if( _exercise < 0 ) {
        if( unadjustedLevel() <= 0 ) {
            // There is no level below 0 to drop to; a negative level makes exercise()
            // divide by zero.
            _exercise = 0;
        } else {
            _exercise = ( 100 * 100 * pow( unadjustedLevel(), 2U ) ) - 1;
            --_level;
        }
    } else {
        const auto xp_to_level = [&]() {
            return 100 * 100 * pow( unadjustedLevel() + 1, 2U );
        };
        while( _exercise >= xp_to_level() ) {
            _exercise = allow_multilevel ? _exercise - xp_to_level() : 0;
            ++_level;
            if( unadjustedLevel() > unadjustedKnowledgeLevel() ) {
                _knowledgeLevel = _level;
                _knowledgeExperience = 0;
            }
        }

        if( _rustAccumulator < 0 ) {
            _rustAccumulator = 0;
        }
        if( level() == knowledgeLevel() && _exercise > _knowledgeExperience ) {
            _knowledgeExperience = _exercise;
        }

        if( _knowledgeExperience >= 10000 * pow( unadjustedKnowledgeLevel() + 1, 2U ) ) {
            _knowledgeExperience = 0;
            ++_knowledgeLevel;
        }
    }
}

const SkillLevel &SkillLevelMap::get_skill_level_object( const skill_id &ident ) const
{
    static const SkillLevel null_skill{};

    if( ident && ident->is_contextual_skill() ) {
        debugmsg( "Skill \"%s\" is context-dependent.  It cannot be assigned.", ident.str() );
        return null_skill;
    }

    const auto iter = find( ident );

    if( iter != end() ) {
        return iter->second;
    }

    return null_skill;
}

SkillLevel &SkillLevelMap::get_skill_level_object( const skill_id &ident )
{
    static SkillLevel null_skill;

    if( ident && ident->is_contextual_skill() ) {
        debugmsg( "Skill \"%s\" is context-dependent.  It cannot be assigned.", ident.str() );
        return null_skill;
    }

    return ( *this )[ident];
}

void SkillLevelMap::mod_skill_level( const skill_id &ident, int delta )
{
    SkillLevel &obj = get_skill_level_object( ident );
    // Levels below 0 make exercise() divide by zero.
    obj.level( std::max( obj.level() + delta, 0 ) );
}

void SkillLevelMap::mod_knowledge_level( const skill_id &ident, int delta )
{
    SkillLevel &obj = get_skill_level_object( ident );
    obj.knowledgeLevel( std::max( obj.knowledgeLevel() + delta, 0 ) );
}

int SkillLevelMap::get_skill_level( const skill_id &ident ) const
{
    return get_skill_level_object( ident ).level();
}

int SkillLevelMap::get_skill_level( const skill_id &ident, const item &context ) const
{
    const skill_id id = context.is_null() ? ident : context.contextualize_skill( ident );
    return get_skill_level( id );
}

float SkillLevelMap::get_progress_level( const skill_id &ident ) const
{
    return static_cast<float>( get_skill_level_object( ident ).exercise() ) / 100.0f;
}

float SkillLevelMap::get_progress_level( const skill_id &ident, const item &context ) const
{
    const skill_id id = context.is_null() ? ident : context.contextualize_skill( ident );
    return get_progress_level( id );
}

int SkillLevelMap::get_knowledge_level( const skill_id &ident ) const
{
    return get_skill_level_object( ident ).knowledgeLevel();
}

int SkillLevelMap::get_knowledge_level( const skill_id &ident, const item &context ) const
{
    const skill_id id = context.is_null() ? ident : context.contextualize_skill( ident );
    return get_knowledge_level( id );
}

float SkillLevelMap::get_knowledge_progress_level( const skill_id &ident ) const
{
    return static_cast<float>( get_skill_level_object( ident ).knowledgeExperience() ) / 100.0f;
}

float SkillLevelMap::get_knowledge_progress_level( const skill_id &ident,
        const item &context ) const
{
    const skill_id id = context.is_null() ? ident : context.contextualize_skill( ident );
    return get_progress_level( id );
}

bool SkillLevelMap::meets_skill_requirements( const std::map<skill_id, int> &req ) const
{
    return meets_skill_requirements( req, item() );
}

bool SkillLevelMap::meets_skill_requirements( const std::map<skill_id, int> &req,
        const item &context ) const
{
    return std::all_of( req.begin(), req.end(),
    [this, &context]( const std::pair<skill_id, int> &pr ) {
        // Whether or not you meet skill requirements should be based on your level of theory training,
        // not practical experience.
        return get_knowledge_level( pr.first, context ) >= pr.second;
    } );
}

std::map<skill_id, int> SkillLevelMap::compare_skill_requirements(
    const std::map<skill_id, int> &req ) const
{
    return compare_skill_requirements( req, item() );
}

std::map<skill_id, int> SkillLevelMap::compare_skill_requirements(
    const std::map<skill_id, int> &req, const item &context ) const
{
    std::map<skill_id, int> res;

    for( const auto &elem : req ) {
        const int diff = get_skill_level( elem.first, context ) - elem.second;
        if( diff != 0 ) {
            res[elem.first] = diff;
        }
    }

    return res;
}

int SkillLevelMap::exceeds_recipe_requirements( const recipe &rec ) const
{
    int over = rec.skill_used ? get_skill_level( rec.skill_used ) - rec.difficulty : 0;
    for( const auto &elem : compare_skill_requirements( rec.required_skills ) ) {
        over = std::min( over, elem.second );
    }
    return over;
}

bool SkillLevelMap::theoretical_recipe_requirements( const recipe &rec ) const
{
    // Regardless of your current practical skill, do you know the theory of how to make this thing?
    if( rec.skill_used && get_knowledge_level( rec.skill_used ) < rec.difficulty ) {
        return false;
    }
    for( const std::pair<const skill_id, int> &elem : rec.required_skills ) {
        if( get_knowledge_level( elem.first ) < elem.second ) {
            return false;
        }
    }
    return true;
}

bool SkillLevelMap::has_recipe_requirements( const recipe &rec ) const
{
    return exceeds_recipe_requirements( rec ) >= 0 || theoretical_recipe_requirements( rec );
}

bool SkillLevelMap::has_same_levels_as( const SkillLevelMap &other ) const
{
    if( this->size() != other.size() ) {
        return false;
    }
    for( const auto &entry : *this ) {
        const SkillLevel &this_level = entry.second;
        if( other.count( entry.first ) == 0 ) {
            return false;
        }
        const SkillLevel &other_level = other.get_skill_level_object( entry.first );
        if( this_level.level() != other_level.level() ||
            this_level.knowledgeLevel() != other_level.knowledgeLevel() ) {
            return false;
        }
    }
    return true;
}

// Actually take the difference in social skill between the two parties involved
// Caps at 200% when you are 5 levels ahead, int comparison is handled in npctalk.cpp
double price_adjustment( int barter_skill )
{
    int const skill = std::min( 5, std::abs( barter_skill ) );
    double const val = 0.045 * std::pow( skill, 2 ) - 0.025 * skill + 1;
    return barter_skill >= 0 ? val : 1 / val;
}
