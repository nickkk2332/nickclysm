#include "talker_item.h"

#include <climits>
#include <functional>
#include <list>
#include <vector>

#include "ammo.h"
#include "bodypart.h"
#include "cata_utility.h"
#include "character.h"
#include "coordinates.h"
#include "debug.h"
#include "flag.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "magic_enchantment.h"
#include "map.h"
#include "math_parser_diag_value.h"
#include "messages.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "units.h"
#include "value_ptr.h"
#include "visitable.h"

static const ammotype ammo_battery( "battery" );

namespace
{
// MATH-J07: an item's power is the battery charge it holds; other ammo (a gun's rounds) isn't
int battery_charges( const item &it )
{
    return it.ammo_capacity( ammo_battery ) > 0 ? it.ammo_remaining() : 0;
}

// The item, or nullptr once it is gone: an EOC can still hold the talker of an item that was used
// up (taken apart, crafted with, destroyed) earlier in the same EOC.
const item *alive( const item_location *loc )
{
    return loc != nullptr ? loc->get_item() : nullptr;
}

item *alive( item_location *loc )
{
    return loc != nullptr ? loc->get_item() : nullptr;
}

// a sum of counts that stops at INT_MAX instead of wrapping
int add_capped( int a, int b )
{
    return b > 0 && a > INT_MAX - b ? INT_MAX : a + b;
}
} // namespace

std::string talker_item_const::disp_name() const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->display_name() : std::string();
}

std::string talker_item_const::get_name() const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->type_name() : std::string();
}

// An item that is gone has no position: these give the invalid point, and say nothing.
int talker_item_const::posx( const map &here ) const
{
    return pos_bub( here ).x();
}

int talker_item_const::posy( const map &here ) const
{
    return pos_bub( here ).y();
}

int talker_item_const::posz() const
{
    return pos_abs().z();
}

tripoint_bub_ms talker_item_const::pos_bub( const map &here ) const
{
    return alive( me_it_const ) != nullptr ? me_it_const->pos_bub( here ) : tripoint_bub_ms::invalid;
}

tripoint_abs_ms talker_item_const::pos_abs() const
{
    return alive( me_it_const ) != nullptr ? me_it_const->pos_abs() : tripoint_abs_ms::invalid;
}

tripoint_abs_omt talker_item_const::pos_abs_omt() const
{
    return alive( me_it_const ) != nullptr ? project_to<coords::omt>( pos_abs() ) :
           tripoint_abs_omt::invalid;
}

diag_value const *talker_item_const::maybe_get_value( const std::string &var_name ) const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->maybe_get_value( var_name ) : nullptr;
}

bool talker_item_const::has_flag( const flag_id &f ) const
{
    const item *it = alive( me_it_const );
    if( it == nullptr ) {
        return false;
    }
    add_msg_debug( debugmode::DF_TALKER, "Item %s checked for flag %s", it->tname(), f.c_str() );
    return it->has_flag( f );
}

std::vector<std::string> talker_item_const::get_topics( bool ) const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->typeId()->chat_topics : std::vector<std::string>();
}

bool talker_item_const::will_talk_to_u( const Character &you, bool ) const
{
    return !you.is_dead_state();
}

int talker_item_const::get_cur_hp( const bodypart_id & ) const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->max_damage() - it->damage() : 0;
}

int talker_item_const::get_degradation() const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->degradation() : 0;
}

int talker_item_const::get_hp_max( const bodypart_id & ) const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->max_damage() : 0;
}

// Item power is battery charges, counted as 1 mJ each. Only battery is power: a lamp-oil or
// arrow tool has none, as power_max() says.
units::energy talker_item_const::power_cur() const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? 1_mJ * battery_charges( *it ) : 0_mJ;
}

units::energy talker_item_const::power_max() const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? 1_mJ * it->ammo_capacity( ammo_battery, false, me_it_const->carrier() ) :
           0_mJ;
}

int talker_item_const::get_artifact_resonance() const
{
    const item *it = alive( me_it_const );
    if( it == nullptr ) {
        return 0;
    }
    int ret = 0;
    for( enchant_cache &this_ench : it->get_proc_enchantments() ) {
        ret += this_ench.get_value_add( enchant_vals::mod::ARTIFACT_RESONANCE );
    }
    return ret;
}

int talker_item_const::get_count() const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->count() : 0;
}

int talker_item_const::coverage_at( bodypart_id &id ) const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->get_coverage( id ) : 0;
}

int talker_item_const::encumbrance_at( bodypart_id &id ) const
{
    const item *it = alive( me_it_const );
    if( it == nullptr ) {
        return 0;
    }
    // for the one wearing or carrying it, or else the avatar
    const Character *holder = me_it_const->carrier();
    return it->get_encumber( holder != nullptr ? *holder : get_player_character(), id );
}

int talker_item_const::get_volume() const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? units::to_milliliter( it->volume() ) : 0;
}

units::mass talker_item_const::get_weight() const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->weight() : 0_gram;
}

int talker_item_const::get_quality( const std::string &quality, bool strict ) const
{
    return alive( me_it_const ) != nullptr ? me_it_const->get_quality( quality, strict ) : 0;
}

void talker_item::set_value( const std::string &var_name, diag_value const &value )
{
    if( item *it = alive( me_it ) ) {
        it->set_var( var_name, value );
    }
}

void talker_item::remove_value( const std::string &var_name )
{
    if( item *it = alive( me_it ) ) {
        it->erase_var( var_name );
    }
}

