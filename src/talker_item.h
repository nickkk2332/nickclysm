#pragma once
#ifndef CATA_SRC_TALKER_ITEM_H
#define CATA_SRC_TALKER_ITEM_H

#include <memory>
#include <string>

#include "coords_fwd.h"
#include "talker.h"
#include "type_id.h"
#include "units_fwd.h"

class item_location;

/*
 * Talker wrapper class for item.
 */
class talker_item_const: public const_talker_cloner<talker_item_const>
{
    public:
        explicit talker_item_const( const item_location *new_me ): me_it_const( new_me ) {}
        talker_item_const() = default;
        talker_item_const( const talker_item_const & ) = default;
        talker_item_const( talker_item_const && ) = delete;
        talker_item_const &operator=( const talker_item_const & ) = default;
        talker_item_const &operator=( talker_item_const && ) = delete;
        ~talker_item_const() override = default;

        item_location const *get_const_item() const override {
            return me_it_const;
        }

        // identity and location
        std::string disp_name() const override;
        std::string get_name() const override;
        int posx( const map &here ) const override;
        int posy( const map &here ) const override;
        int posz() const override;
        tripoint_bub_ms pos_bub( const map &here ) const override;
        tripoint_abs_ms pos_abs() const override;
        tripoint_abs_omt pos_abs_omt() const override;

        diag_value const *maybe_get_value( const std::string &var_name ) const override;

        bool has_flag( const flag_id &f ) const override;

        std::vector<std::string> get_topics( bool radio_contact ) const override;
        bool will_talk_to_u( const Character &you, bool force ) const override;

        int get_cur_hp( const bodypart_id & ) const override;
        int get_degradation() const override;
        int get_hp_max( const bodypart_id & ) const override;
        units::energy power_cur() const override;
        units::energy power_max() const override;
        int get_artifact_resonance() const override;

        int get_count() const override;
        int coverage_at( bodypart_id & ) const override;
        int encumbrance_at( bodypart_id & ) const override;
        int get_volume() const override;
        units::mass get_weight() const override;
        int get_quality( const std::string &, bool strict ) const override;

        int get_price() const override;
        int get_price_postapoc() const override;

        // inventory queries answer for what the item holds, not for the item itself
        int charges_of( const itype_id &item_id ) const override;
        bool has_charges( const itype_id &item_id, int count ) const override;
        bool has_charges( const itype_id &item_id, int count, bool in_tools ) const override;
        bool has_amount( const itype_id &item_id, int count ) const override;
        int get_amount( const itype_id &item_id ) const override;
        bool has_item_with_flag( const flag_id &flag ) const override;
        std::vector<const item *> const_items_with( const std::function<bool( const item & )> &filter )
        const override;
    private:
        const item_location *me_it_const{};
};

class talker_item: public talker_item_const, public talker_cloner<talker_item>
{
    public:
        explicit talker_item( item_location *new_me ) : talker_item_const( new_me ), me_it( new_me ) {};
        talker_item() = default;
        talker_item( const talker_item & ) = default;
        talker_item( talker_item && ) = delete;
        talker_item &operator=( const talker_item & ) = default;
        talker_item &operator=( talker_item && ) = delete;
        ~talker_item() override = default;

        // underlying element accessor functions
        item_location *get_item() override {
            return me_it;
        }

        void set_value( const std::string &var_name, diag_value const &value ) override;
        void remove_value( const std::string & ) override;

        void set_power_cur( units::energy value ) override;
        void set_all_parts_hp_cur( int ) override;
        void set_degradation( int ) override;
        void die( map *here ) override;
        void set_fault( const fault_id &fault_id, bool force, const Character *holder ) override;
        void set_random_fault_of_type( const std::string &fault_type, bool force,
                                       const Character *holder ) override;

        // puts the item into this one's container pockets, or on its tile if it doesn't fit
        void i_add( const item &new_item ) override;
        void i_add_or_drop( item &new_item, bool force_equip = false ) override;
        void remove_items_with( const std::function<bool( const item & )> &filter ) override;
        std::vector<item *> items_with( const std::function<bool( const item & )> &filter ) override;

    private:
        item_location *me_it{};
};
#endif // CATA_SRC_TALKER_ITEM_H
