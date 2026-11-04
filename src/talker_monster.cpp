#include "talker_monster.h"

#include <vector>

#include "cata_scope_helpers.h"
#include "character.h"
#include "coordinates.h"
#include "creature.h"
#include "damage.h"
#include "debug.h"
#include "effect.h"
#include "incapacitation.h"
#include "map.h"
#include "math_parser_diag_value.h"
#include "mattack_common.h"
#include "messages.h"
#include "monster.h"
#include "monstergenerator.h"
#include "mtype.h"
#include "teleport.h"
#include "units.h"

std::string talker_monster_const::disp_name() const
{
    return me_mon_const->disp_name();
}

std::string talker_monster_const::get_name() const
{
    return me_mon_const->get_name();
}

int talker_monster_const::posx( const map &here ) const
{
    return me_mon_const->posx( here );
}

int talker_monster_const::posy( const map &here ) const
{
    return me_mon_const->posy( here );
}

int talker_monster_const::posz() const
{
    return me_mon_const->posz();
}

tripoint_bub_ms talker_monster_const::pos_bub( const map &here ) const
{
    return me_mon_const->pos_bub( here );
}

tripoint_abs_ms talker_monster_const::pos_abs() const
{
    return me_mon_const->pos_abs();
}

tripoint_abs_omt talker_monster_const::pos_abs_omt() const
{
    return me_mon_const->pos_abs_omt();
}

int talker_monster_const::pain_cur() const
{
    return me_mon_const->get_pain();
}

int talker_monster_const::perceived_pain_cur() const
{
    return me_mon_const->get_perceived_pain();
}

bool talker_monster_const::has_effect( const efftype_id &effect_id, const bodypart_id &bp ) const
{
    return me_mon_const->has_effect( effect_id, bp );
}

effect talker_monster_const::get_effect( const efftype_id &effect_id, const bodypart_id &bp ) const
{
    return me_mon_const->get_effect( effect_id, bp );
}

void talker_monster::add_effect( const efftype_id &new_effect, const time_duration &dur,
                                 const std::string &bp, bool permanent, bool force,
                                 int intensity )
{
    me_mon->add_effect( new_effect, dur, bodypart_str_id( bp ), permanent, intensity, force );
}

void talker_monster::remove_effect( const efftype_id &old_effect, const std::string &bp )
{
    bodypart_id target_part;
    if( "RANDOM" == bp ) {
        target_part = get_player_character().random_body_part( true );
    } else {
        target_part = bodypart_str_id( bp );
    }
    me_mon->remove_effect( old_effect, target_part );
}

void talker_monster::mod_pain( int amount )
{
    me_mon->mod_pain( amount );
}

diag_value const *talker_monster_const::maybe_get_value( const std::string &var_name ) const
{
    return me_mon_const->maybe_get_value( var_name );
}

bool talker_monster_const::has_flag( const flag_id &f ) const
{
    add_msg_debug( debugmode::DF_TALKER, "Monster %s checked for flag %s", me_mon_const->name(),
                   f.c_str() );
    return me_mon_const->has_flag( f );
}

bool talker_monster_const::has_species( const species_id &species ) const
{
    add_msg_debug( debugmode::DF_TALKER, "Monster %s checked for species %s", me_mon_const->name(),
                   species.c_str() );
    return me_mon_const->in_species( species );
}

bool talker_monster_const::bodytype( const bodytype_id &bt ) const
{
    add_msg_debug( debugmode::DF_TALKER, "Monster %s checked for bodytype %s", me_mon_const->name(),
                   bt );
    return me_mon_const->type->bodytype == bt;
}

void talker_monster::set_value( const std::string &var_name, diag_value const &value )
{
    me_mon->set_value( var_name, value );
}

void talker_monster::remove_value( const std::string &var_name )
{
    me_mon->remove_value( var_name );
}

std::string talker_monster_const::short_description() const
{
    return me_mon_const->type->get_description();
}

int talker_monster_const::get_anger() const
{
    return me_mon_const->anger;
}

void talker_monster::set_anger( int new_val )
{
    me_mon->anger = new_val;
}

int talker_monster_const::morale_cur( bool ) const
{
    return me_mon_const->morale;
}

void talker_monster::set_morale( int new_val )
{
    me_mon->morale = new_val;
}

int talker_monster_const::get_friendly() const
{
    return me_mon_const->friendly;
}

int talker_monster_const::get_difficulty() const
{
    return me_mon_const->type->get_total_difficulty();
}

int talker_monster_const::get_size() const
{
    add_msg_debug( debugmode::DF_TALKER, "Size category of monster %s = %d", me_mon_const->name(),
                   me_mon_const->get_size() - 0 );
    return me_mon_const->get_size() - 0;
}

