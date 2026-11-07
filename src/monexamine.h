#pragma once
#ifndef CATA_SRC_MONEXAMINE_H
#define CATA_SRC_MONEXAMINE_H

#include <vector>

class Character;
class monster;
struct pet_interaction;

namespace monexamine
{

bool pet_menu( monster &z );
// MONDT-J14: the pet_interactions from JSON the pet menu offers now: the monster type's, then its
// species' (one whose id the type has too is left out), those whose conditions hold for who (alpha)
// and the pet (beta)
std::vector<const pet_interaction *> offered_pet_interactions( monster &z, Character &who );
// runs one of them, who alpha, the pet beta
void run_pet_interaction( const pet_interaction &interaction, monster &z, Character &who );
bool mech_hack( monster &z );
bool pay_bot( monster &z );
bool mfriend_menu( monster &z );

} // namespace monexamine

#endif // CATA_SRC_MONEXAMINE_H
