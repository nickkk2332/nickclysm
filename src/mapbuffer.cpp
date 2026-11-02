#include "mapbuffer.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cata_path.h"
#include "cata_utility.h"
#include "debug.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "game.h"
#include "input.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "output.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "point.h"
#include "popup.h"
#include "std_hash_fs_path.h"
#include "string_formatter.h"
#include "submap.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "worldfactory.h"
#include "zzip.h"

#define dbg(x) DebugLog((x),D_MAP) << __FILE__ << ":" << __LINE__ << ": "

static std::string quad_file_name( const tripoint_abs_omt &om_addr )
{
    return string_format( "%d.%d.%d.map", om_addr.x(), om_addr.y(), om_addr.z() );
}

static cata_path find_dirname( const tripoint_abs_omt &om_addr )
{
    const tripoint_abs_seg segment_addr = project_to<coords::seg>( om_addr );
    std::string segment = string_format( "%d.%d.%d",
                                         segment_addr.x(),
                                         segment_addr.y(), segment_addr.z() );
    return PATH_INFO::current_dimension_save_path() / "maps" / segment;
}

mapbuffer MAPBUFFER;

mapbuffer::mapbuffer() = default;
mapbuffer::~mapbuffer() = default;

void mapbuffer::clear()
{
    submaps.clear();
    // A cleared buffer is about to load another save or dimension
    segment_listings.clear();
}

void mapbuffer::clear_outside_reality_bubble()
{
    map &here = get_map();
    auto it = submaps.begin();
    while( it != submaps.end() ) {
        if( here.inbounds( it->first ) ) {
            ++it;
        } else {
            it = submaps.erase( it );
        }
    }
}

bool mapbuffer::add_submap( const tripoint_abs_sm &p, std::unique_ptr<submap> &sm )
{
    // The buffer never holds an entry without a submap: submap_exists and lookup_submap would
    // disagree about it
    if( !sm ) {
        return false;
    }
    const auto iter = submaps.find( p );
    if( iter != submaps.end() && iter->second ) {
        return false;
    }

    submaps[p] = std::move( sm );

    return true;
}

bool mapbuffer::add_submap( const tripoint_abs_sm &p, submap *sm )
{
    // FIXME: get rid of this overload and make submap ownership semantics sane.
    std::unique_ptr<submap> temp( sm );
    bool result = add_submap( p, temp );
    if( !result ) {
        // NOLINTNEXTLINE( bugprone-unused-return-value )
        temp.release();
    }
    return result;
}

void mapbuffer::remove_submap( const tripoint_abs_sm &addr )
{
    auto m_target = submaps.find( addr );
    if( m_target == submaps.end() ) {
        debugmsg( "Tried to remove non-existing submap %s", addr.to_string() );
        return;
    }
    submaps.erase( m_target );
}

submap *mapbuffer::lookup_submap( const tripoint_abs_sm &p )
{
    dbg( D_INFO ) << "mapbuffer::lookup_submap( x[" << p.x() << "], y[" << p.y() << "], z["
                  << p.z() << "])";

    const auto iter = submaps.find( p );
    if( iter == submaps.end() || !iter->second ) {
        try {
            return unserialize_submaps( p );
        } catch( const std::exception &err ) {
            debugmsg( "Failed to load submap %s: %s", p.to_string(), err.what() );
        }
        return nullptr;
    }

    return iter->second.get();
}

bool mapbuffer::submap_exists( const tripoint_abs_sm &p )
{
    // Could so with a second check against a std::unordered_set<tripoint_abs_sm> of already checked existing but not loaded submaps before resorting to unserializing?
    const auto iter = submaps.find( p );
    if( iter == submaps.end() || !iter->second ) {
        try {
            return unserialize_submaps( p ) != nullptr;
        } catch( const std::exception &err ) {
            debugmsg( "Failed to load submap %s: %s", p.to_string(), err.what() );
        }
        return false;
    }

    return true;
}

bool mapbuffer::submap_exists_approx( const tripoint_abs_sm &p )
{
    const auto iter = submaps.find( p );
    if( iter == submaps.end() ) {
        try {
            const tripoint_abs_omt om_addr = project_to<coords::omt>( p );
            std::string file_name = quad_file_name( om_addr );
            if( const std::unordered_set<std::string> *listing = segment_listing( om_addr ) ) {
                return listing->count( file_name ) > 0;
            }

            const cata_path dirname = find_dirname( om_addr );
            if( world_generator->active_world->has_compression_enabled() ) {
                cata_path zzip_name = dirname;
                zzip_name += zzip_suffix;
                if( !file_exist( zzip_name ) ) {
                    return false;
                }
                std::optional<zzip> z = zzip::load( zzip_name.get_unrelative_path(),
                                                    ( PATH_INFO::world_base_save_path() / "maps.dict" ).get_unrelative_path() );
                return z && z->has_file( std::filesystem::u8path( file_name ) );
            } else {
                return file_exist( dirname / file_name );
            }
        } catch( const std::exception &err ) {
            debugmsg( "Failed to load submap %s: %s", p.to_string(), err.what() );
        }
        return false;
    }

    return true;
}

