#include <functional>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "coordinates.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "options.h"
#include "overmap.h"
#include "point.h"
#include "type_id.h"

static const oter_str_id oter_forest( "forest" );
static const oter_str_id oter_road_nesw( "road_nesw" );

namespace
{
// Mimics locales such as de_DE: ',' as the decimal point and '.' as the
// thousands separator, grouping digits by three.
class comma_decimal_numpunct : public std::numpunct<char>
{
    protected:
        char do_decimal_point() const override {
            return ',';
        }
        char do_thousands_sep() const override {
            return '.';
        }
        std::string do_grouping() const override {
            return "\3";
        }
};
} // namespace

// Overmap predecessors (the terrain a road/trail/etc. was placed over) must survive
// a save/load round trip, otherwise mapgen falls back to the fallback predecessor.
TEST_CASE( "data-b01_overmap_predecessors_survive_save_load", "[audit][overmap]" )
{
    const tripoint_om_omt p( 10, 10, 0 );

    std::unique_ptr<overmap> om = std::make_unique<overmap>( point_abs_om::zero );
    om->ter_set( p, oter_forest.id() );
    om->ter_set( p, oter_road_nesw.id() );
    const std::vector<oter_id> before = om->predecessors( p );
    REQUIRE( before.size() == 1 );
    REQUIRE( before[0].id().str() == oter_forest.str() );

    std::ostringstream out;
    om->serialize( out );

    std::unique_ptr<overmap> loaded = std::make_unique<overmap>( point_abs_om::zero );
    std::istringstream in( out.str() );
    loaded->unserialize( in );

    CHECK( loaded->ter( p ).id().str() == oter_road_nesw.str() );
    const std::vector<oter_id> after = loaded->predecessors( p );
    REQUIRE( after.size() == 1 );
    CHECK( after[0].id().str() == oter_forest.str() );
}

// Option slider values must not be formatted with the UI locale, because
// cOpt::setValue( std::string ) parses them with the classic locale.
TEST_CASE( "data-b02_option_slider_values_ignore_ui_locale", "[audit][options]" )
{
    option_slider slider;
    {
        const std::locale old_locale;
        on_out_of_scope restore_locale( [&old_locale]() {
            std::locale::global( old_locale );
        } );
        std::locale::global( std::locale( std::locale::classic(), new comma_decimal_numpunct ) );

        // Sanity check: default-constructed streams now format with the comma locale.
        std::ostringstream probe;
        probe << 0.8 << ' ' << 1000;
        REQUIRE( probe.str() == "0,8 1.000" );

        JsonValue jv = json_loader::from_string( R"({
            "name": "audit test slider",
            "levels": [
                {
                    "level": 0,
                    "name": "audit test level",
                    "options": [
                        { "option": "SPAWN_DENSITY", "type": "float", "val": 0.8 },
                        { "option": "MONSTER_SPEED", "type": "int", "val": 1000 }
                    ]
                }
            ]
        })" );
        JsonObject jo = jv.get_object();
        slider.load( jo, "audit_test" );
    }
    REQUIRE( slider.count() == 1 );

    options_manager::options_container opts = get_options().get_world_defaults();
    REQUIRE( opts.count( "SPAWN_DENSITY" ) == 1 );
    REQUIRE( opts.count( "MONSTER_SPEED" ) == 1 );
    slider.apply_opts( 0, opts );

    CHECK( opts.at( "SPAWN_DENSITY" ).value_as<float>() == Approx( 0.8f ) );
    CHECK( opts.at( "MONSTER_SPEED" ).value_as<int>() == 1000 );
}
