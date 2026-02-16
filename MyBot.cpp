#include "hlt/game.hpp"
#include "hlt/constants.hpp"
#include "hlt/log.hpp"

#include <random>
#include <ctime>
#include <set>
#include <map>

using namespace std;
using namespace hlt;

#ifdef _DEBUG
# define LOG(X) log::log(X);
#else
# define LOG(X)
#endif // DEBUG

enum class ShipState {
    HARVESTING,
    RETURNING
};

int main(int argc, char* argv[]) {
    unsigned int rng_seed;
    if (argc > 1) rng_seed = static_cast<unsigned int>(stoul(argv[1]));
    else rng_seed = static_cast<unsigned int>(time(nullptr));
    mt19937 rng(rng_seed);

    Game game;
    game.ready("Mon pote amélioré");

    map<EntityId, ShipState> ship_states; //état des vaisseaux (RECOLTE, RETOUR VAISSEAU MERE)

    for (;;) {
        game.update_frame();
        vector<Command> command_queue;
        shared_ptr<Player> me = game.me;
        unique_ptr<GameMap>& game_map = game.game_map;

        // "Carte" des positions où nos vaisseaux vont aller ce tour-ci pour éviter les collisions
        // On remplit ça à chaque décision de mouvement
        set<Position> intended_positions;

        //* --Gestion des petits vaisseaux-- *
        for (const auto& ship_iterator : me->ships) {

            shared_ptr<Ship> ship = ship_iterator.second;
            EntityId id = ship->id;

            //Initialisation de l'état des nouveaux vaisseaux
            if (ship_states.find(id) == ship_states.end()) {
                ship_states[id] = ShipState::HARVESTING;
            }

            //Si vaisseau plein => état retour vaisseau mère
            if (ship->halite >= constants::MAX_HALITE * 0.95) {
                ship_states[id] = ShipState::RETURNING;
            }
            // Si vaisseau vide => état récolte
            else if (ship->halite == 0) {
                ship_states[id] = ShipState::HARVESTING;
            }

            // Retour au vaisseau mère en panique avant fin de la partie
            int turns_to_home = game_map->calculate_distance(ship->position, me->shipyard->position);
            if (game.turn_number > constants::MAX_TURNS - (turns_to_home + 10)) {
                ship_states[id] = ShipState::RETURNING;
            }

            
            Command command = ship->stay_still();
            bool move_decided = false;

            //Comportement de l'état retour
            if (ship_states[id] == ShipState::RETURNING) {
                if (ship->position == me->shipyard->position) {
                    move_decided = true; //Dépose ressource quand sur vaisseau mère
                }
                else {
                    Direction dir = game_map->naive_navigate(ship, me->shipyard->position); //retour vaisseau mère tout droit (! Sensible au collision)
                    command = ship->move(dir);
                    move_decided = true;
                }
            }
            else { //Comportement de l'état récolte 
                // Si la case actuelle contient encore beaucoup de ressources (100 ici),
                // on reste dessus
                if (game_map->at(ship)->halite > 100 && !ship->is_full()) {
                    command = ship->stay_still();
                    move_decided = true;
                }
                else {
                    // Recherche de la case proche la plus intéressante
                    int max_score = -1;
                    Direction best_dir = Direction::STILL;

                    for (const auto& dir : ALL_CARDINALS) {
                        Position target_pos = ship->position.directional_offset(dir);

                        // Verification de si la case est déjà prévue par un autre vaisseau => éviter les collisions
                        if (intended_positions.count(target_pos)) continue;

                        int cell_halite = game_map->at(target_pos)->halite;

                        // Définition des scores d'intéret pour les cases adjacentes
                        if (cell_halite > max_score) {
                            max_score = cell_halite;
                            best_dir = dir;
                        }
                    }

                    // Si on a trouvé une direction intéressante (et différente de rester sur place si c'est vide)
                    if (best_dir != Direction::STILL) {
                        command = ship->move(best_dir);
                        move_decided = true;
                    }
                }
            }

            // Enregistrement de la prochaine position pour éviter les collision avec les autres vaisseaux
            // (! sensible au collision => point d'amélioration)
            Position future_pos = ship->position;

            command_queue.push_back(command);
        }

        //* --Gestion du vaisseau mère-- *
        if (game.turn_number <= 250 &&
            me->halite >= constants::SHIP_COST &&
            !game_map->at(me->shipyard)->is_occupied())
        {
            command_queue.push_back(me->shipyard->spawn());
        }

        if (!game.end_turn(command_queue)) break;
    }
    return 0;
}

/* 
Inventaire des fonctions pratiques pour donner des directives au bot :
    
    spawn() : fait apparaitre un petit vaisseau sur la case du vaisseau mère
    stay_still() : petit vaisseau reste sur place pour cette frame
    move() : déplace le vaisseau dans une direction donnée (cardinale)
    naive_navigate() : déplacement simple vers une cible

    Getter :
        is_full() : petit vaisseau plein
        is_occupied() : est ce que la case est occupée
        at() : récupère position de l'élément (vaisseau mère ou petit vaisseau, case...)
*/

/*
Inventaire des fonctions de gestion de partie :
    push_back() : ajouter une instruction à la liste des instructions à faire
    end_turn() : regarde si la liste d'instruction est terminée
    update_frame() : actualise l'entièreté de la partie est initialise le tour actuel
*/