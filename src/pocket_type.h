#pragma once
#ifndef CATA_SRC_POCKET_TYPE_H
#define CATA_SRC_POCKET_TYPE_H

#include <cstdint>
#include <initializer_list>

template <typename E> struct enum_traits;

enum class pocket_type : int {
    CONTAINER,
    MAGAZINE,
    MAGAZINE_WELL, //holds magazines
    MOD, // the gunmods or toolmods
    CORPSE, // the "corpse" pocket - bionics embedded in a corpse
    SOFTWARE, // software put into usb or some such; TO-DO: obsolete
    E_FILE_STORAGE, // storing electronic files
    CABLE, // pocket for storing power/data cables and handling their connections
    MIGRATION, // this allows items to load contents that are too big, in order to spill them later.
    EBOOK, // holds electronic books for a device or usb; TO-DO: obsolete
    LAST
};

template<>
struct enum_traits<pocket_type> {
    static constexpr pocket_type last = pocket_type::LAST;
};

/**
 * A set of pocket types held as bits, so building one (as a default argument does on every
 * call) and testing it costs nothing: `{ pocket_type::CONTAINER, pocket_type::MAGAZINE }`.
 */
class pocket_type_set
{
    public:
        constexpr pocket_type_set() = default;
        constexpr pocket_type_set( std::initializer_list<pocket_type> types ) {
            for( const pocket_type type : types ) {
                bits |= bit( type );
            }
        }
        constexpr bool contains( pocket_type type ) const {
            return ( bits & bit( type ) ) != 0;
        }
        constexpr bool empty() const {
            return bits == 0;
        }
        constexpr bool operator==( const pocket_type_set &rhs ) const {
            return bits == rhs.bits;
        }
        constexpr bool operator!=( const pocket_type_set &rhs ) const {
            return bits != rhs.bits;
        }
    private:
        static constexpr uint32_t bit( pocket_type type ) {
            return static_cast<uint32_t>( 1 ) << static_cast<int>( type );
        }
        static_assert( static_cast<int>( pocket_type::LAST ) <= 32,
                       "pocket_type_set holds one bit per pocket type" );
        uint32_t bits = 0;
};

#endif // CATA_SRC_POCKET_TYPE_H
