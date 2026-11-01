#include <string>

#include "cata_catch.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "item_pocket.h"
#include "json_loader.h"

// Loads a pocket definition and returns any debugmsg it produced (unread keys are reported
// when the JSON object is destroyed, so it must go out of scope inside the capture).
static std::string load_pocket( const std::string &json, pocket_data &out )
{
    return capture_debugmsg_during( [&]() {
        const JsonObject jo = json_loader::from_string( json );
        out.deserialize( jo );
    } );
}

// INV-J01: the pocket loader marked every member as read, so a misspelled or ignored key
// (12 clothing pockets used "max_length" for "max_item_length") never reported an error.
TEST_CASE( "inv-j01_pocket_loader_reports_unknown_keys", "[audit][pocket]" )
{
    SECTION( "a misspelled key is reported" ) {
        pocket_data pocket;
        const std::string msg = load_pocket( R"({ "pocket_type": "CONTAINER",
            "max_contains_volume": "1 L", "max_contains_weight": "1 kg", "max_length": "20 cm" })",
                                             pocket );
        CHECK( msg.find( "max_length" ) != std::string::npos );
    }
    SECTION( "a correct pocket loads quietly" ) {
        pocket_data pocket;
        const std::string msg = load_pocket( R"({ "pocket_type": "CONTAINER",
            "max_contains_volume": "1 L", "max_contains_weight": "1 kg",
            "max_item_length": "20 cm" })", pocket );
        CHECK( msg.empty() );
        CHECK( pocket.max_item_length == 20_cm );
    }
    SECTION( "ammo pockets read the keys that apply to them" ) {
        pocket_data pocket;
        const std::string msg = load_pocket( R"({ "pocket_type": "CONTAINER",
            "ammo_restriction": { "rock": 10 }, "volume_encumber_modifier": 0.3 })", pocket );
        CHECK( msg.empty() );
        CHECK( pocket.volume_encumber_modifier == Approx( 0.3f ) );
    }
}
