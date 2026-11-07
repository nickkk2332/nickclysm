#pragma once
#ifndef CATA_SRC_SHOP_CONS_RATE_H
#define CATA_SRC_SHOP_CONS_RATE_H

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "generic_factory.h"
#include "translation.h"
#include "type_id.h"
#include "units.h"

class JsonObject;
class JsonValue;
class item;
class npc;
struct const_dialogue;

constexpr char const *SHOPKEEPER_CONSUMPTION_RATES = "shopkeeper_consumption_rates";
constexpr char const *SHOPKEEPER_BLACKLIST = "shopkeeper_blacklist";
constexpr char const *SHOPKEEPER_WHITELIST = "shopkeeper_whitelist";

struct icg_entry {
    itype_id itype;
    item_category_id category;
    item_group_id item_group;
    translation message;

    std::function<bool( const_dialogue const & )> condition;

    bool operator==( icg_entry const &rhs ) const;
    bool matches( item const &it, npc const &beta ) const;
    /// What the condition (and a price rule's math) sees for @ref it, traded with @ref beta: the
    /// avatar as alpha, the trader as beta, and the item in the context variables trade_item,
    /// trade_item_category, trade_item_count, trade_item_damage, trade_item_rotten and
    /// trade_item_owner
    static const_dialogue trade_dialogue( item const &it, npc const &beta );
    /// Reports an item, category or group that isn't defined; @ref owner says whose entry it is
    void check( std::string_view owner ) const;
};

class icg_entry_reader : public generic_typed_reader<icg_entry_reader>
{
    public:
        static icg_entry _part_get_next( JsonObject const &jo );
        static icg_entry get_next( JsonValue &jv );
};

struct shopkeeper_cons_rate_entry: public icg_entry {
    int rate = 0;

    shopkeeper_cons_rate_entry() = default;
    explicit shopkeeper_cons_rate_entry( icg_entry const &rhs ) : icg_entry( rhs ), rate( 0 ) {}

    bool operator==( shopkeeper_cons_rate_entry const &rhs ) const;
};

struct shopkeeper_cons_rates {
    shopkeeper_cons_rates_id id;
    bool was_loaded = false;

    int default_rate = -1;
    units::money junk_threshold = 1_cent;
    std::vector<shopkeeper_cons_rate_entry> rates;

    shopkeeper_cons_rates() = default;

    static void reset();
    static const std::vector<shopkeeper_cons_rates> &get_all();
    static void load_rate( const JsonObject &jo, std::string const &src );
    static void finalize_all();
    static void check_all();
    void load( const JsonObject &jo, std::string_view src );
    void check() const;

    int get_rate( item const &it, npc const &beta ) const;
    bool matches( item const &it, npc const &beta ) const;
};

struct shopkeeper_blacklist {
    shopkeeper_blacklist_id id;
    bool was_loaded = false;

    std::vector<icg_entry> entries;

    static void reset();
    static const std::vector<shopkeeper_blacklist> &get_all();
    static void load_blacklist( const JsonObject &jo, std::string const &src );
    static void finalize_all();
    static void check_all();
    void load( const JsonObject &jo, std::string_view src );
    void check() const;
    icg_entry const *matches( item const &it, npc const &beta ) const;
};

struct shopkeeper_whitelist {
    shopkeeper_whitelist_id id;
    bool was_loaded = false;

    std::vector<icg_entry> entries;
    translation message;

    static void reset();
    static const std::vector<shopkeeper_whitelist> &get_all();
    static void load_whitelist( const JsonObject &jo, std::string const &src );
    static void finalize_all();
    static void check_all();
    void load( const JsonObject &jo, std::string_view src );
    void check() const;
    icg_entry const *matches( item const &it, npc const &beta ) const;
};

#endif // CATA_SRC_SHOP_CONS_RATE_H
