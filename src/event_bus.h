#pragma once
#ifndef CATA_SRC_EVENT_BUS_H
#define CATA_SRC_EVENT_BUS_H

#include <utility>
#include <vector>

#include "character_id.h"
#include "event.h"
#include "type_id.h"

class Creature;
class event_subscriber;
class item_location;
class talker;
class vehicle;

class event_bus
{
    public:
        event_bus() = default;
        event_bus( const event_bus & ) = delete;
        event_bus &operator=( const event_bus & ) = delete;
        ~event_bus();
        void subscribe( event_subscriber * );
        void unsubscribe( event_subscriber * );

        void send( const cata::event & ) const;
        void send_with_talker( Creature *, Creature *, const cata::event & ) const;
        void send_with_talker( Creature *, item_location *, const cata::event & ) const;
        void send_with_talker( vehicle *alpha, Creature *, const cata::event &e ) const;
        /** Any talkers: each subscriber gets its own copy. A null alpha is looked up as for send */
        void send_with_talkers( const talker *alpha, const talker *beta,
                                const cata::event &e ) const;
        /** For an event no creature caused: its EOCs get an empty alpha talker, not the avatar */
        void send_without_talker( const cata::event &e ) const;
        template<event_type Type, typename... Args>
        void send( Args &&... args ) const {
            send( cata::event::make<Type>( std::forward<Args>( args )... ) );
        }
    private:
        std::vector<event_subscriber *> subscribers;
};

event_bus &get_event_bus();

/**
 * The killer fields of character_dies and monster_dies for @p killer: the character that
 * killed, or whose summon did (invalid otherwise), and the monster that did (mon_null otherwise).
 */
std::pair<character_id, mtype_id> event_killer_fields( const Creature *killer );

#endif // CATA_SRC_EVENT_BUS_H
