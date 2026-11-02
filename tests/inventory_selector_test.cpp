#include <functional>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "cata_utility.h"
#include "coordinates.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "map.h"
#include "map_helpers.h"
#include "map_selector.h"
#include "player_helpers.h"
#include "point.h"
#include "pocket_type.h"
#include "ret_val.h"
#include "type_id.h"

class Character;

static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bag_plastic( "bag_plastic" );
static const itype_id itype_hammer( "hammer" );
static const itype_id itype_knife_hunting( "knife_hunting" );
static const itype_id itype_rock( "rock" );
static const itype_id itype_test_jacket_jean( "test_jacket_jean" );

namespace
{
class counting_preset : public inventory_selector_preset
{
    public:
        explicit counting_preset( std::function<bool( const item & )> shows ) :
            shows( std::move( shows ) ) {}
        bool is_shown( const item_location &loc ) const override {
            ++asked[loc.get_item()];
            return shows( *loc );
        }
        mutable std::map<const item *, int> asked;
    private:
        std::function<bool( const item & )> shows;
};

class layout_selector : public inventory_selector
{
    public:
        using inventory_selector::inventory_selector;
        using inventory_selector::get_all_columns;
};

std::vector<std::string> layout_of( const layout_selector &selector )
{
    std::vector<std::string> out;
    for( const inventory_column *col : selector.get_all_columns() ) {
        for( const inventory_entry *e : col->get_entries( []( const inventory_entry & en ) {
        return en.is_item();
        }, true ) ) {
            out.push_back( e->any_item()->typeId().str() + " top=" +
                           ( e->topmost_parent ? e->topmost_parent->typeId().str() : "none" ) +
                           " indent=" + std::to_string( e->indent ) +
                           " chevron=" + ( e->chevron ? "yes" : "no" ) );
        }
    }
    return out;
}

void check_asked_once( const counting_preset &preset )
{
    for( const auto &[it, n] : preset.asked ) {
        CAPTURE( it->typeId() );
        CHECK( n == 1 );
    }
}

class haul_selector_probe : public inventory_haul_selector
{
    public:
        using inventory_haul_selector::inventory_haul_selector;
        using inventory_multiselector::to_use;
};
} // namespace

TEST_CASE( "inventory_selector_asks_the_preset_once_per_item", "[inventory][ui]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = u.pos_bub();

    GIVEN( "backpack with a hammer and a knife" ) {
        item pack( itype_backpack );
        REQUIRE( pack.put_in( item( itype_hammer ), pocket_type::CONTAINER ).success() );
        REQUIRE( pack.put_in( item( itype_knife_hunting ), pocket_type::CONTAINER ).success() );
        REQUIRE_FALSE( here.add_item( pos, pack ).is_null() );

        WHEN( "every item shown" ) {
            counting_preset preset( return_true<item> );
            layout_selector selector( u, preset );
            selector.add_map_items( pos );
            THEN( "all three listed, each asked once" ) {
                CHECK( selector.item_entry_count() == 3 );
                CHECK( preset.asked.size() == 3 );
                check_asked_once( preset );
                CHECK( layout_of( selector ) == std::vector<std::string> {
                    "backpack top=none indent=0 chevron=yes",
                    "knife_hunting top=backpack indent=2 chevron=no",
                    "hammer top=backpack indent=2 chevron=no"
                } );
            }
        }
        WHEN( "backpack is hidden" ) {
            counting_preset preset( []( const item & it ) {
                return it.typeId() != itype_backpack;
            } );
            layout_selector selector( u, preset );
            selector.add_map_items( pos );
            THEN( "only contents listed, each item asked once" ) {
                CHECK( selector.item_entry_count() == 2 );
                check_asked_once( preset );
                CHECK( layout_of( selector ) == std::vector<std::string> {
                    "knife_hunting top=none indent=0 chevron=no",
                    "hammer top=none indent=0 chevron=no"
                } );
            }
        }
    }
    GIVEN( "backpack with plastic bag with hammer" ) {
        item bag( itype_bag_plastic );
        REQUIRE( bag.put_in( item( itype_hammer ), pocket_type::CONTAINER ).success() );
        item pack( itype_backpack );
        REQUIRE( pack.put_in( bag, pocket_type::CONTAINER ).success() );
        REQUIRE_FALSE( here.add_item( pos, pack ).is_null() );

        WHEN( "plastic bag in the middle hidden" ) {
            counting_preset preset( []( const item & it ) {
                return it.typeId() != itype_bag_plastic;
            } );
            layout_selector selector( u, preset );
            selector.add_map_items( pos );
            THEN( "backpack and hammer listed, each item asked once" ) {
                CHECK( selector.item_entry_count() == 2 );
                check_asked_once( preset );
                CHECK( layout_of( selector ) == std::vector<std::string> {
                    "backpack top=none indent=0 chevron=yes",
                    "hammer top=none indent=2 chevron=no"
                } );
            }
        }
    }
}

