#pragma once
#ifndef CATA_SRC_TILE_LOOKUP_MEMO_H
#define CATA_SRC_TILE_LOOKUP_MEMO_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// What a sprite lookup asks for on top of its base id: the id itself, its "_transparent"
// form, or its "_int<intensity>" form.
enum class tile_lookup_suffix : uint8_t {
    none,
    transparent,
    intensity,
};

/**
 * Remembers what sprite lookups resolved to, misses included, so a redraw resolves each id
 * with one hash and no string building.  The answers may only depend on the tileset, the
 * game data and the season: the season and the category are part of the key, and validate()
 * forgets everything when the tileset or the game data changes.
 *
 * The memo does not own its tileset (a tileset holds GPU textures, which must go when the
 * renderer lets go of them), so it watches it: if the tileset it was filled from has been
 * destroyed, another one at the same address is not mistaken for it.
 *
 * A lookup of an id the caller also holds as an int id (terrain, furniture, traps, fields)
 * can be remembered by that int id instead (find_by_int_id), which needs no hash.  An int id
 * stands for one string id only as long as the game data is unchanged, which validate()
 * already requires.
 *
 * The renderer uses the memo only while the "Experimental: cache sprite lookups" option is
 * on (set_enabled).
 */
template<typename Result, size_t Seasons, size_t Categories>
class tile_lookup_memo
{
    public:
        // Int ids from this one up are answered but not remembered, so a wild one can't make
        // a table huge.
        static constexpr int int_id_limit = 1 << 20;

        /**
         * Turns the memo on or off and returns whether it is on.  A change either way forgets
         * everything, so what it remembered before it was turned off is never used again.
         */
        bool set_enabled( bool on ) {
            if( on != enabled ) {
                clear();
                enabled = on;
            }
            return enabled;
        }
        bool is_enabled() const {
            return enabled;
        }

        /**
         * Forgets everything unless `owner` is the live tileset the memo was filled from and
         * `data_generation` (see DynamicDataLoader::data_generation) is unchanged.
         */
        template<typename T>
        void validate( const std::shared_ptr<T> &owner, uint64_t data_generation ) {
            const void *raw = owner.get();
            if( raw == owner_raw && !owner_alive.expired() && data_generation == generation ) {
                return;
            }
            clear();
            owner_raw = raw;
            owner_alive = owner;
            generation = data_generation;
        }

        /** The remembered answer, or nullptr if this lookup has not been made. */
        const Result *find( size_t season, size_t category, const std::string &id,
                            tile_lookup_suffix suffix, int intensity,
                            const std::string_view variant ) const {
            if( season >= Seasons || category >= Categories ) {
                return nullptr;
            }
            const by_id_map &by_id = answers[season][category];
            const auto found = by_id.find( id );
            if( found == by_id.end() ) {
                return nullptr;
            }
            return find_in( found->second, suffix, intensity, variant );
        }

        /**
         * As find(), for the lookup of the id whose int id in its category is `int_id` (the
         * to_i() of a ter_id for terrain, and so on).  These answers are kept apart from the
         * ones by string id.
         */
        const Result *find_by_int_id( size_t season, size_t category, int int_id,
                                      tile_lookup_suffix suffix, int intensity,
                                      const std::string_view variant ) const {
            if( season >= Seasons || category >= Categories || int_id < 0 ) {
                return nullptr;
            }
            const by_int_id_table &by_int_id = int_answers[season][category];
            if( static_cast<size_t>( int_id ) >= by_int_id.size() ) {
                return nullptr;
            }
            return find_in( by_int_id[static_cast<size_t>( int_id )], suffix, intensity, variant );
        }

        /**
         * Remembers `result` as the answer to this lookup and returns it.  The reference is
         * valid until the next call that changes the memo.
         */
        const Result &remember( size_t season, size_t category, const std::string_view id,
                                tile_lookup_suffix suffix, int intensity, const std::string_view variant,
                                Result result ) {
            if( season >= Seasons || category >= Categories ) {
                overflow = std::move( result );
                return overflow;
            }
            std::vector<answer> &for_id = answers[season][category][std::string( id )];
            for_id.push_back( { suffix, intensity, std::string( variant ), std::move( result ) } );
            ++count;
            return for_id.back().result;
        }

        /** As remember(), for the lookup find_by_int_id() finds. */
        const Result &remember_by_int_id( size_t season, size_t category, int int_id,
                                          tile_lookup_suffix suffix, int intensity,
                                          const std::string_view variant, Result result ) {
            if( season >= Seasons || category >= Categories || int_id < 0 ||
                int_id >= int_id_limit ) {
                overflow = std::move( result );
                return overflow;
            }
            by_int_id_table &by_int_id = int_answers[season][category];
            if( static_cast<size_t>( int_id ) >= by_int_id.size() ) {
                by_int_id.resize( static_cast<size_t>( int_id ) + 1 );
            }
            std::vector<answer> &for_id = by_int_id[static_cast<size_t>( int_id )];
            for_id.push_back( { suffix, intensity, std::string( variant ), std::move( result ) } );
            ++count;
            return for_id.back().result;
        }

        /** Forgets everything; whether the memo is on stays as it is. */
        void clear() {
            for( std::array<by_id_map, Categories> &by_category : answers ) {
                for( by_id_map &by_id : by_category ) {
                    by_id.clear();
                }
            }
            for( std::array<by_int_id_table, Categories> &by_category : int_answers ) {
                for( by_int_id_table &by_int_id : by_category ) {
                    by_int_id.clear();
                }
            }
            owner_raw = nullptr;
            owner_alive.reset();
            generation = 0;
            count = 0;
        }

        /** How many answers are remembered. */
        size_t size() const {
            return count;
        }

    private:
        struct answer {
            tile_lookup_suffix suffix;
            int intensity;
            std::string variant;
            Result result;
        };
        using by_id_map = std::unordered_map<std::string, std::vector<answer>>;
        // indexed by int id
        using by_int_id_table = std::vector<std::vector<answer>>;

        static const Result *find_in( const std::vector<answer> &for_id, tile_lookup_suffix suffix,
                                      int intensity, const std::string_view variant ) {
            for( const answer &a : for_id ) {
                if( a.suffix == suffix && a.intensity == intensity && a.variant == variant ) {
                    return &a.result;
                }
            }
            return nullptr;
        }

        std::array<std::array<by_id_map, Categories>, Seasons> answers;
        std::array<std::array<by_int_id_table, Categories>, Seasons> int_answers;
        bool enabled = false;
        const void *owner_raw = nullptr;
        std::weak_ptr<const void> owner_alive;
        uint64_t generation = 0;
        size_t count = 0;
        // only for out-of-range keys, which are never remembered
        Result overflow;
};

#endif // CATA_SRC_TILE_LOOKUP_MEMO_H
