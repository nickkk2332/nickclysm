#pragma once
#ifndef CATA_SRC_ADDICTION_H
#define CATA_SRC_ADDICTION_H

#include <string>
#include <string_view>
#include <vector>

#include "calendar.h"
#include "translation.h"
#include "type_id.h"

class Character;
class JsonObject;
class JsonOut;

// Minimum intensity before effects are seen
constexpr int MIN_ADDICTION_LEVEL = 3;
constexpr int MAX_ADDICTION_LEVEL = 20;

struct add_type {
    private:
        translation _type_name;
        translation _name;
        translation _desc;
        morale_type _craving_morale;
        effect_on_condition_id _effect;
        std::string _builtin;
        // HLTH-J11: the lifecycle, which used to be the same for every addiction
        time_duration _dose_satiation = 2_hours;
        time_duration _recovery_time = 6_hours;
        time_duration _intensity_decay = 10_minutes;
        int _max_intensity = MAX_ADDICTION_LEVEL;
        int _remove_below = MIN_ADDICTION_LEVEL;
    public:
        addiction_id id;
        bool was_loaded = false;
        static void load_add_types( const JsonObject &jo, const std::string &src );
        static void reset();
        static void check_add_types();
        static void finalize_all();
        void load( const JsonObject &jo, std::string_view src );
        static const std::vector<add_type> &get_all();

        const translation &get_name() const {
            return _name;
        }
        /**
         * Returns the name of an addiction. It should be able to finish the sentence
         * "Became addicted to ______".
         */
        const translation &get_type_name() const {
            return _type_name;
        }
        const translation &get_description() const {
            return _desc;
        }
        const morale_type &get_craving_morale() const {
            return _craving_morale;
        }
        const effect_on_condition_id &get_effect() const {
            return _effect;
        }
        const std::string &get_builtin() const {
            return _builtin;
        }
        /** How long a dose sates this addiction (before the character's ADDICTION_SATIATION). */
        time_duration get_dose_satiation() const {
            return _dose_satiation;
        }
        /** How long past the end of its satiation it takes to lose a level (before ADDICTION_RECOVERY). */
        time_duration get_recovery_time() const {
            return _recovery_time;
        }
        /** How much sooner each level of intensity ends the recovery. */
        time_duration get_intensity_decay() const {
            return _intensity_decay;
        }
        /** The intensity doses can't raise it past. */
        int get_max_intensity() const {
            return _max_intensity;
        }
        /** An addiction whose recovery ends below this intensity is gone instead of losing a level. */
        int get_remove_below() const {
            return _remove_below;
        }
        /** get_dose_satiation(), changed by @p u's ADDICTION_SATIATION enchantment values. */
        time_duration dose_satiation_for( const Character &u ) const;
        /** get_recovery_time(), changed by @p u's ADDICTION_RECOVERY enchantment values. */
        time_duration recovery_time_for( const Character &u ) const;
};

/** The strength of a dose of an addictive substance for @p u: @p strength changed by the
 *  character's ADDICTION_STRENGTH enchantment values (HLTH-J11). */
int addiction_strength_for( const Character &u, int strength );

class addiction
{
    public:
        addiction_id type;
        int intensity = 0;
        time_duration sated = 1_hours;

        addiction() = default;
        explicit addiction( const addiction_id &t, const int i = 1 ) : type {t}, intensity {i} { }

        void serialize( JsonOut &json ) const;
        void deserialize( const JsonObject &jo );

        /** Runs one update of this addiction's effects **/
        bool run_effect( Character &u );
        /** One turn of recovery: uses up a turn of satiation and, once the recovery time is
         *  over, loses a level. True when the addiction is over and must be removed. */
        bool recover( const Character &u );
};

std::string add_type_legacy_conv( std::string const &v );

// Adds an addiction to a list that holds one entry per type: an entry of the same type takes the
// higher intensity and the longer satiation instead (PROG-B07).
void merge_addiction( std::vector<addiction> &addictions, const addiction &add );

#endif // CATA_SRC_ADDICTION_H