void talker_item::set_power_cur( units::energy value )
{
    item *it = alive( me_it );
    if( it == nullptr ) {
        return;
    }
    // Only an item that holds battery charges has power to set.  On a gun loaded with bullets
    // the capacity is 0, and setting 0 battery charges unloaded it; a bionic powered tool
    // without a battery draws on its user's bionics.
    const int capacity = it->ammo_capacity( ammo_battery );
    if( capacity <= 0 || ( it->has_flag( flag_USES_BIONIC_POWER ) &&
                           it->magazines_current().empty() ) ) {
        return;
    }
    // keep the battery ammo it holds (a mod's own kind), or else the battery type's default
    const itype *loaded = it->ammo_data();
    const itype_id ammo = loaded != nullptr && loaded->ammo && loaded->ammo->type == ammo_battery ?
                          loaded->get_id() : ammo_battery->default_ammotype();
    it->ammo_set( ammo, clamp( static_cast<int>( value.value() ), 0, capacity ) );
}

void talker_item::set_all_parts_hp_cur( int set )
{
    if( item *it = alive( me_it ) ) {
        it->set_damage( it->max_damage() - set );
    }
}

void talker_item::set_degradation( int set )
{
    if( item *it = alive( me_it ) ) {
        it->set_degradation( set );
    }
}

void talker_item::die( map * )
{
    if( alive( me_it ) != nullptr ) {
        me_it->remove_item();
    }
}

void talker_item::set_fault( const fault_id &fault_id, bool force, const Character *holder )
{
    if( item *it = alive( me_it ) ) {
        it->set_fault( fault_id, force, holder );
    }
}

void talker_item::set_random_fault_of_type( const std::string &fault_type, bool force,
        const Character *holder )
{
    if( item *it = alive( me_it ) ) {
        it->set_random_fault_of_type( fault_type, force, holder );
    }
}

int talker_item_const::get_price() const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->price( false ) : 0;
}

int talker_item_const::get_price_postapoc() const
{
    const item *it = alive( me_it_const );
    return it != nullptr ? it->price( true ) : 0;
}

// INV-J07: an item talker's inventory is what the item holds (its container, magazine and
// magazine well pockets, and whatever is nested in their containers); the item itself doesn't
// count. A bag answers how many bandages are in it, a flashlight how much battery it holds.

int talker_item_const::charges_of( const itype_id &item_id ) const
{
    const item *it = alive( me_it_const );
    if( it == nullptr ) {
        return 0;
    }
    int total = 0;
    for( const item *content : it->all_items_top() ) {
        total = add_capped( total, content->charges_of( item_id ) );
    }
    return total;
}

bool talker_item_const::has_charges( const itype_id &item_id, int count ) const
{
    return charges_of( item_id ) >= count;
}

bool talker_item_const::has_charges( const itype_id &item_id, int count, bool in_tools ) const
{
    if( !in_tools ) {
        return has_charges( item_id, count );
    }
    const item *it = alive( me_it_const );
    if( it == nullptr ) {
        return count <= 0;
    }
    int total = 0;
    for( const item *content : it->all_items_top() ) {
        total = add_capped( total, content->charges_of( item_id, INT_MAX, return_true<item>,
                            nullptr, true ) );
    }
    return total >= count;
}

bool talker_item_const::has_amount( const itype_id &item_id, int count ) const
{
    return get_amount( item_id ) >= count;
}

int talker_item_const::get_amount( const itype_id &item_id ) const
{
    const item *it = alive( me_it_const );
    if( it == nullptr ) {
        return 0;
    }
    int total = 0;
    for( const item *content : it->all_items_top() ) {
        total = add_capped( total, content->amount_of( item_id ) );
    }
    return total;
}

bool talker_item_const::has_item_with_flag( const flag_id &flag ) const
{
    const item *it = alive( me_it_const );
    if( it == nullptr ) {
        return false;
    }
    return it->has_item_with( [it, &flag]( const item & content ) {
        return &content != it && content.has_flag( flag );
    } );
}

std::vector<const item *> talker_item_const::const_items_with( const
        std::function<bool( const item & )> &filter ) const
{
    const item *it = alive( me_it_const );
    if( it == nullptr ) {
        return {};
    }
    return it->items_with( [it, &filter]( const item & content ) {
        return &content != it && filter( content );
    } );
}

std::vector<item *> talker_item::items_with( const std::function<bool( const item & )> &filter )
{
    std::vector<item *> res;
    item *it = alive( me_it );
    if( it == nullptr ) {
        return res;
    }
    it->visit_items( [it, &filter, &res]( item * content, item * ) {
        if( content != it && filter( *content ) ) {
            res.push_back( content );
        }
        return VisitResponse::NEXT;
    } );
    return res;
}

void talker_item::i_add( const item &new_item )
{
    item *it = alive( me_it );
    if( it == nullptr ) {
        return;
    }
    item payload( new_item );
    // the pockets holding this item must have room for what goes into it too
    if( it->is_container() && me_it->parents_can_contain_recursive( &payload ).success() &&
        it->put_in( payload, pocket_type::CONTAINER, false, nullptr, true ).success() ) {
        me_it->on_contents_changed();
        return;
    }
    map &here = get_map();
    const tripoint_bub_ms pos = me_it->pos_bub( here );
    if( here.inbounds( pos ) ) {
        here.add_item_or_charges( pos, payload );
    }
}

void talker_item::i_add_or_drop( item &new_item, bool )
{
    i_add( new_item );
}

void talker_item::remove_items_with( const std::function<bool( const item & )> &filter )
{
    item *it = alive( me_it );
    if( it == nullptr ) {
        return;
    }
    int count = INT_MAX;
    std::list<item> removed;
    it->remove_internal( [&filter]( item & content ) {
        return filter( content );
    }, count, removed );
    if( !removed.empty() ) {
        it->on_contents_changed();
        me_it->on_contents_changed();
    }
}
