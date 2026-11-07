#include "subbodypart.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "bodypart.h"
#include "damage.h"
#include "debug.h"
#include "flexbuffer_json.h"
#include "generic_factory.h"
#include "type_id.h"

namespace
{

generic_factory<sub_body_part_type> sub_body_part_factory( "sub body part" );

// groups all the bodypart into a shared bucket
std::unordered_map<sub_bodypart_str_id, std::vector<sub_bodypart_str_id>>
        combined_similar_sub_bodyparts;

} // namespace

/**@relates string_id*/
template<>
bool string_id<sub_body_part_type>::is_valid() const
{
    return sub_body_part_factory.is_valid( *this );
}

/** @relates int_id */
template<>
bool int_id<sub_body_part_type>::is_valid() const
{
    return sub_body_part_factory.is_valid( *this );
}

/**@relates string_id*/
template<>
const sub_body_part_type &string_id<sub_body_part_type>::obj() const
{
    return sub_body_part_factory.obj( *this );
}

/** @relates int_id */
template<>
const sub_body_part_type &int_id<sub_body_part_type>::obj() const
{
    return sub_body_part_factory.obj( *this );
}

/** @relates int_id */
template<>
const sub_bodypart_str_id &int_id<sub_body_part_type>::id() const
{
    return sub_body_part_factory.convert( *this );
}

/**@relates string_id*/
template<>
sub_bodypart_id string_id<sub_body_part_type>::id() const
{
    return sub_body_part_factory.convert( *this, sub_bodypart_id( 0 ) );
}

/** @relates int_id */
template<>
int_id<sub_body_part_type>::int_id( const string_id<sub_body_part_type> &id ) : _id( id.id() ) {}

void sub_body_part_type::load_bp( const JsonObject &jo, const std::string &src )
{
    sub_body_part_factory.load( jo, src );
}

void sub_body_part_type::load( const JsonObject &jo, std::string_view )
{
    mandatory( jo, was_loaded, "id", id );
    mandatory( jo, was_loaded, "name", name );
    mandatory( jo, was_loaded, "parent", parent );
    optional( jo, was_loaded, "secondary", secondary );
    optional( jo, was_loaded, "max_coverage", max_coverage );
    optional( jo, was_loaded, "side", part_side );
    optional( jo, was_loaded, "name_multiple", name_multiple );

    // turn off blindly copying-from to prevent accidental inconsistent opposites
    if( jo.has_string( "opposite" ) ) {
        mandatory( jo, was_loaded, "opposite", opposite );
    } else {
        opposite = id;
    }

    // defaults to self
    optional( jo, was_loaded, "locations_under", locations_under, { id } );
    optional( jo, was_loaded, "similar_bodypart", similar_bodypart );
    optional( jo, was_loaded, "unarmed_damage", unarmed_damage );
}

void sub_body_part_type::reset()
{
    sub_body_part_factory.reset();
    // Or the parts of a world loaded earlier stay listed (CHAR-B07)
    combined_similar_sub_bodyparts.clear();
}

void sub_body_part_type::finalize_all()
{
    combined_similar_sub_bodyparts.clear();
    sub_body_part_factory.finalize();
}

void sub_body_part_type::finalize()
{
    // populate combined_similar_bodyparts
    if( similar_bodypart.has_value() ) {
        const auto add_similar = []( std::vector<sub_bodypart_str_id> &similar,
        const sub_bodypart_str_id & sbp ) {
            if( std::find( similar.begin(), similar.end(), sbp ) == similar.end() ) {
                similar.emplace_back( sbp );
            }
        };
        // `head` is similar to `head_dragonfly`
        add_similar( combined_similar_sub_bodyparts[similar_bodypart.value()], id );
        // `head_dragonfly` is similar to `head`
        add_similar( combined_similar_sub_bodyparts[id], similar_bodypart.value() );
    }
}

void sub_body_part_type::check_consistency()
{
    sub_body_part_factory.check();
}

void sub_body_part_type::check() const
{
    if( !parent.is_valid() ) {
        debugmsg( "Sub body part %s has invalid parent %s", id.c_str(), parent.c_str() );
    }
    if( !opposite.is_valid() ) {
        debugmsg( "Sub body part %s has invalid opposite %s", id.c_str(), opposite.c_str() );
    }
    for( const sub_bodypart_str_id &under : locations_under ) {
        if( !under.is_valid() ) {
            debugmsg( "Sub body part %s has invalid locations_under %s", id.c_str(), under.c_str() );
        }
    }
    if( similar_bodypart.has_value() && !similar_bodypart->is_valid() ) {
        debugmsg( "Sub body part %s has invalid similar_bodypart %s", id.c_str(),
                  similar_bodypart->c_str() );
    }
    for( const damage_unit &du : unarmed_damage.damage_units ) {
        if( !du.type.is_valid() ) {
            debugmsg( "Sub body part %s has invalid unarmed_damage type %s", id.c_str(),
                      du.type.c_str() );
        }
    }
}

std::vector<sub_bodypart_str_id> sub_body_part_type::get_all_combined_similar_sub_bodyparts() const
{
    const auto found = combined_similar_sub_bodyparts.find( id );

    if( found != combined_similar_sub_bodyparts.end() ) {
        return found->second;
    }

    return std::vector<sub_bodypart_str_id>();
}
