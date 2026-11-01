#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cata_catch.h"
#include "colony.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "item_uid.h"
#include "json.h"
#include "json_loader.h"
#include "type_id.h"

static const faction_id faction_your_followers( "your_followers" );

static const itype_id itype_hammer( "hammer" );

static std::string save_pile( const cata::colony<item> &pile )
{
    std::ostringstream os;
    JsonOut jsout( os );
    jsout.write( pile );
    return os.str();
}

static cata::colony<item> load_pile( const std::string &saved )
{
    cata::colony<item> loaded;
    const JsonValue jv = json_loader::from_string( saved );
    REQUIRE( jv.read( loaded, true ) );
    return loaded;
}

// ITEM-B02: map item piles are saved with run-length encoding. Items that looked the
// same were merged even when their owner, birthday, snippet etc. differed, and every
// copy came back with a fresh uid, breaking references to it.
TEST_CASE( "item-b02_rle_keeps_saved_state_and_uids", "[audit][item]" )
{
    cata::colony<item> pile;
    for( int i = 0; i < 4; ++i ) {
        item hammer( itype_hammer );
        if( i == 2 ) {
            hammer.set_owner( faction_your_followers );
        }
        pile.insert( std::move( hammer ) );
    }
    std::vector<int64_t> uids;
    std::vector<faction_id> owners;
    for( const item &it : pile ) {
        REQUIRE( it.uid().is_valid() );
        uids.push_back( it.uid().get_value() );
        owners.push_back( it.get_owner() );
    }

    const std::string saved = save_pile( pile );
    CAPTURE( saved );
    // The two leading unowned hammers are still compressed into one two-element run, which
    // older builds can read; the other hammer's uid travels inside the item as "rle_uids".
    CHECK( saved.find( "\"rle_uids\":[" + std::to_string( uids[0] ) + "]" ) != std::string::npos );
    CHECK( saved.find( "},2]" ) != std::string::npos );

    const cata::colony<item> loaded = load_pile( saved );
    REQUIRE( loaded.size() == 4 );
    std::vector<int64_t> loaded_uids;
    std::vector<faction_id> loaded_owners;
    for( const item &it : loaded ) {
        loaded_uids.push_back( it.uid().get_value() );
        loaded_owners.push_back( it.get_owner() );
    }
    CHECK( loaded_owners == owners );
    CHECK( loaded_uids == uids );
}

TEST_CASE( "item-b02_rle_loads_legacy_runs_without_uids", "[audit][item]" )
{
    const cata::colony<item> loaded = load_pile( R"([[{"typeid":"hammer"},3]])" );
    REQUIRE( loaded.size() == 3 );
    std::vector<int64_t> seen;
    for( const item &it : loaded ) {
        CHECK( it.typeId() == itype_hammer );
        CHECK( it.uid().is_valid() );
        for( int64_t other : seen ) {
            CHECK( other != it.uid().get_value() );
        }
        seen.push_back( it.uid().get_value() );
    }
}