void mapbuffer::save( bool delete_after_save )
{
    assure_dir_exist( PATH_INFO::current_dimension_save_path() / "maps" );
    int num_saved_submaps = 0;
    int num_total_submaps = submaps.size();

    map &here = get_map();

    static_popup popup;

    // A set of already-saved submaps, in global overmap coordinates.
    std::set<tripoint_abs_omt> saved_submaps;
    std::list<tripoint_abs_sm> submaps_to_delete;
    static constexpr std::chrono::milliseconds update_interval( 500 );
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();

    for( auto &elem : submaps ) {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if( last_update + update_interval < now ) {
            popup.message( _( "Please wait as the map saves [%d/%d]" ),
                           num_saved_submaps, num_total_submaps );
            ui_manager::redraw();
            refresh_display();
            inp_mngr.pump_events();
            last_update = now;
        }
        // Whatever the coordinates of the current submap are,
        // we're saving a 2x2 quad of submaps at a time.
        // Submaps are generated in quads, so we know if we have one member of a quad,
        // we have the rest of it, if that assumption is broken we have REAL problems.
        const tripoint_abs_omt om_addr = project_to<coords::omt>( elem.first );
        if( saved_submaps.count( om_addr ) != 0 ) {
            // Already handled this one.
            continue;
        }
        saved_submaps.insert( om_addr );

        // A segment is a chunk of 32x32 submap quads.
        // We're breaking them into subdirectories so there aren't too many files per directory.
        // Might want to make a set for this one too so it's only checked once per save().
        const cata_path dirname = find_dirname( om_addr );
        const cata_path quad_path = dirname / quad_file_name( om_addr );

        bool inside_reality_bubble = here.inbounds( om_addr );
        // delete_on_save deletes everything, otherwise delete submaps
        // outside the current map.
        save_quad( dirname, quad_path, om_addr, submaps_to_delete,
                   delete_after_save || !inside_reality_bubble );
        num_saved_submaps += 4;
    }
    for( auto &elem : submaps_to_delete ) {
        remove_submap( elem );
    }
}