int talker_monster_const::get_speed() const
{
    return me_mon_const->get_speed();
}

int talker_monster_const::get_grab_strength() const
{
    add_msg_debug( debugmode::DF_TALKER, "Grab strength of monster %s = %d", me_mon_const->name(),
                   me_mon_const->get_grab_strength() );
    return  me_mon_const->get_grab_strength();
}

bool talker_monster_const::can_see_location( const tripoint_bub_ms &pos ) const
{
    const map &here = get_map();

    return me_mon_const->sees( here, pos );
}

int talker_monster_const::get_volume() const
{
    return units::to_milliliter( me_mon_const->get_volume() );
}

int talker_monster_const::get_weight() const
{
    return units::to_milligram( me_mon_const->get_weight() );
}

bool talker_monster_const::is_warm() const
{
    return me_mon_const->is_warm();
}

bool talker_monster_const::is_underwater() const
{
    return me_mon_const->is_underwater();
}

bool talker_monster_const::is_swimming() const
{
    return me_mon_const->is_swimming;
}

void talker_monster::set_friendly( int new_val )
{
    me_mon->friendly = new_val;
}

// val('pos_x') and the like: moves as u_teleport does, and falls at once over open air
void talker_monster::set_pos( tripoint_abs_ms new_pos )
{
    if( teleport::teleport_to_point( *me_mon, get_map().get_bub( new_pos ), true, false, false ) ) {
        me_mon->gravity_check();
    }
}

bool talker_monster::get_is_alive() const
{
    return !me_mon->is_dead();
}

void talker_monster::die( map *here )
{
    me_mon->die( here, nullptr );
}

void talker_monster::set_all_parts_hp_cur( int set )
{
    me_mon->set_hp( set );
}

// u_attack for a monster: with allow_special, the "technique" names a special attack, which is
// used on the target whatever its cooldown: one of the monster's own (whose cooldown restarts),
// or else any monster attack defined in JSON. Otherwise (or for any other name) a basic melee
// attack, if the target is adjacent and seen.
void talker_monster::attack_target( Creature &t, bool allow_special,
                                    const matec_id &force_technique, bool, int forced_movecost )
{
    if( me_mon->is_dead() || incapacitation::unconscious( *me_mon ) || &t == me_mon ) {
        return;
    }
    const int moves_before = me_mon->get_moves();
    const std::string &special_name = force_technique.str();
    const auto own_special = me_mon->type->special_attacks.find( special_name );
    const bool is_own = own_special != me_mon->type->special_attacks.end();
    // Hardcoded attacks are left out: some expect the monster to have them
    const mtype_special_attack *json_attack = allow_special && !is_own ?
            MonsterGenerator::generator().find_json_attack( special_name ) : nullptr;
    if( allow_special && ( is_own || json_attack != nullptr ) ) {
        // An eoc special attack can run u_attack again: stop such a loop
        static int nesting = 0;
        if( nesting >= 10 ) {
            debugmsg( "u_attack: special attack %s of %s keeps running u_attack; stopped",
                      special_name, me_mon->get_name() );
            return;
        }
        nesting++;
        on_out_of_scope end_nesting( []() {
            nesting--;
        } );
        // Special attack actors aim at the creature at the monster's destination
        me_mon->set_dest( t.pos_abs() );
        if( !is_own ) {
            ( *json_attack )->call( *me_mon );
        } else if( own_special->second->call( *me_mon ) &&
                   me_mon->special_cooldown( special_name ) ) {
            me_mon->reset_special( special_name );
        }
    } else {
        if( !me_mon->is_adjacent( &t, true ) || !me_mon->sees( get_map(), t ) ) {
            return;
        }
        me_mon->melee_attack( t );
    }
    if( forced_movecost >= 0 ) {
        me_mon->set_moves( moves_before - forced_movecost );
    }
}

dealt_damage_instance talker_monster::deal_damage( Creature *source, bodypart_id bp,
        const damage_instance &dam ) const
{
    return me_mon->deal_damage( source, bp, dam );
}

std::vector<std::string> talker_monster_const::get_topics( bool ) const
{
    return me_mon_const->type->chat_topics;
}

int talker_monster_const::get_cur_hp( const bodypart_id & ) const
{
    return me_mon_const->get_hp();
}

int talker_monster_const::get_hp_max( const bodypart_id & ) const
{
    return me_mon_const->get_hp_max();
}

double talker_monster_const::armor_at( damage_type_id &dt, bodypart_id &bp ) const
{
    return me_mon_const->get_armor_type( dt, bp );
}

bool talker_monster_const::will_talk_to_u( const Character &you, bool ) const
{
    return !you.is_dead_state();
}
