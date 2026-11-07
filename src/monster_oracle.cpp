#include <string>

#include "behavior.h"
#include "coordinates.h"
#include "item.h"
#include "map.h"
#include "map_iterator.h"
#include "mapdata.h"
#include "monster.h"
#include "monster_oracle.h"
#include "type_id.h"

namespace behavior
{

const Creature *monster_oracle_t::creature() const
{
    return subject;
}

const Creature *monster_oracle_t::target() const
{
    return subject == nullptr ? nullptr : subject->attack_target();
}

status_t monster_oracle_t::not_hallucination( std::string_view ) const
{
    return subject->is_hallucination() ? status_t::failure : status_t::running;
}

status_t monster_oracle_t::split_possible( std::string_view ) const
{
    // check if subject has split to support inverting this predicate for absorb monsters without split
    if( subject->has_special( "SPLIT" ) && ( subject->get_hp() / 2 ) > subject->get_hp_max() ) {
        return status_t::running;
    }
    return status_t::failure;
}

status_t monster_oracle_t::items_available( std::string_view ) const
{
    map &here = get_map();

    if( !here.has_flag( ter_furn_flag::TFLAG_SEALED, subject->pos_bub() ) &&
        here.has_items( subject->pos_bub() ) ) {
        // The same material rule as the ABSORB_ITEMS attack (MONAI-B09)
        for( const item &it : here.i_at( subject->pos_bub() ) ) {
            if( subject->can_absorb_item( it ) ) {
                return status_t::running;
            }
        }
    }
    return status_t::failure;
}

// TODO: Have it select a target and stash it somewhere.
status_t monster_oracle_t::adjacent_plants( std::string_view ) const
{
    map &here = get_map();

    for( const tripoint_bub_ms &p : here.points_in_radius( subject->pos_bub(), 1 ) ) {
        if( here.has_flag( ter_furn_flag::TFLAG_PLANT, p ) ) {
            return status_t::running;
        }
    }
    return status_t::failure;
}

status_t monster_oracle_t::special_available( std::string_view special_name ) const
{
    bool only_if_present = false;
    std::string mspecial_name;

    if( special_name.rfind( '!', 0 ) != std::string::npos ) {
        only_if_present = true;
        mspecial_name = special_name.substr( 1 );
    } else {
        mspecial_name = special_name;
    }

    bool has_special = only_if_present ? subject->has_special( mspecial_name ) : true;

    return ( !has_special && only_if_present ) || ( has_special &&
            subject->special_available( mspecial_name ) ) ? status_t::running : status_t::failure;
}

} // namespace behavior