void mapbuffer::save_quad(
    const cata_path &dirname, const cata_path &filename, const tripoint_abs_omt &om_addr,
    std::list<tripoint_abs_sm> &submaps_to_delete, bool delete_after_save )
{
    std::vector<point_rel_sm> offsets;
    std::vector<tripoint_abs_sm> submap_addrs;
    offsets.reserve( 4 );
    submap_addrs.reserve( 4 );
    offsets.push_back( point_rel_sm::zero );
    offsets.push_back( point_rel_sm::south );
    offsets.push_back( point_rel_sm::east );
    offsets.push_back( point_rel_sm::south_east );

    bool all_uniform = true;
    bool any_reverted = false;
    for( point_rel_sm &offsets_offset : offsets ) {
        tripoint_abs_sm submap_addr = project_to<coords::sm>( om_addr );
        submap_addr += offsets_offset.raw(); // TODO: Make += etc. available to relative parameters as well.
        submap_addrs.push_back( submap_addr );
        // find rather than [], which would insert an entry without a submap
        const auto iter = submaps.find( submap_addr );
        const submap *sm = iter == submaps.end() ? nullptr : iter->second.get();
        if( sm != nullptr ) {
            if( !sm->is_uniform() ) {
                all_uniform = false;
            } else if( sm->reverted ) {
                any_reverted = true;
            }
        }
    }

    const bool compressed = world_generator->active_world->has_compression_enabled();
    const std::filesystem::path quad_file = filename.get_relative_path().filename();
    std::optional<zzip> z;
    cata_path zzip_name = dirname;
    zzip_name += zzip_suffix;
    const auto open_zzip = [&]() {
        z = zzip::load( zzip_name.get_unrelative_path(),
                        ( PATH_INFO::world_base_save_path() / "maps.dict" ).get_unrelative_path() );
        if( !z ) {
            throw std::runtime_error( "Failed opening compressed save file " +
                                      zzip_name.get_unrelative_path().generic_u8string() );
        }
    };

    bool reverted_to_uniform = false;
    if( all_uniform ) {
        // Nothing to save - this quad will be regenerated faster than it would be re-read.
        // Uniform quads are the great majority, so nothing here opens the save (for a
        // compressed world, opening the segment's zzip would also create it) unless a
        // submap reverted to uniform, whose saved quad then has to go.
        if( any_reverted ) {
            if( !compressed ) {
                reverted_to_uniform = std::filesystem::exists( filename.get_unrelative_path() );
            } else if( file_exist( zzip_name ) ) {
                open_zzip();
                reverted_to_uniform = z->has_file( quad_file );
            }
        }
        // deleting the file might fail on some platforms in some edge cases so force serialize this
        // uniform quad
        if( !reverted_to_uniform ) {
            if( delete_after_save ) {
                for( auto &submap_addr : submap_addrs ) {
                    const auto iter = submaps.find( submap_addr );
                    if( iter != submaps.end() && iter->second != nullptr ) {
                        submaps_to_delete.push_back( submap_addr );
                    }
                }
            }
            return;
        }
    } else if( compressed ) {
        open_zzip();
    }

    std::stringstream stringout;
    JsonOut jsout( stringout );
    jsout.start_array();
    for( auto &submap_addr : submap_addrs ) {
        if( submaps.count( submap_addr ) == 0 ) {
            continue;
        }

        submap *sm = submaps[submap_addr].get();

        if( sm == nullptr ) {
            continue;
        }

        jsout.start_object();

        jsout.member( "version", savegame_version );
        jsout.member( "coordinates" );

        jsout.start_array();
        jsout.write( submap_addr.x() );
        jsout.write( submap_addr.y() );
        jsout.write( submap_addr.z() );
        jsout.end_array();

        sm->store( jsout );

        jsout.end_object();

        if( delete_after_save ) {
            submaps_to_delete.push_back( submap_addr );
        }
    }

    jsout.end_array();

    std::string s = std::move( stringout ).str();

    if( z ) {
        z->add_file( quad_file, s );
    } else {
        // Don't create the directory if it would be empty
        assure_dir_exist( dirname );
        write_to_file( filename, [&]( std::ostream & fout ) {
            fout << s;
        } );
    }
    note_quad_file( om_addr, quad_file.generic_u8string(), true );

    if( all_uniform && reverted_to_uniform ) {
        bool deleted = false;
        if( z ) {
            deleted = z->delete_files( { quad_file } );
        } else {
            deleted = std::filesystem::remove( filename.get_unrelative_path() );
        }
        if( deleted ) {
            note_quad_file( om_addr, quad_file.generic_u8string(), false );
        }
    }
    if( z ) {
        cata_path tmp_path = zzip_name + ".tmp";
        if( z->compact_to( tmp_path.get_unrelative_path(), 2.0 ) ) {
            z.reset();
            rename_file( tmp_path, zzip_name );
        }
    }
}

void mapbuffer::check_listings_current()
{
    const std::string maps_dir = ( PATH_INFO::current_dimension_save_path() / "maps"
                                 ).generic_u8string();
    const bool compressed = world_generator->active_world->has_compression_enabled();
    if( maps_dir != listed_maps_dir || compressed != listed_compressed ) {
        segment_listings.clear();
        listed_maps_dir = maps_dir;
        listed_compressed = compressed;
    }
}

const std::unordered_set<std::string> *mapbuffer::segment_listing( const tripoint_abs_omt &om_addr )
{
    check_listings_current();
    const tripoint_abs_seg segment = project_to<coords::seg>( om_addr );
    const auto known = segment_listings.find( segment );
    if( known != segment_listings.end() ) {
        return &known->second;
    }

    std::unordered_set<std::string> listing;
    const cata_path dirname = find_dirname( om_addr );
    if( listed_compressed ) {
        cata_path zzip_name = dirname;
        zzip_name += zzip_suffix;
        // An absent zzip lists nothing; don't create it by opening it
        if( file_exist( zzip_name ) ) {
            std::optional<zzip> z = zzip::load( zzip_name.get_unrelative_path(),
                                                ( PATH_INFO::world_base_save_path() / "maps.dict" ).get_unrelative_path() );
            if( !z ) {
                // Leave it to the lookup, which reports it
                return nullptr;
            }
            for( const std::filesystem::path &entry : z->get_entries() ) {
                listing.insert( entry.generic_u8string() );
            }
        }
    } else {
        const std::filesystem::path dir = dirname.get_unrelative_path();
        try {
            if( std::filesystem::is_directory( dir ) ) {
                for( const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator( dir ) ) {
                    listing.insert( entry.path().filename().generic_u8string() );
                }
            }
        } catch( const std::filesystem::filesystem_error & ) {
            return nullptr;
        }
    }
    return &segment_listings.emplace( segment, std::move( listing ) ).first->second;
}