TEST_CASE( "haul_selector_skips_items_not_on_the_tile", "[inventory][ui]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    map &here = get_map();
    const tripoint_bub_ms pos = u.pos_bub();
    const tripoint_bub_ms elsewhere = pos + tripoint_rel_ms( 1, 0, 0 );

    GIVEN( "haul list with a hammer here and a knife on the next tile" ) {
        item &hammer = here.add_item( pos, item( itype_hammer ) );
        item &knife = here.add_item( elsewhere, item( itype_knife_hunting ) );
        REQUIRE_FALSE( hammer.is_null() );
        REQUIRE_FALSE( knife.is_null() );
        std::vector<item_location> haul_list {
            item_location( map_cursor( &here, pos ), &hammer ),
            item_location( map_cursor( &here, elsewhere ), &knife )
        };

        WHEN( "haul selector applies the list" ) {
            haul_selector_probe selector( u );
            selector.add_map_items( pos );
            selector.apply_selection( haul_list );

            THEN( "only hammer is selected" ) {
                REQUIRE( selector.to_use.size() == 1 );
                CHECK( selector.to_use.front().first.get_item() == &hammer );
                CHECK( selector.to_use.front().second == 1 );
            }
        }
    }
}

// UIINV-B02 (review): a drag between two items in one container skipped every parent check,
// but the pocket of that container holding the destination still grows when the item comes
// from its other pocket. Each of the jacket's two pockets holds 1 kg; a rock weighs 657 g.
TEST_CASE( "uiinv-b02_drag_drop_checks_the_pockets_that_grow", "[audit][inventory]" )
{
    clear_avatar();
    clear_map();
    avatar &u = get_avatar();
    map &here = get_map();
    std::optional<std::list<item>::iterator> worn = u.wear_item( item( itype_test_jacket_jean ),
            false );
    REQUIRE( worn );
    item &worn_jacket = **worn;
    item_location jacket( u, &worn_jacket );
    std::vector<item_pocket *> pockets = jacket->get_container_pockets();
    REQUIRE( pockets.size() == 2 );
    item_pocket &pocket_a = *pockets[0];
    item_pocket &pocket_b = *pockets[1];
    // pocket A: a plastic bag, and a rock beside it
    item *bag_it = pocket_a.insert_item( item( itype_bag_plastic ) ).value();
    item *rock_a_it = pocket_a.insert_item( item( itype_rock ) ).value();
    REQUIRE( bag_it != nullptr );
    REQUIRE( rock_a_it != nullptr );
    item_location bag( jacket, bag_it );
    const item_location rock_a( jacket, rock_a_it );
    REQUIRE( pocket_a.remaining_weight() < item( itype_rock ).weight() );

    SECTION( "from the jacket's other pocket into the bag" ) {
        item *rock_b_it = pocket_b.insert_item( item( itype_rock ) ).value();
        REQUIRE( rock_b_it != nullptr );
        const item_location rock_b( jacket, rock_b_it );
        // pocket A would hold the bag, two rocks, 1319 g
        const ret_val<void> ret = drag_drop_parents_can_contain( rock_b, bag );
        CHECK_FALSE( ret.success() );
        CHECK( ret.str() == "item is too heavy for a parent pocket" );
    }
    SECTION( "from beside the bag into it, in the same pocket" ) {
        // the rock leaves pocket A as it goes back in, in the bag
        CHECK( drag_drop_parents_can_contain( rock_a, bag ).success() );
    }
    SECTION( "from the bag into the jacket" ) {
        item *in_bag = bag->get_container_pockets().front()->insert_item( item( itype_rock ) ).value();
        REQUIRE( in_bag != nullptr );
        // pocket A is over its limit now, but its contents don't grow
        CHECK( drag_drop_parents_can_contain( item_location( bag, in_bag ), jacket ).success() );
    }
    SECTION( "from the ground into the bag" ) {
        item &ground_rock = here.add_item( u.pos_bub(), item( itype_rock ) );
        REQUIRE_FALSE( ground_rock.is_null() );
        const item_location from_ground( map_cursor( &here, u.pos_bub() ), &ground_rock );
        CHECK_FALSE( drag_drop_parents_can_contain( from_ground, bag ).success() );
    }
    clear_avatar();
}
