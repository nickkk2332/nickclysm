#pragma once
#ifndef CATA_SRC_TILE_LOOKUP_MEMO_H
#define CATA_SRC_TILE_LOOKUP_MEMO_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
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
 */
template<typename Result, size_t Seasons, size_t Categories>
class tile_lookup_memo
{
    public:
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
                            const std::string &variant ) const {
            if( season >= Seasons || category >= Categories ) {
                return nullptr;
            }
            const by_id_map &by_id = answers[season][category];
            const auto found = by_id.find( id );
            if( found == by_id.end() ) {
                return nullptr;
            }
            for( const answer &a : found->second ) {
                if( a.suffix == suffix && a.intensity == intensity && a.variant == variant ) {
                    return &a.result;
                }
            }
            return nullptr;
        }

        /**
         * Remembers `result` as the answer to this lookup and returns it.  The reference is
         * valid until the next call that changes the memo.
         */
        const Result &remember( size_t season, size_t category, const std::string &id,
                                tile_lookup_suffix suffix, int intensity, const std::string &variant,
                                Result result ) {
            if( season >= Seasons || category >= Categories ) {
                overflow = std::move( result );
                return overflow;
            }
            std::vector<answer> &for_id = answers[season][category][id];
            for_id.push_back( { suffix, intensity, variant, std::move( result ) } );
            ++count;
            return for_id.back().result;
        }

        void clear() {
            for( std::array<by_id_map, Categories> &by_category : answers ) {
                for( by_id_map &by_id : by_category ) {
                    by_id.clear();
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

        std::array<std::array<by_id_map, Categories>, Seasons> answers;
        const void *owner_raw = nullptr;
        std::weak_ptr<const void> owner_alive;
        uint64_t generation = 0;
        size_t count = 0;
        // only for out-of-range keys, which are never remembered
        Result overflow;
};

#endif // CATA_SRC_TILE_LOOKUP_MEMO_H