void mapbuffer::note_quad_file( const tripoint_abs_omt &om_addr, const std::string &file_name,
                                bool on_disk )
{
    check_listings_current();
    const auto known = segment_listings.find( project_to<coords::seg>( om_addr ) );
    if( known == segment_listings.end() ) {
        // Not listed yet; listing it later reads the save
        return;
    }
    if( on_disk ) {
        known->second.insert( file_name );
    } else {
        known->second.erase( file_name );
    }
}

// We're reading in way too many entities here to mess around with creating sub-objects and
// seeking around in them, so we're using the json streaming API.
submap *mapbuffer::unserialize_submaps( const tripoint_abs_sm &p )
{
    // Map the tripoint to the submap quad that stores it.
    const tripoint_abs_omt om_addr = project_to<coords::omt>( p );
    std::string file_name = quad_file_name( om_addr );

    // Most lookups that miss are for quads that were never saved (uniform ones, and
    // map::generate asks for every submap of an OMT): answer those from the listing
    const std::unordered_set<std::string> *listing = segment_listing( om_addr );
    if( listing != nullptr && listing->count( file_name ) == 0 ) {
        return nullptr;
    }

    const cata_path dirname = find_dirname( om_addr );
    std::filesystem::path file_name_path = std::filesystem::u8path( file_name );
    cata_path quad_path = dirname / file_name;

    bool read = [&] {
        if( world_generator->active_world->has_compression_enabled() )
        {
            cata_path zzip_name = dirname;
            zzip_name += zzip_suffix;
            if( !file_exist( zzip_name ) ) {
                return false;
            }

            std::optional<zzip> z = zzip::load( zzip_name.get_unrelative_path(),
                                                ( PATH_INFO::world_base_save_path() / "maps.dict" ).get_unrelative_path() );
            if( !z ) {
                debugmsg( _fmt( "Failed to load submaps from {0}, could not open zzip.", zzip_name ) );
                return false;
            }
            if( !z->has_file( file_name_path ) ) {
                return false;
            }
            std::vector<std::byte> contents = z->get_file( file_name_path );
            std::string_view string_contents{ reinterpret_cast<char *>( contents.data() ), contents.size() };
            JsonValue jsin = json_loader::from_string( std::string( string_contents ) );
            try {
                deserialize( jsin );
            } catch( std::exception &err ) {
                debugmsg( _( "Failed to read from \"%1$s\": %2$s" ), zzip_name.generic_u8string() + ":" + file_name,
                          err.what() );
                return false;
            }
            return true;
        } else
        {
            return read_from_file_optional_json( quad_path, [this]( const JsonValue & jsin ) {
                deserialize( jsin );
            } );
        }
    }();

    if( !read ) {
        return nullptr;
    }

    // fill in uniform submaps that were not serialized. Note that failure as a result of it
    // not being uniform is OK and results in any missing uniform submaps being generated.
    oter_id const oid = overmap_buffer.ter( om_addr );
    generate_uniform_omt( project_to<coords::sm>( om_addr ), oid );
    // Don't insert an empty entry for a missing submap, so it can still be generated
    const auto iter = submaps.find( p );
    if( iter == submaps.end() || !iter->second ) {
        debugmsg( "file %s did not contain the expected submap %s for non-uniform terrain %s",
                  quad_path.generic_u8string(), p.to_string(), oid.id().str() );
        return nullptr;
    }

    return iter->second.get();
}

void mapbuffer::deserialize( const JsonArray &ja )
{
    for( JsonObject submap_json : ja ) {
        std::unique_ptr<submap> sm = std::make_unique<submap>();
        tripoint_abs_sm submap_coordinates;
        int version = 0;
        // We have to read version first because the iteration order of json members is undefined.
        if( submap_json.has_int( "version" ) ) {
            version = submap_json.get_int( "version" );
        }
        for( JsonMember submap_member : submap_json ) {
            std::string submap_member_name = submap_member.name();
            if( submap_member_name == "coordinates" ) {
                JsonArray coords_array = submap_member;
                tripoint_abs_sm loc{ coords_array.next_int(), coords_array.next_int(), coords_array.next_int() };
                submap_coordinates = loc;
            } else {
                sm->load( submap_member, submap_member_name, version );
            }
        }

        if( !add_submap( submap_coordinates, sm ) ) {
            debugmsg( "submap %s was already loaded", submap_coordinates.to_string() );
        }
    }
}
